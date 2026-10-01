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
 * net/DeviceTransport.h — the firmware's AiTransport (WiFi + mbedTLS +
 * esp_http_client), pumped on its own FreeRTOS task.
 *
 * Arduino-free: it only forward-declares the two ai:: types, so main.cpp, Wifi,
 * and the app layer can name the factory without dragging esp_http_client in.
 *
 * The factory is defined ONLY in an ARDUINO build. AiClient.cpp's device branch
 * compiles to a named StubTransport everywhere else, so the emulator and the
 * host test stay keyless and socketless.
 */

#pragma once

namespace ai {
struct AiConfig;
class AiTransport;
}  // namespace ai

namespace net {

ai::AiTransport* makeDeviceTransport(const ai::AiConfig& cfg);

}  // namespace net
