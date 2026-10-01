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
 * AiModelIcons.h — generated. Do not edit by hand.
 *
 * Regenerate with: node ~/.hermes/cache/scratch/gen_ai_icons.js
 * Source marks: Simple Icons (CC0), plus Wikimedia Commons for the three that
 * were withdrawn there on trademark request (OpenAI, xAI, Grok). Bundled
 * deliberately, with the trademark exposure noted in the project record.
 *
 * 20x20 RGB565A8 per asset: a white RGB565 colour plane then an A8 alpha plane.
 * Monochrome by design — the white glyph reads correctly on both the idle row
 * background and the accent-blue focused row, so there is no second colourway to
 * keep in sync. Two sizes exist and the AI app's settings toggle picks between
 * them; both sit on the same 20x20 canvas so the row geometry never changes.
 *
 * Index by provider and size; the array order IS the enum order.
 */
#pragma once

#include <cstdint>

namespace ai {
namespace icons {

inline constexpr int kIconW        = 20;
inline constexpr int kIconH        = 20;
inline constexpr int kIconStride   = kIconW * 2;           // RGB565A8: bytes per colour-plane row
inline constexpr int kIconDataSize = kIconW * kIconH * 3;  // 2 bytes/px colour + 1 byte/px alpha

enum class Provider : uint8_t {
    OpenAi = 0,
    Anthropic = 1,
    Gemini = 2,
    Google = 3,
    DeepSeek = 4,
    MistralAi = 5,
    Meta = 6,
    XAi = 7,
    Grok = 8,
    Qwen = 9,
    Nvidia = 10,
    Perplexity = 11,
    MoonshotAi = 12,
    MiniMax = 13,
    Ollama = 14,
    OpenRouter = 15,
    HuggingFace = 16,
    AlibabaCloud = 17,
    Baidu = 18,
    ByteDance = 19,
    Count,
};

enum class IconSize : uint8_t {
    Compact = 0,
    Large = 1,
    Count,
};

inline constexpr int kProviderCount = static_cast<int>(Provider::Count);
inline constexpr int kIconSizeCount = static_cast<int>(IconSize::Count);

/// Human-readable provider name.
const char* providerName(Provider p);

/// 20x20 RGB565A8 blob, or nullptr for an out-of-range provider or size.
const uint8_t* providerIcon(Provider p, IconSize size);

}  // namespace icons
}  // namespace ai
