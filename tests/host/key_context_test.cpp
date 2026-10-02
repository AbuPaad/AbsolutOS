/*
 * NeoCalculator - NumOS
 *
 * Host test for the shared context tables (src/ui/AppContext.h,
 * src/ui/KeyContext.h). No LVGL, no Arduino, no display — so it builds with a
 * bare g++ like tests/host/notes_mdrender_test.cpp does:
 *
 *   g++ -std=gnu++17 -I src tests/host/key_context_test.cpp -o /tmp/kc && /tmp/kc
 *
 * Two jobs:
 *   1. Assert the invariants the renderers depend on (the device soft-key bar
 *      has five slots; HOME/MODE/ON are never dead; no duplicate keys in a
 *      context).
 *   2. Dump the tables in the same canonical form scripts/gen_key_context.py
 *      emits, so the C++ and the JavaScript can be compared mechanically. Two
 *      hand-checked copies is exactly how the web pad and the firmware drift.
 *
 * The dump goes to stdout with --dump; otherwise it runs assertions only.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ui/AppContext.h"
#include "ui/KeyContext.h"

using numos::Ctx;
using numos::KeyRole;
using numos::Role;

namespace {

int failures = 0;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        ++failures;
        std::printf("FAIL %s\n", message.c_str());
    } else {
        std::printf("ok   %s\n", message.c_str());
    }
}

const char* roleName(Role role)
{
    switch (role) {
        case Role::Primary:   return "primary";
        case Role::Secondary: return "secondary";
        case Role::Disabled:  return "disabled";
    }
    return "disabled";
}

/// The 5 soft-key slots on the device bar. More primaries than this silently
/// drops keys off the bar.
constexpr int kSoftKeySlots = 5;

void dump(std::vector<Ctx> all)
{
    // Canonical form, matched by the Node side of the comparison:
    //   <slug>:<ID>=<role>;<ID>=<role>;...
    for (Ctx ctx : all) {
        const numos::ContextMap* map = numos::contextFor(ctx);
        std::printf("%s:", numos::ctxSlug(ctx));
        if (map) {
            for (uint8_t index = 0; index < map->count; ++index) {
                if (index) std::printf(";");
                std::printf("%d=%s", static_cast<int>(map->keys[index].key),
                            roleName(map->keys[index].role));
            }
        }
        std::printf("\n");
    }
}

}  // namespace

int main(int argc, char** argv)
{
    const bool dumping = argc > 1 && std::strcmp(argv[1], "--dump") == 0;

    std::vector<Ctx> all;
    for (int value = 0; value < static_cast<int>(Ctx::Count); ++value) {
        all.push_back(static_cast<Ctx>(value));
    }

    if (dumping) {
        dump(all);
        return 0;
    }

    // Every context has a map, and Ctx::Count is a sentinel with none.
    for (Ctx ctx : all) {
        require(numos::contextFor(ctx) != nullptr,
                std::string("context table exists for ") + numos::ctxSlug(ctx));
    }
    require(numos::contextFor(Ctx::Count) == nullptr,
            "Ctx::Count is a sentinel, not a context");

    // Names and slugs are distinct: a collision would make a CSS selector or a
    // diagnostic field ambiguous. Aggregated into one check so the output stays
    // readable.
    {
        std::vector<std::string> slugProblems;
        std::vector<std::string> nameProblems;
        for (std::size_t a = 0; a < all.size(); ++a) {
            for (std::size_t b = a + 1; b < all.size(); ++b) {
                if (std::strcmp(numos::ctxSlug(all[a]), numos::ctxSlug(all[b])) == 0) {
                    slugProblems.push_back(numos::ctxSlug(all[a]));
                }
                if (std::strcmp(numos::ctxName(all[a]), numos::ctxName(all[b])) == 0) {
                    nameProblems.push_back(numos::ctxName(all[a]));
                }
            }
        }
        require(slugProblems.empty(), "context slugs are unique");
        require(nameProblems.empty(), "context display names are unique");
    }

    for (Ctx ctx : all) {
        const numos::ContextMap* map = numos::contextFor(ctx);
        if (!map) continue;
        const std::string slug = numos::ctxSlug(ctx);

        int primaries = 0;
        for (uint8_t index = 0; index < map->count; ++index) {
            if (map->keys[index].role == Role::Primary) ++primaries;
        }
        require(primaries <= kSoftKeySlots,
                slug + " fits the " + std::to_string(kSoftKeySlots) +
                    " soft-key slots (has " + std::to_string(primaries) + ")");

        // No key listed twice: the second entry would win silently in
        // ctxRoleFor() and the legend would disagree with the role.
        bool duplicated = false;
        for (uint8_t a = 0; a < map->count && !duplicated; ++a) {
            for (uint8_t b = static_cast<uint8_t>(a + 1); b < map->count; ++b) {
                if (map->keys[a].key == map->keys[b].key) {
                    duplicated = true;
                    break;
                }
            }
        }
        require(!duplicated, slug + " lists each key once");

        // The universal escapes survive even where the table forgets them.
        bool escapesAlive = true;
        for (KeyCode escape : {KeyCode::HOME, KeyCode::MODE, KeyCode::ON}) {
            if (numos::ctxRoleFor(ctx, escape) == Role::Disabled) escapesAlive = false;
        }
        require(escapesAlive, slug + " keeps an escape route alive");
    }

    // The clearest demonstrations of the feature, pinned so a table edit that
    // breaks them is a test failure rather than a surprise in the browser.
    {
        const Ctx gb = Ctx::GameBoy;
        require(numos::ctxRoleFor(gb, KeyCode::ENTER) == Role::Primary,
                "Game Boy: ENTER is the A button (primary)");
        bool deadMaths = true;
        for (KeyCode math : {KeyCode::NUM_7, KeyCode::SIN, KeyCode::SQRT}) {
            if (numos::ctxRoleFor(gb, math) != Role::Disabled) deadMaths = false;
        }
        require(deadMaths, "Game Boy: the maths keypad is dead");
    }
    {
        // The reverse case: the graph-only keys are meaningless in the
        // calculator, which is the whole point of a relevance table.
        bool deadGraph = true;
        for (KeyCode graph : {KeyCode::GRAPH, KeyCode::ZOOM, KeyCode::TRACE,
                              KeyCode::TABLE}) {
            if (numos::ctxRoleFor(Ctx::Calculation, graph) != Role::Disabled) {
                deadGraph = false;
            }
        }
        require(deadGraph, "Calculation: graph-only keys are disabled");
    }
    {
        require(numos::ctxRoleFor(Ctx::Ai, KeyCode::NUM_7) == Role::Primary,
                "AI: NUM_7 is the verification trigger, so it is primary");
        require(numos::ctxRoleFor(Ctx::Calculation, KeyCode::NUM_7) ==
                    Role::Secondary,
                "AI: NUM_7 is not primary in the calculator");
    }

    // An unlisted key in an unlisted context must not throw or light up.
    require(numos::ctxRoleFor(Ctx::Splash, KeyCode::ENTER) == Role::Disabled,
            "Splash: ENTER does nothing");

    std::printf(failures == 0 ? "\nkey_context_test: PASS\n"
                              : "\nkey_context_test: %d FAILURE(S)\n",
                failures);
    return failures == 0 ? 0 : 1;
}
