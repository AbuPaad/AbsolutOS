/*
 * Host test for the native filesystem wrapper's directory listing — the API the
 * Game Boy app uses to scan /roms. No LVGL, no SDL, no emulator.
 *
 * Build (from the repository root):
 *   g++ -std=gnu++17 -O2 -Wall -Wextra -I src \
 *       tests/host/fs_listing_test.cpp src/hal/FileSystem.cpp -o fs_listing_test
 *   ./fs_listing_test /path/to/scratch/dir
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>

#include "hal/FileSystem.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? " PASS" : " FAIL", what);
    if (!ok) ++g_failures;
}

void writeFile(const char* path, const char* data, size_t len) {
    File f = LittleFS.open(path, "w");
    if (f) f.write(reinterpret_cast<const uint8_t*>(data), len);
}

}  // namespace

int main(int argc, char** argv) {
    const char* root = (argc > 1) ? argv[1] : "./emulator_data_fs_test";

    // Start from a clean slate so the test is repeatable.
    std::string listing = std::string(root) + "/roms";
    std::string sub     = std::string(root) + "/roms/sub";
    std::remove((sub + "/c.gb").c_str());
    std::remove((listing + "/a.gb").c_str());
    std::remove((listing + "/b.gb").c_str());
    std::filesystem::remove(sub);
    std::filesystem::remove(listing);
    std::filesystem::remove(root);

    LittleFSClass::setRoot(root);
    check(std::strcmp(LittleFSClass::root(), root) == 0, "root() reflects setRoot()");
    check(LittleFS.begin(), "begin() succeeds");

    check(LittleFS.mkdir("/roms"), "mkdir(\"/roms\")");
    check(LittleFS.mkdir("/roms/sub"), "mkdir(\"/roms/sub\") (no recursion needed)");
    check(!LittleFS.mkdir("/roms/sub"), "a second mkdir on the same path fails");
    writeFile("/roms/a.gb", "AAAAA", 5);
    writeFile("/roms/b.gb", "BBB", 3);
    writeFile("/roms/sub/c.gb", "C", 1);

    check(LittleFS.exists("/roms/a.gb"), "exists() sees a written file");
    check(LittleFS.isDirectory("/roms"), "isDirectory(\"/roms\")");
    check(!LittleFS.isDirectory("/roms/a.gb"), "a regular file is not a directory");
    check(!LittleFS.isDirectory("/nope"), "a missing path is not a directory");

    // ── the actual scan loop the app performs ──────────────────────────────
    File dir = LittleFS.open("/roms", "r");
    check(static_cast<bool>(dir), "open(\"/roms\", \"r\") yields a handle");
    check(dir.isDirectory(), "that handle reports isDirectory()");
    check(std::strcmp(dir.name(), "roms") == 0,
          "a directory handle reports its base name (Arduino parity)");

    std::set<std::string> files;
    std::set<std::string> dirs;
    size_t entryCount = 0;
    while (File entry = dir.openNextFile()) {
        ++entryCount;
        check(std::strcmp(entry.name(), "") != 0, "each entry has a base name");
        check(entry.path()[0] == '/', "each entry exposes a root-relative path");
        if (entry.isDirectory()) dirs.insert(entry.path());
        else                     files.insert(entry.path());
    }
    std::printf("[INFO] scanned %zu entries: %zu files, %zu dirs\n",
                entryCount, files.size(), dirs.size());

    check(entryCount == 3, "the scan sees exactly three entries");
    check(files.count("/roms/a.gb") == 1, "a.gb is listed");
    check(files.count("/roms/b.gb") == 1, "b.gb is listed");
    check(files.count("/roms/sub") == 0, "a subdirectory is not listed as a file");
    check(dirs.count("/roms/sub") == 1, "the subdirectory is listed and flagged");

    // ── metadata used for a picker row ─────────────────────────────────────
    File a = LittleFS.open("/roms/a.gb", "r");
    check(static_cast<bool>(a), "a listed file can be opened by its emulated path");
    check(std::strcmp(a.name(), "a.gb") == 0, "file name() is the base name");
    check(std::strcmp(a.path(), "/roms/a.gb") == 0, "file path() is root-relative");
    check(a.size() == 5, "file size() is correct");
    char buf[8] = {0};
    const size_t got = a.read(reinterpret_cast<uint8_t*>(buf), 5);
    check(got == 5 && std::strncmp(buf, "AAAAA", 5) == 0, "file contents round-trip");
    a.close();

    // ── failure shapes the app must handle ─────────────────────────────────
    File missing = LittleFS.open("/roms/nope.gb", "r");
    check(!missing, "opening a missing file yields a falsy handle");
    File missingDir = LittleFS.open("/nope", "r");
    check(!missingDir, "opening a missing directory yields a falsy handle");
    File emptyDir = LittleFS.open("/roms/sub", "r");
    check(emptyDir.isDirectory(), "a subdirectory opens as a directory");
    File first = emptyDir.openNextFile();
    check(static_cast<bool>(first) && std::strcmp(first.name(), "c.gb") == 0,
          "nested scan finds c.gb");
    File past = emptyDir.openNextFile();
    check(!past, "past the end openNextFile() returns a falsy handle");

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
