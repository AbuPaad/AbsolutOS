# MATLAB-03-USB-HID — does enabling USB CDC break anything?

Date: 2026-10-18 · Branch: `main` · Author: config read-only audit (nothing built)
Status: **answer to a question.** No flags changed, no firmware flashed.

---

## 1. The premise is wrong: CDC is not disabled

Native USB CDC is **already on, in every env that matters**, and the production
env *cannot build* without it.

| Env | Flags | Line |
|:--|:--|:--|
| `esp32s3_n16r8` (bench/base) | `ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`, `NUMOS_SERIAL_BACKEND_USB_CDC=1` | `platformio.ini:49-51` |
| `numos-esp32-s3-wroom-1u-n16r8` (production) | same three | `platformio.ini:161-163` |

And `scripts/check-production-target.py` hard-fails the build if any of it is
missing:

- board manifest must carry `ARDUINO_USB_MODE=1` and `ARDUINO_USB_CDC_ON_BOOT=1`
- `hwids` must contain **`0x303A:0x1001`** — the native ESP USB Serial/JTAG VID/PID
- the production env must contain `-DNUMOS_SERIAL_BACKEND_USB_CDC=1`
- and must **not** claim an onboard UART bridge

The comment at `platformio.ini:44-48` says it plainly: the bench rig was moved to
the module's native USB socket to match production, the console is on
`/dev/ttyACM0`, and the CH343 header (GPIO43/44) now carries only the ROM banner.

**So `docs/HARDWARE.md` §6 is stale** — it still claims CDC "is disabled in the
production environment", which was true of an older UART0-era setup. Do not use
that section as the source of truth; `platformio.ini` and the gate script are.

Also misleading by name: `esp32s3_n16r8_validate_usbcdc` is not "the CDC
experiment" — it is the env that *forces* CDC on by unflagging the UART0 variant.

## 2. So what is actually off? HID, and that is a different flag

`ARDUINO_USB_MODE=1` selects the **ROM's USB Serial/JTAG peripheral**. That is a
fixed-function device whose USB descriptor lives in ROM: CDC console + JTAG
debugging, and nothing else. **You cannot add a HID interface to it** — there is
no descriptor to edit. That is why CDC being on does *not* mean keyboard mode is
nearly free.

A HID keyboard requires `ARDUINO_USB_MODE=0`, which switches to **TinyUSB**
(USB-OTG) in the Arduino-ESP32 core, where the descriptor is yours to define —
including a composite CDC + HID device.

| | `USB_MODE=1` (today) | `USB_MODE=0` (HID) |
|:--|:--|:--|
| Stack | ROM USB Serial/JTAG | TinyUSB |
| Console | CDC + JTAG, VID/PID `0x303A:0x1001` | TinyUSB CDC, different VID/PID |
| HID possible | **no** | yes, via descriptor |
| Built-in JTAG | yes | **no** |
| Code needed | none | descriptor + `USBHIDKeyboard` + re-validation |

## 3. What flipping to `USB_MODE=0` would actually cost

Nothing hardware. Four real software costs:

1. **Built-in JTAG debug is lost.** The production env sets
   `debug_tool = esp-builtin`, which only works with USB Serial/JTAG. Under
   TinyUSB you go back to an external probe, or keep a `USB_MODE=1` env for
   debugging.
2. **Your own production gate rejects it.** `check-production-target.py` asserts
   `0x303A:0x1001` in `hwids`, `ARDUINO_USB_MODE=1` in the manifest and
   `NUMOS_SERIAL_BACKEND_USB_CDC=1` in the env. A `USB_MODE=0` production build
   fails the check by design. (The gate stays correct — production should stay on
   ROM CDC.)
3. **Console and boot sequencing re-open.** `src/main.cpp:156` waits up to 3 s on
   `while (!Serial && millis()-start < 3000)` when the CDC backend is selected;
   under TinyUSB the enumeration timing and the monitor reconnect behaviour both
   shift and need the same `monitor_rts=0` discipline re-verified.
4. **Composite descriptor work.** CDC + HID together means defining the interface
   collection properly; that is where the actual fiddling is.

## 4. What *cannot* break

- **Bricking: not a thing here.** Download mode lives in ROM on GPIO19/20 and is
  independent of whatever the application configures USB as. A bad USB build is
  recovered by the normal bootloader sequence.
- **The CH343 header stays as an out-of-band fallback.** It is a separate chip on
  GPIO43/44 carrying UART0; changing the S3's *native* USB stack does not touch
  it. Worst case is a reflash over that header — which is exactly what production
  provisioning already does.
- **The keypad, display, camera and SD pins are untouched.** Verified: no
  conflict on 19/20 — the `19`/`20` hits in `ProductionKeypadMap.generated.h` are
  key IDs and matrix coordinates, and `ProductionDisplayProfile.cpp:299` is a
  buffer offset, not a pin.

## 5. Recommendation

**Do not touch the production env.** Add a sibling that extends it:

```ini
[env:numos-esp32-s3-wroom-1u-n16r8-hid]
extends = env:numos-esp32-s3-wroom-1u-n16r8
build_unflags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
build_flags =
    -DARDUINO_USB_MODE=0
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DNUMOS_USB_HID_KEYPAD=1
```

(Exact flag spelling to be confirmed against the core version actually pinned —
no `platform_packages` pin exists in `platformio.ini`, so verify before building.)

That keeps: the gate script passing, production on ROM CDC, JTAG available in
normal builds, and the HID experiment isolated to one env that can be flashed and
thrown away.

## 6. Loose end worth fixing separately

`docs/HARDWARE.md` §6 states the opposite of the code and will keep costing
someone an evening. It should be corrected to: *native USB CDC is the production
serial backend; the CH343 header carries UART0 and is the out-of-band fallback.*
Not changed here — owner's call.

---

### Evidence

- `platformio.ini:49-51`, `:161-163` — CDC flags in base + production envs
- `platformio.ini:44-48` — bench rig moved to native USB, console on ttyACM0
- `platformio.ini:345-356` — `..._validate_usbcdc` forces CDC on
- `scripts/check-production-target.py:140-165` — the enforced contract
- `src/input/NumosSerialBackend.h:6-18` — backend selection
- `src/main.cpp:145-170` — boot-time `while(!Serial)` window
- `docs/HARDWARE.md` §6 — **stale**, contradicts the above
- `docs/PRODUCTION_PCBA_HARDWARE_CONTRACT.md` — GPIO19/20 + PCBWay native USB test
