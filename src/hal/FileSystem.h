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
 * FileSystem.h — Wrapper LittleFS-compatible para builds nativos (PC)
 *
 * En modo NATIVE_SIM provee las clases File y LittleFSClass que emulan
 * la API de LittleFS del framework Arduino, usando el sistema de archivos
 * local del PC con carpeta base ./emulator_data/.
 *
 * Uso en código que ya usa LittleFS:
 *   #ifdef ARDUINO
 *   #include <FS.h>
 *   #include <LittleFS.h>
 *   #else
 *   #include "hal/FileSystem.h"
 *   #endif
 *
 * El código de serialización (File::write, File::read, LittleFS.open, etc.)
 * funciona sin cambios en ambas plataformas.
 *
 * ── Listado de directorios (paridad con Arduino) ────────────────────────────
 * El wrapper nativo no tenía forma de enumerar un directorio, así que el código
 * de app que necesita "recorrer /roms y ver qué hay" solo podía compilar en
 * firmware. Ahora File expone la misma forma que Arduino:
 *
 *   File dir = LittleFS.open("/roms", "r");
 *   if (dir && dir.isDirectory()) {
 *       while (File entry = dir.openNextFile()) {
 *           if (entry.isDirectory()) continue;
 *           const char* base = entry.name();   // "x.gb"
 *           const char* path = entry.path();   // "/roms/x.gb" (ruta emulada)
 *           size_t bytes     = entry.size();
 *       }
 *   }
 *
 * En firmware esa MISMA secuencia la sirve LittleFS/FS del framework (File de
 * ESP32 ya implementa openNextFile/isDirectory/name); aquí solo se implementa
 * el equivalente nativo. La implementación nativa usa std::filesystem, que
 * evita #ifdefs por plataforma (POSIX opendir vs _findfirst de Windows).
 */

#pragma once

#ifndef ARDUINO   // ═══ Solo activo en builds no-Arduino ═══

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

#ifdef __EMSCRIPTEN__
/**
 * Notify the private browser persistence controller after a successful
 * filesystem mutation. The implementation calls a Module-private callback;
 * it never exposes Emscripten FS or Wasm memory on window.numos.
 */
extern "C" void numosFilesystemDidMutate(int operation);
#endif

// ════════════════════════════════════════════════════════════════════════════
// File — Wrapper sobre FILE* compatible con Arduino File
// ════════════════════════════════════════════════════════════════════════════
class File {
public:
    File() : _fp(nullptr), _mutating(false), _dir(nullptr), _isDir(false) {}
    File(FILE* fp, bool mutating = false)
        : _fp(fp), _mutating(mutating), _dir(nullptr), _isDir(false) {}
    ~File()        { close(); }

    // Move semantics (Arduino File es copiable, pero para native mover es suficiente)
    File(File&& other) noexcept
        : _fp(other._fp), _mutating(other._mutating), _dir(other._dir),
          _isDir(other._isDir), _dirHostPath(other._dirHostPath),
          _root(other._root), _name(other._name),
          _emulatedPath(other._emulatedPath) {
        other._fp = nullptr;
        other._mutating = false;
        other._dir = nullptr;
        other._isDir = false;
        other._dirHostPath.clear();
        other._root.clear();
        other._name.clear();
        other._emulatedPath.clear();
    }
    File& operator=(File&& other) noexcept {
        if (this != &other) {
            close();
            _fp = other._fp;
            _mutating = other._mutating;
            _dir = other._dir;
            _isDir = other._isDir;
            _dirHostPath = other._dirHostPath;
            _root = other._root;
            _name = other._name;
            _emulatedPath = other._emulatedPath;
            other._fp = nullptr;
            other._mutating = false;
            other._dir = nullptr;
            other._isDir = false;
            other._dirHostPath.clear();
            other._root.clear();
            other._name.clear();
            other._emulatedPath.clear();
        }
        return *this;
    }

    explicit operator bool() const { return _fp != nullptr || _dir != nullptr; }

    void close() {
        if (_dir) {
            // The iterator object is owned by this File; its type is only known
            // in the .cpp (std::filesystem is deliberately not included here).
            destroyDirectoryHandle(_dir);
            _dir = nullptr;
        }
        _isDir = false;
        _dirHostPath.clear();
        _name.clear();
        _emulatedPath.clear();
        if (_fp) {
            const bool notify = _mutating && std::fclose(_fp) == 0;
            _fp = nullptr;
            _mutating = false;
#ifdef __EMSCRIPTEN__
            if (notify) numosFilesystemDidMutate(1);  // write/close
#else
            (void)notify;
#endif
        }
    }

    size_t write(const uint8_t* buf, size_t len) {
        if (!_fp) return 0;
        return std::fwrite(buf, 1, len, _fp);
    }

    /// Lee hasta len bytes en buf.  Devuelve bytes leídos.
    size_t read(uint8_t* buf, size_t len) {
        if (!_fp) return 0;
        return std::fread(buf, 1, len, _fp);
    }

    /// Lectura de un byte compatible con Arduino File::read().
    int read() {
        if (!_fp) return -1;
        return std::fgetc(_fp);
    }

    /// Tamaño total del archivo (seek al final y volver). 0 para directorios.
    size_t size() {
        if (!_fp) return 0;
        long cur = std::ftell(_fp);
        std::fseek(_fp, 0, SEEK_END);
        long sz = std::ftell(_fp);
        std::fseek(_fp, cur, SEEK_SET);
        return (size_t)(sz > 0 ? sz : 0);
    }

    /// Posición actual del cursor
    size_t position() {
        if (!_fp) return 0;
        long p = std::ftell(_fp);
        return (size_t)(p > 0 ? p : 0);
    }

    size_t available() {
        return size() - position();
    }

    void seek(size_t pos) {
        if (_fp) std::fseek(_fp, (long)pos, SEEK_SET);
    }

    // ── Listado de directorios (paridad con Arduino File) ───────────────────

    /** True cuando este File representa un directorio abierto. */
    bool isDirectory() const { return _isDir; }

    /**
     * Nombre base de la entrada ("rom.gb", o el nombre de la carpeta si
     * isDirectory()). Igual que File::name() de Arduino/ESP32.
     */
    const char* name() const { return _name.c_str(); }

    /** Ruta dentro del sistema de archivos emulado ("/roms/rom.gb"). */
    const char* path() const { return _emulatedPath.c_str(); }

    /**
     * Siguiente entrada del directorio. Devuelve un File vacío (falsy) al final.
     * Solo válido sobre un File de directorio.
     */
    File openNextFile();

    /**
     * Fábrica interna: construye el File nativo adecuado (archivo o directorio)
     * para una ruta del host. Usada por LittleFSClass::open y openNextFile.
     */
    static File fromHostPath(const std::string& hostPath, const std::string& root,
                             const char* mode = "rb", bool allowDirectory = false);

private:
    FILE*       _fp;
    bool        _mutating;
    void*       _dir;              ///< std::filesystem::directory_iterator*
    bool        _isDir;
    std::string _dirHostPath;      ///< host path of an opened directory
    std::string _root;             ///< emulated FS root this entry lives under
    std::string _name;             ///< base name of this entry
    std::string _emulatedPath;     ///< root-relative path of this entry

    /// Defined in the .cpp: deletes the iterator without exposing its type.
    static void destroyDirectoryHandle(void* handle);

    // No copyable
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};

// ════════════════════════════════════════════════════════════════════════════
// LittleFSClass — Emulación de LittleFS que usa ./emulator_data/
// ════════════════════════════════════════════════════════════════════════════
class LittleFSClass {
public:
    /**
     * Fija el directorio raíz del sistema de archivos emulado (FIX-01).
     * Debe llamarse ANTES de begin(); por defecto "./emulator_data".
     * Permite a NativeHal apuntar la persistencia a un sandbox por-ejecución
     * (runs deterministas/CI) sin tocar el árbol del repositorio.
     */
    static void setRoot(const char* root);

    /** Directorio raíz actualmente configurado. */
    static const char* root();

    /**
     * Inicializa el "sistema de archivos" (crea el directorio base).
     * @param formatOnFail  Ignorado en PC (siempre crea el directorio).
     */
    bool begin(bool formatOnFail = false);

    /** Verifica si un archivo existe. */
    bool exists(const char* path);

    /**
     * Abre un archivo.
     * @param path  Ruta absoluta (ej: "/vars.dat")
     * @param mode  "r" (lectura) o "w" (escritura/creación)
     *
     * Si @p path es un directorio y el modo es de lectura, devuelve un File de
     * directorio listo para openNextFile().
     */
    File open(const char* path, const char* mode);

    /** Elimina un archivo. */
    bool remove(const char* path);

    /** Renombra un archivo dentro de la raíz emulada. */
    bool rename(const char* from, const char* to);

    /** Crea un directorio dentro de la raíz emulada. */
    bool mkdir(const char* path);

    /** True si la ruta existe y es un directorio. */
    bool isDirectory(const char* path);

    /** Ruta del host para una ruta emulada (diagnóstico). */
    std::string hostPath(const char* path) const;

private:
    std::string fullPath(const char* path) const;
    bool _initialized = false;
};

// ── Instancia global (como en Arduino) ──
extern LittleFSClass LittleFS;

#endif // !ARDUINO
