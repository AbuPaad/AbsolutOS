/*
 * NeoCalculator - NumOS
 * Copyright (C) 2026 Juan Ramon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/**
 * net/TlsSession.h — the ONE place the TLS memory budget and the TLS session
 * serialisation live.
 *
 * WHY THIS EXISTS
 * ---------------
 * mbedTLS takes its record buffers from the INTERNAL heap: two blocks of
 * MBEDTLS_SSL_MAX_CONTENT_LEN (16384), fixed inside the prebuilt libmbedtls.a
 * because asymmetric and dynamic buffers are both off in this SDK. A handshake
 * that cannot get the SECOND 16 KB block reports `ESP_ERR_HTTP_CONNECT` with
 * errno 0 — byte-for-byte the same failure shape as a bad certificate, so it
 * used to look like a network fault and cost days.
 *
 * Three things were wrong and none is a threshold tweak:
 *   1. More than one session could be live at once (the boot OTA auto-check on
 *      core 0, the AI request on core 1, the Wolfram hop). Each live session
 *      holds 2x16 KB internal, so the peak demand was 64 KB+ and the per-session
 *      pre-open guard could not see the other session at all.
 *   2. The floor was checked in TWO places with DIFFERENT numbers, and the
 *      "lower" one was dead code because the higher one ran anyway.
 *   3. mbedTLS itself was pinned to internal DRAM by the SDK. The shipping
 *      Arduino-ESP32 sdkconfig sets CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=1, so the
 *      handshake could not use PSRAM even with 8 MB free, and the ssl context
 *      (one ~32 KB contiguous allocation carrying both 16 KB record buffers)
 *      competed with the display draw buffer for the same scarce pool. The
 *      binding quantity was never "free heap" but the largest CONTIGUOUS block
 *      of DMA-capable internal RAM: measured on hardware, a handshake failed
 *      with largest 39,924 B and succeeded with largest 44,020 B, printing
 *      `esp-sha: Failed to allocate buf memory` -> cert verify FFFFFFFF ->
 *      handshake -0x3000 — i.e. an out-of-space fault wearing a certificate
 *      error's clothes.
 *
 * THE CONTRACT
 * ------------
 * `tlsSessionAcquire()` before the socket, `tlsSessionRelease()` after it, and
 * exactly one canonical `tlsMemoryCheck()` while the lock is held. Every https
 * request in the firmware funnels through net::HttpStream, so this is the single
 * chokepoint: at most one session's buffers are ever live, the memory check sees
 * the heap the session will actually get, and the mbedTLS allocator is redirected
 * off internal DRAM before the first handshake can allocate anything.
 *
 * Header-only on purpose: nothing new is added to a build_src_filter, and the
 * whole thing is inert off-device (agents.md §5.5).
 */

#pragma once

#include <cstddef>
#include <cstdint>

// These MUST stay outside `namespace net`: including a standard/Arduino header
// inside a namespace injects every symbol it declares (std::, etc.) into that
// namespace, so `std` would resolve to `net::std` and every std:: use in this
// file and its includers breaks.
#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>
#include <mbedtls/platform.h>   // mbedtls_platform_set_calloc_free()
#endif

namespace net {

/// mbedTLS's fixed content-buffer size in this SDK. One block MUST be findable
/// before a handshake has any chance.
constexpr size_t kTlsContentBytes = 16384u;

/**
 * Canonical internal-RAM floor for ONE TLS session, checked while the session
 * lock is held (so no other session is competing for these bytes).
 *
 * RECALIBRATED after the mbedTLS allocator was moved to PSRAM (see
 * tlsEnsurePsramAllocator below). The handshake no longer needs 32 KB of
 * contiguous internal DRAM, so the old 48 KB / 16 KB floor would now refuse
 * requests that are certain to succeed — at boot the device sits at ~47 KB free,
 * i.e. the old floor alone kept the OTA check failing after the real cause was
 * fixed. What internal RAM is still genuinely required is small and is the esp_sha
 * scratch buffer plus misc transients: on hardware, with the allocator redirected,
 * the handshake completed down to free 20,320 B / largest 12,788 B.
 *
 *   - free >= 16 KB: headroom for the non-mbedTLS transients and the worker task.
 *   - largest >= 8 KB: measured with the mask the crypto scratch actually uses
 *     (DMA|INTERNAL|8BIT), not plain INTERNAL — esp_sha asks for
 *     MALLOC_CAP_8BIT|MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL, and on the S3 the
 *     DMA-capable internal pool is a strict subset of the internal pool, so an
 *     INTERNAL-only check can pass while that allocation fails.
 */
constexpr size_t kTlsMinInternalFree    = 16u * 1024u;
constexpr size_t kTlsMinInternalLargest = 8u * 1024u;

/// The mask esp_sha_dma() uses for its scratch block. Checked separately so a
/// shortage of DMA-capable (not merely internal) RAM fails by name.
constexpr uint32_t kTlsScratchCaps = MALLOC_CAP_8BIT | MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;

/// Live internal-heap figures, plus whether they clear the floor above.
struct TlsMemReport {
    size_t freeBytes    = 0;
    size_t largestBytes = 0;
    bool   ok           = false;
};

#if defined(ARDUINO)

namespace detail {

/**
 * A COUNTING semaphore that starts at 1, not a mutex: the abort path releases
 * this from a DIFFERENT task than the one that acquired it (DeviceTransport's
 * worker owns the socket, the loop task calls close() to unblock it), and a
 * FreeRTOS mutex may only be given by its owner. No priority inheritance, which
 * is fine — every TLS user here runs at the same priority.
 *
 * Meyers singleton: C++11 guarantees the static is constructed exactly once,
 * thread-safely, so two tasks racing the first request cannot each build one.
 */
struct TlsLock {
    StaticSemaphore_t storage;
    SemaphoreHandle_t handle;
    TlsLock() : handle(xSemaphoreCreateCountingStatic(1, 1, &storage)) {}
};

inline SemaphoreHandle_t tlsLock() {
    static TlsLock s_lock;
    return s_lock.handle;
}

/// Count of sessions currently holding the lock — read by OtaUpdater to defer
/// the boot auto-check so it never queues behind (or in front of) a user request.
inline std::atomic<int>& tlsActiveCount() {
    static std::atomic<int> s_n{0};
    return s_n;
}

}  // namespace detail

/**
 * mbedTLS's allocator callback pair, redirected to PSRAM.
 *
 * mbedTLS is built with MBEDTLS_PLATFORM_MEMORY, so the allocator behind
 * mbedtls_calloc()/mbedtls_free() is a pair of function pointers rather than a
 * compile-time choice — which is what makes this fix possible WITHOUT rebuilding
 * the SDK. The shipping framework ships prebuilt libs and the platform has no
 * custom_sdkconfig, so CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=1 cannot simply be
 * flipped; overriding at runtime is the only in-repo route.
 *
 * PSRAM first, internal only as a fallback if PSRAM is exhausted, so TLS keeps
 * working either way. heap_caps_free() returns the block to whichever heap owns
 * it, so the fallback is safe. Consequence worth knowing: record buffers now sit
 * in PSRAM, so the crypto engines take their non-DMA copy path — that path's
 * internal scratch is small (<= one SHA block) and is exactly what
 * kTlsMinInternalLargest guards.
 */
inline void* tlsPsramCalloc(size_t n, size_t size) {
    void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p;
}

inline void tlsPsramFree(void* p) { heap_caps_free(p); }

/**
 * Install the PSRAM allocator once, before the first handshake allocates
 * anything. Called from tlsSessionAcquire() so it cannot be forgotten by a new
 * caller, and idempotent so repeated sessions are free. Logs once: a silent
 * failure to install is precisely the class of bug this file exists to prevent,
 * and it costs days when nobody can tell whether the override took effect.
 */
inline void tlsEnsurePsramAllocator() {
    static bool s_installed = false;
    if (s_installed) return;
    s_installed = true;   // set first: never retry in a loop, even if the call fails
    const int rc = mbedtls_platform_set_calloc_free(&tlsPsramCalloc, &tlsPsramFree);
    Serial.printf("[TLS] mbedTLS allocator -> PSRAM: %s (rc=%d)\n",
                  rc == 0 ? "ok" : "FAILED - handshake stays on internal DRAM", rc);
}

/// Take the one-and-only TLS slot. false = timed out; the caller must fail with
/// a name, never proceed. timeoutMs is clamped so a caller cannot wait forever.
inline bool tlsSessionAcquire(uint32_t timeoutMs) {
    tlsEnsurePsramAllocator();   // must precede the first mbedTLS allocation
    SemaphoreHandle_t h = detail::tlsLock();
    if (!h) return true;   // best effort if FreeRTOS refused the static create
    const TickType_t ticks = pdMS_TO_TICKS(timeoutMs ? timeoutMs : 1u);
    if (xSemaphoreTake(h, ticks) != pdTRUE) return false;
    detail::tlsActiveCount().fetch_add(1, std::memory_order_relaxed);
    return true;
}

/// Give the slot back. Safe from any task; the counter floors at zero so a
/// double release cannot wedge every future request.
inline void tlsSessionRelease() {
    int prev = detail::tlsActiveCount().fetch_sub(1, std::memory_order_acq_rel);
    if (prev <= 0) detail::tlsActiveCount().store(0, std::memory_order_relaxed);
    SemaphoreHandle_t h = detail::tlsLock();
    if (h) xSemaphoreGive(h);
}

/// True while any TLS session is live anywhere in the firmware.
inline bool tlsSessionBusy() {
    return detail::tlsActiveCount().load(std::memory_order_acquire) > 0;
}

inline TlsMemReport tlsMemoryCheck() {
    TlsMemReport r;
    r.freeBytes    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    r.largestBytes = heap_caps_get_largest_free_block(kTlsScratchCaps);
    r.ok = (r.freeBytes >= kTlsMinInternalFree &&
            r.largestBytes >= kTlsMinInternalLargest);
    return r;
}

#else  // !ARDUINO — emulator / host: one process, one socket, no arbitration

inline bool tlsSessionAcquire(uint32_t)        { return true; }
inline void tlsSessionRelease()                {}
inline bool tlsSessionBusy()                   { return false; }
inline TlsMemReport tlsMemoryCheck()           { return TlsMemReport{}; }

#endif

}  // namespace net
