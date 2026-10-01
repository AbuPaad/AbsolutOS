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
 * ModelCatalog.h - the curated list behind the AI app's model picker.
 *
 * Bundled, not fetched. The device should not have to make a second request
 * before it can ask a question, and a fixed list means every row is guaranteed
 * an icon. The cost is that it goes stale - these are real slugs from
 * GET https://openrouter.ai/api/v1/models, checked 2026-09-29. Re-verify before a
 * release with:
 *   curl -s https://openrouter.ai/api/v1/models | jq -r '.data[].id'
 *
 * `id` is the exact slug and goes on the wire untouched - never prettify it.
 * `label` is display only: the provider's own name for the model with the vendor
 * prefix stripped, because the icon already says which company it is.
 */
#pragma once

#include <cstring>

#include "ai/AiModelIcons.h"

namespace ai {

struct ModelEntry {
    const char*     id;
    const char*     label;
    icons::Provider provider;
};

inline constexpr ModelEntry kModels[] = {
    {"openai/gpt-6-luna-pro",                   "GPT-6 Luna Pro",          icons::Provider::OpenAi},
    {"openai/gpt-6-luna",                       "GPT-6 Luna",              icons::Provider::OpenAi},
    {"openai/gpt-6-astra",                      "GPT-6 Astra",             icons::Provider::OpenAi},
    {"anthropic/claude-sonnet-5.5",             "Claude Sonnet 5.5",       icons::Provider::Anthropic},
    {"anthropic/claude-opus-5.5",               "Claude Opus 5.5",         icons::Provider::Anthropic},
    {"anthropic/claude-fable-5.1",              "Claude Fable 5.1",        icons::Provider::Anthropic},
    {"google/gemini-3.8-flash",                 "Gemini 3.8 Flash",        icons::Provider::Gemini},
    {"google/gemini-3.5-flash-lite",            "Gemini 3.5 Flash Lite",   icons::Provider::Gemini},
    {"google/gemini-3.1-flash-image",           "Nano Banana 2", icons::Provider::Gemini},
    {"deepseek/deepseek-v4.1-flash",            "DeepSeek V4.1 Flash",     icons::Provider::DeepSeek},
    {"deepseek/deepseek-v4-pro",                "DeepSeek V4 Pro 0423",    icons::Provider::DeepSeek},
    {"mistralai/mistral-medium-3-5",            "Mistral Medium 3.5",      icons::Provider::MistralAi},
    {"mistralai/ministral-8b-2512",             "Ministral 3 8B 2512",     icons::Provider::MistralAi},
    {"meta-llama/llama-4-maverick",             "Llama 4 Maverick",        icons::Provider::Meta},
    {"meta-llama/llama-3.3-70b-instruct",       "Llama 3.3 70B Instruct",  icons::Provider::Meta},
    {"x-ai/grok-4.7",                           "Grok 4.7",                icons::Provider::XAi},
    {"x-ai/grok-4.20",                          "Grok 4.20",               icons::Provider::XAi},
    {"qwen/qwen3.8-max-prime",                  "Qwen3.8 Max Prime",       icons::Provider::Qwen},
    {"qwen/qwen3.8-flash",                      "Qwen3.8 Flash",           icons::Provider::Qwen},
    {"nvidia/nemotron-3-ultra-550b-a55b",       "Nemotron 3 Ultra",        icons::Provider::Nvidia},
    {"perplexity/sonar-pro",                    "Sonar Pro",               icons::Provider::Perplexity},
    {"moonshotai/kimi-k3",                      "Kimi K3",                 icons::Provider::MoonshotAi},
    {"minimax/minimax-m3",                      "MiniMax M3",              icons::Provider::MiniMax},
    {"baidu/ernie-4.5-vl-424b-a47b",            "ERNIE 4.5 VL",  icons::Provider::Baidu},
    {"bytedance/ui-tars-1.5-7b",                "UI-TARS 7B",              icons::Provider::ByteDance},
    {"openrouter/auto",                         "Auto Router",             icons::Provider::OpenRouter},
};

inline constexpr int kModelCount = static_cast<int>(sizeof(kModels) / sizeof(kModels[0]));

/// Index of the entry with this exact id, or -1. The config stores an id, so
/// this is how the picker marks the current selection.
inline int findModelById(const char* id) {
    if (!id) return -1;
    for (int i = 0; i < kModelCount; ++i) {
        if (std::strcmp(kModels[i].id, id) == 0) return i;
    }
    return -1;
}

}  // namespace ai
