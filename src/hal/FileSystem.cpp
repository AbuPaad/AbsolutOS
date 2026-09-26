/*
 * NeoCalculator - NumOS
 * Copyright (C) 2026 Juan Ramon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/**
 * FileSystem.cpp — Implementación del wrapper LittleFS para PC
 *
 * Simula LittleFS creando archivos en ./emulator_data/ usando
 * funciones estándar de C (fopen, fwrite, fread).
 *
 * Solo se compila cuando NATIVE_SIM está definido.
 */

#ifndef ARDUINO

#include "FileSystem.h"
#include <cstdio>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>

EM_JS(void, numosFilesystemDidMutate, (int operation), {
    const callback = Module['numosPersistenceDirty'];
    if (typeof callback === 'function') callback(operation);
});
#endif

#ifdef _WIN32
    #include <direct.h>
    #define MKDIR_P(path) ::_mkdir(path)
#else
    #include <sys/stat.h>
    #define MKDIR_P(path) ::mkdir(path, 0755)
#endif

// Listado de directorios: std::filesystem evita #ifdefs por plataforma
// (opendir/readdir en POSIX vs _findfirst/_findnext en Windows/MinGW).
#include <filesystem>
#include <system_error>

// Raíz configurable (FIX-01): por defecto el comportamiento histórico
// ./emulator_data (relativo al CWD); NativeHal puede redirigirla a un
// sandbox por-ejecución ANTES de begin().
static std::string s_rootDir = "./emulator_data";

// ── Instancia global ────────────────────────────────────────────────────────
LittleFSClass LittleFS;

// ── setRoot / root — raíz configurable del filesystem emulado ───────────────
void LittleFSClass::setRoot(const char* root) {
    if (root && root[0]) s_rootDir = root;
}

const char* LittleFSClass::root() {
    return s_rootDir.c_str();
}

// ── fullPath — Convierte ruta LittleFS a ruta local del PC ──────────────────
std::string LittleFSClass::fullPath(const char* path) const {
    std::string p = s_rootDir;
    if (path && path[0] != '/' && path[0] != '\\') p += '/';
    if (path) p += path;
    return p;
}

// ── begin — Crea el directorio raíz si no existe ────────────────────────────
bool LittleFSClass::begin(bool /*formatOnFail*/) {
    MKDIR_P(s_rootDir.c_str());
    _initialized = true;
    return true;
}

// ── exists — Verifica si el archivo existe ──────────────────────────────────
bool LittleFSClass::exists(const char* path) {
    std::string fp = fullPath(path);
    FILE* f = std::fopen(fp.c_str(), "rb");
    if (f) {
        std::fclose(f);
        return true;
    }
    return false;
}

// ── open — Abre o crea un archivo ───────────────────────────────────────────
File LittleFSClass::open(const char* path, const char* mode) {
    if (!_initialized) return File();

    // Forzar modo binario (LittleFS siempre es binario)
    std::string bmode = mode ? mode : "r";
    if (bmode.find('b') == std::string::npos) {
        bmode += 'b';
    }

    std::string fp = fullPath(path);
    const bool mutating = bmode.find('w') != std::string::npos ||
                          bmode.find('a') != std::string::npos ||
                          bmode.find('+') != std::string::npos;

    // Un directorio abierto en modo lectura se devuelve como File de directorio,
    // con paridad Arduino: LittleFS.open("/dir") + openNextFile(). En modo de
    // escritura se deja caer al fopen() normal, que fallará (como en Arduino).
    return File::fromHostPath(fp, s_rootDir, bmode.c_str(), !mutating);
}

// ── remove — Elimina un archivo ─────────────────────────────────────────────
bool LittleFSClass::remove(const char* path) {
    std::string fp = fullPath(path);
    const bool removed = std::remove(fp.c_str()) == 0;
#ifdef __EMSCRIPTEN__
    if (removed) numosFilesystemDidMutate(2);
#endif
    return removed;
}

// ── rename — Renombra dentro de la raíz emulada ─────────────────────────────
bool LittleFSClass::rename(const char* from, const char* to) {
    const std::string source = fullPath(from);
    const std::string destination = fullPath(to);
    const bool renamed = std::rename(source.c_str(), destination.c_str()) == 0;
#ifdef __EMSCRIPTEN__
    if (renamed) numosFilesystemDidMutate(4);
#endif
    return renamed;
}

// ── mkdir — Crea un directorio dentro de la raíz emulada ────────────────────
bool LittleFSClass::mkdir(const char* path) {
    const std::string fp = fullPath(path);
    const bool created = MKDIR_P(fp.c_str()) == 0;
#ifdef __EMSCRIPTEN__
    if (created) numosFilesystemDidMutate(3);
#endif
    return created;
}

// ── emulatedPathOf — Ruta host → ruta dentro del FS emulado ─────────────────
// "./emulator_data/roms/x.gb" con raíz "./emulator_data" → "/roms/x.gb"
static std::string emulatedPathOf(const std::string& hostPath, const std::string& root) {
    std::string base = root;
    while (base.size() > 1 && base.back() == '/') base.pop_back();

    std::string p = hostPath;
    if (!base.empty() && p.compare(0, base.size(), base) == 0) p.erase(0, base.size());
    if (p.empty()) return "/";
    if (p[0] != '/') p.insert(p.begin(), '/');
    return p;
}

// ── File::fromHostPath — Fábrica de File para una ruta del host ─────────────
File File::fromHostPath(const std::string& hostPath, const std::string& root,
                        const char* mode, bool allowDirectory) {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (allowDirectory && fs::is_directory(hostPath, ec) && !ec) {
        auto* it = new fs::directory_iterator(hostPath, ec);
        if (ec) {
            delete it;
            return File();
        }
        File entry;
        entry._dir         = it;
        entry._isDir       = true;
        entry._dirHostPath = hostPath;
        entry._root        = root;
        entry._emulatedPath = emulatedPathOf(hostPath, root);
        entry._name         = fs::path(hostPath).filename().string();
        return entry;
    }

    FILE* f = std::fopen(hostPath.c_str(), mode && mode[0] ? mode : "rb");
    if (!f) return File();

    const bool mutating = mode && (std::strchr(mode, 'w') || std::strchr(mode, 'a') ||
                                   std::strchr(mode, '+'));
    File entry(f, mutating);
    entry._root         = root;
    entry._emulatedPath = emulatedPathOf(hostPath, root);
    entry._name         = fs::path(hostPath).filename().string();
    return entry;
}

// ── File::destroyDirectoryHandle — libera el iterador sin exponer su tipo ───
void File::destroyDirectoryHandle(void* handle) {
    delete static_cast<std::filesystem::directory_iterator*>(handle);
}

// ── File::openNextFile — siguiente entrada del directorio ───────────────────
File File::openNextFile() {
    if (!_dir) return File();

    namespace fs = std::filesystem;
    auto* it = static_cast<fs::directory_iterator*>(_dir);
    const fs::directory_iterator end;
    std::error_code ec;

    while (*it != end) {
        const fs::directory_entry entry = **it;
        it->increment(ec);
        if (ec) return File();

        const std::string base = entry.path().filename().string();
        if (base.empty() || base == "." || base == "..") continue;
        return File::fromHostPath(entry.path().string(), _root, "rb", true);
    }
    return File();
}

// ── isDirectory — ¿la ruta emulada es un directorio? ────────────────────────
bool LittleFSClass::isDirectory(const char* path) {
    std::error_code ec;
    return std::filesystem::is_directory(fullPath(path), ec) && !ec;
}

// ── hostPath — ruta del host correspondiente (diagnóstico) ──────────────────
std::string LittleFSClass::hostPath(const char* path) const {
    return fullPath(path);
}

#endif // !ARDUINO
