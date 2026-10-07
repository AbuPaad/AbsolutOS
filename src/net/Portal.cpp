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
 * net/Portal.cpp — the Arduino half of net/Portal.h.
 *
 * Shape of the implementation, and why:
 *
 *   * OWN TASK, NEVER THE LOOP. WebServer::handleClient() blocks on a socket
 *     read; the loop task also owns the keypad scan, lv_timer_handler() and the
 *     app update ticks. So the server gets its own FreeRTOS task pinned to core
 *     0 (where the Wi-Fi stack lives) and the loop only ever calls tick().
 *   * LittleFS AND Preferences ARE TOUCHED FROM THAT TASK. The AI app and the
 *     notes app read the same filesystem from the loop task, so every mutation
 *     the portal performs holds one mutex; reads use the same guard. Cheaper and
 *     more obviously right than pretending the two tasks cannot collide.
 *   * THE PAGE IS IN THE BINARY, NOT ON THE FILESYSTEM. The portal's whole job
 *     is to work on a device whose filesystem is empty and which has no
 *     internet, so it cannot fetch assets and cannot rely on files it is
 *     supposed to be creating. One self-contained page, no CDN, no webfonts.
 *   * CONFIG GOES WHERE THE READERS ALREADY LOOK. The API key goes to NVS
 *     (namespace "numos-ai", key "api_key" — the exact key AiConfig::load()
 *     reads) so it never lands in a file the browser can download. Host, model
 *     and transport go into /ai/config.json, because those are data the file
 *     editor is also allowed to touch — and writing transport="device" is
 *     load-bearing: the compiled default is "replay", which makes a fresh
 *     device silently answer from the recorded fixture instead of the network.
 *   * CAPTIVE, NOT JUST A SERVER. With no internet on the AP, a phone shows
 *     "no internet" and may refuse to use the Wi-Fi. A DNS responder answers
 *     every name with our own IP and every unknown HTTP path 302s to the page,
 *     which is what makes the OS raise its sign-in sheet.
 */

#include "net/Portal.h"

#if defined(ARDUINO)

#include <Arduino.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ai/AiClient.h"        // AiConfig (wa_appid)
#include "ai/WolframClient.h"   // buildWolframUrl, classifyWolfram, parseWolframBody
#include "net/HttpStream.h"   // the AppID test: one real GET, same seam as the app
#include "net/Wifi.h"

namespace net {
namespace {

// ── tuning ──────────────────────────────────────────────────────────────────
constexpr uint16_t kPort          = 80;
constexpr size_t   kTaskStack     = 10240;
constexpr UBaseType_t kTaskPrio   = 1;
constexpr BaseType_t  kTaskCore   = 0;      ///< the Wi-Fi stack's core
constexpr size_t   kMaxTextBytes  = 192u * 1024u;   ///< one file, in the editor
constexpr size_t   kMaxUploadBytes = 8u * 1024u * 1024u;
constexpr size_t   kMaxPathChars  = 128;
constexpr uint32_t kQuitWaitMs    = 3000;

// Pre-scan / auto-join, run BEFORE the AP is raised. Both are bounded so a
// portal press never feels like a hang: the Wi-Fi screen shows the activity and
// the user can cancel by pressing the toggle again.
constexpr uint32_t kPreScanMs   = 4000;   ///< cap on "which saved nets are here?"
constexpr uint32_t kJoinMs      = 10000;  ///< budget per saved network before the next
constexpr int      kMinJoinRssi = -85;    ///< ignore sightings weaker than this

// The AP + web server need internal (DMA-capable) DRAM, but the Wi-Fi driver
// allocates it in several smaller blocks, so the meaningful guard is TOTAL free
// internal heap — not the single largest contiguous free block.
//
// The floor is deliberately LOW. An ASSOCIATED STA already holds its lwIP/TLS
// buffers, so a unit that has joined a network has materially less internal DRAM
// free than a blank one — and a 24 KB floor REFUSED the portal in exactly that
// state ("it will not open once you are on a network"). softAP() reports a real
// failure, so this is only a courtesy guard against starting on a hopeless heap;
// it must not be the thing that blocks a board whose Wi-Fi path is otherwise
// fine.
constexpr size_t   kMinInternalFree = 10u * 1024u;

// ── state ───────────────────────────────────────────────────────────────────
WebServer  g_server(kPort);
DNSServer  g_dns;
TaskHandle_t g_task      = nullptr;
SemaphoreHandle_t g_fsMutex = nullptr;
volatile bool g_quit      = false;
volatile bool g_taskDone  = true;
volatile uint32_t g_requests = 0;
uint32_t   g_lastReqMs   = 0;
bool       g_running     = false;
char       g_apSsid[33]  = {0};
char       g_apPass[17]  = {0};
std::string g_lastError;

// ── pre-scan / auto-join state (main loop task only) ────────────────────────
PortalPhase g_phase       = PortalPhase::Off;
uint32_t    g_phaseStartMs = 0;
std::string g_activity;        ///< "scanning", "joining <ssid>"
int         g_scanned      = 0;///< SSIDs the pre-scan saw

/// Saved networks that the pre-scan actually saw, strongest first.
struct JoinCandidate { std::string ssid; std::string pass; int rssi; };
std::vector<JoinCandidate> g_candidates;
size_t      g_candidateIndex = 0;
std::string g_uploadPath;      ///< target of the in-flight multipart upload
std::string g_uploadError;     ///< named reason a multipart upload failed
File        g_uploadFile;      ///< kept OPEN for the whole upload (see handleUploadWrite)
size_t     g_uploadBytes = 0;

// ── small helpers ───────────────────────────────────────────────────────────

/** Per-unit AP credentials from the chip id: unique, no shared factory secret. */
void makeCredentials() {
    const uint64_t mac = ESP.getEfuseMac();
    const uint32_t lo  = static_cast<uint32_t>(mac & 0xFFFFFFFFu);
    const uint32_t hi  = static_cast<uint32_t>(mac >> 32);
    const uint16_t tag = static_cast<uint16_t>((lo >> 8) ^ hi);
    std::snprintf(g_apSsid, sizeof(g_apSsid), "NumOS-%04X", static_cast<unsigned>(tag));

    // 8 digits: WPA2 demands at least 8 characters, and this one is read off the
    // calculator screen by the user, so decimal beats hex for typability.
    uint32_t v = (lo ^ (hi * 2654435761u)) ^ (lo >> 13);
    v %= 100000000u;
    std::snprintf(g_apPass, sizeof(g_apPass), "%08u", static_cast<unsigned>(v));
}

struct FsLock {
    FsLock()  { if (g_fsMutex) xSemaphoreTake(g_fsMutex, portMAX_DELAY); }
    ~FsLock() { if (g_fsMutex) xSemaphoreGive(g_fsMutex); }
};

std::string jsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) out += ' ';
                else out += c;
        }
    }
    return out;
}

/**
 * Accept only absolute, in-tree, sane paths. The portal exposes the whole
 * LittleFS root on purpose, but "..", control bytes and absurd lengths are
 * rejected rather than sanitised: a rejected path is a visible 400, a
 * sanitised one is a silent surprise.
 */
bool safePath(const char* raw, std::string& out) {
    if (!raw || !*raw) return false;
    std::string p(raw);
    if (p.size() > kMaxPathChars) return false;
    if (p[0] != '/') return false;
    if (p.find("..") != std::string::npos) return false;
    for (char c : p) {
        if (static_cast<unsigned char>(c) < 0x20 || c == '\\') return false;
    }
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    out = p;
    return true;
}

std::string parentOf(const std::string& p) {
    if (p.size() <= 1) return "/";
    const size_t slash = p.find_last_of('/');
    if (slash == 0 || slash == std::string::npos) return "/";
    return p.substr(0, slash);
}

const char* mimeFor(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = name.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (ext == "md" || ext == "txt" || ext == "json" || ext == "csv") return "text/plain";
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "js")  return "application/javascript";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gb" || ext == "gbc") return "application/octet-stream";
    return "application/octet-stream";
}

bool readTextCapped(const std::string& path, std::string& out) {
    out.clear();
    File f = LittleFS.open(path.c_str(), "r");
    if (!f || f.isDirectory()) return false;
    char buf[512];
    while (true) {
        const int n = f.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf));
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
        if (out.size() > kMaxTextBytes) { out.resize(kMaxTextBytes); break; }
    }
    f.close();
    return true;
}

/** temp + rename, so a dropped connection can never leave a half-written note. */
bool writeTextAtomic(const std::string& path, const std::string& body) {
    const std::string tmp = path + ".tmp";
    File f = LittleFS.open(tmp.c_str(), "w");
    if (!f) return false;
    const size_t n = f.write(reinterpret_cast<const uint8_t*>(body.data()), body.size());
    f.close();
    if (n != body.size()) { LittleFS.remove(tmp.c_str()); return false; }
    LittleFS.remove(path.c_str());
    return LittleFS.rename(tmp.c_str(), path.c_str());
}

bool ensureDir(const std::string& path) {
    File probe = LittleFS.open(path.c_str(), "r");
    if (probe && probe.isDirectory()) { probe.close(); return true; }
    if (probe) probe.close();
    return LittleFS.mkdir(path.c_str());
}

// ── flat JSON in / out (the config surface is ours, so it stays flat) ───────

bool jsonFindString(const std::string& js, const char* key, std::string& out) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t at = js.find(needle);
    if (at == std::string::npos) return false;
    at = js.find(':', at + needle.size());
    if (at == std::string::npos) return false;
    ++at;
    while (at < js.size() && isspace(static_cast<unsigned char>(js[at]))) ++at;
    if (at >= js.size() || js[at] != '"') return false;
    ++at;
    std::string v;
    while (at < js.size() && js[at] != '"') {
        if (js[at] == '\\' && at + 1 < js.size()) ++at;
        v += js[at++];
    }
    out = v;
    return true;
}

/**
 * Set (or insert) one flat "key": "value" pair, rewriting the file only if the
 * value actually changed. /ai/config.json is hand-editable in the file browser,
 * so this must not reformat someone's file beyond the one field.
 */
bool upsertJsonString(const std::string& path, const char* key, const std::string& value) {
    std::string js;
    if (!readTextCapped(path, js)) js = "{\n}\n";

    const std::string pair = std::string("\"") + key + "\": \"" + jsonEscape(value) + "\"";
    const std::string needle = std::string("\"") + key + "\"";
    const size_t at = js.find(needle);

    if (at == std::string::npos) {
        const size_t close = js.find_last_of('}');
        if (close == std::string::npos) {
            js = "{\n  " + pair + "\n}\n";
        } else {
            const bool hasAny = js.find('"') != std::string::npos;
            js.insert(close, std::string(hasAny ? ",\n  " : "\n  ") + pair + "\n");
        }
    } else {
        const size_t colon = js.find(':', at + needle.size());
        if (colon == std::string::npos) return false;
        size_t end = colon + 1;
        while (end < js.size() && isspace(static_cast<unsigned char>(js[end]))) ++end;
        if (end < js.size() && js[end] == '"') {
            ++end;
            while (end < js.size() && js[end] != '"') {
                if (js[end] == '\\' && end + 1 < js.size()) ++end;
                ++end;
            }
            if (end < js.size()) ++end;          // include the closing quote
        } else {
            while (end < js.size() && js[end] != ',' && js[end] != '}' && js[end] != '\n') ++end;
        }
        js.replace(colon + 1, end - (colon + 1), " " + std::string("\"") + jsonEscape(value) + "\"");
    }
    return writeTextAtomic(path, js);
}

// ── request bookkeeping ─────────────────────────────────────────────────────

void touch() {
    g_requests++;
    g_lastReqMs = millis();
}

void sendJson(int code, const std::string& body) {
    touch();
    g_server.sendHeader("Cache-Control", "no-store");
    g_server.send(code, "application/json; charset=utf-8", body.c_str());
}

void sendError(int code, const std::string& msg) {
    sendJson(code, std::string("{\"ok\":false,\"error\":\"") + jsonEscape(msg) + "\"}");
}

// ── the page ────────────────────────────────────────────────────────────────
//
// Self-contained on purpose (no CDN: the phone has no internet on this AP).
// The Markdown subset matches what the device renderer accepts — headings,
// paragraphs, lists, bold/italic, inline + fenced code, quotes, and a line of
// exactly "---" which this project uses as an explicit page break.

const char kPage[] PROGMEM = R"PAGE(<!doctype html><html lang="en" data-theme="dark"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>NumOS</title><style>
:root{--mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
--bg:#0f1110;--bg-alt:#161917;--card:#1c201d;--text:#e3e8e1;--muted:#949e91;
--brass:#d8b15d;--brass-bg:#292213;--brass-bd:#4a3b1c;--border:#262d27;--border-l:#353d36;
--green:#42b857;--err:#c0392b}
[data-theme=light]{--bg:#c7d3c0;--bg-alt:#b9c7b2;--card:#d1dccb;--text:#121413;--muted:#3b423a;
--brass:#96742a;--brass-bg:#f3e8d2;--brass-bd:#d1b679;--border:#121413;--border-l:#3b423a;
--green:#1b6e2d;--err:#8f2718}
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:var(--mono);font-size:15px;line-height:1.55;
-webkit-text-size-adjust:100%}
a{color:inherit}
header{position:sticky;top:0;z-index:10;background:var(--bg);border-bottom:1px solid var(--border)}
.bar{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
header .bar{max-width:980px;margin:0 auto;padding:11px 16px}
header nav.bar{padding-top:0}
.brand{display:flex;align-items:center;gap:9px;flex:1;min-width:0}
.wordmark{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-size:15px;font-weight:700;
letter-spacing:.02em}
.tag{font-size:10px;letter-spacing:.22em;text-transform:uppercase;color:var(--brass);
background:var(--brass-bg);border:1px solid var(--brass-bd);padding:2px 6px;flex:none}
a.tab{font-size:11px;letter-spacing:.16em;text-transform:uppercase;color:var(--muted);
text-decoration:none;padding:7px 10px;border:1px solid transparent;cursor:pointer}
a.tab:hover{color:var(--text);border-color:var(--border-l)}
#net{font-size:11px;color:var(--muted);margin-left:auto}
main{padding:16px;max-width:980px;margin:0 auto}
button,a.btn{font:inherit;font-size:12px;background:var(--bg-alt);color:var(--text);
border:1px solid var(--border-l);padding:7px 11px;cursor:pointer;text-decoration:none;white-space:nowrap}
button:hover,a.btn:hover{border-color:var(--brass)}
button.p{border-color:var(--brass);color:var(--brass);background:var(--brass-bg)}
button.d:hover{border-color:var(--err);color:var(--err)}
.row{display:flex;gap:10px;align-items:center;padding:9px 2px;border-bottom:1px solid var(--border)}
.row .n{flex:1;word-break:break-all;cursor:pointer}
.row .s{color:var(--muted);font-size:12px;white-space:nowrap}
.n.dir{color:var(--brass)}
input,textarea,select{width:100%;background:var(--bg-alt);color:var(--text);
border:1px solid var(--border-l);padding:9px;font:inherit;font-size:16px}
input:focus,textarea:focus,select:focus{outline:none;border-color:var(--brass)}
label{display:block;margin:16px 0 5px;color:var(--muted);font-size:11px;letter-spacing:.14em;
text-transform:uppercase}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}
#prev{background:var(--card);border:1px solid var(--border);padding:14px;overflow:auto;max-height:70vh}
#prev h1,#prev h2,#prev h3{margin:.6em 0 .3em;line-height:1.25}
#prev pre,#prev code{background:var(--bg-alt);font-family:var(--mono)}
#prev pre{padding:10px;overflow:auto;border:1px solid var(--border)}
#prev code{padding:1px 4px}
#prev blockquote{margin:.6em 0;padding-left:12px;border-left:2px solid var(--brass);color:var(--muted)}
#prev hr{border:0;border-top:1px dashed var(--border-l);margin:1.2em 0}
.pagebreak{color:var(--muted);font-size:11px;letter-spacing:.14em;text-transform:uppercase;
margin:1em 0;text-align:center}
#toast:empty{display:none}
#toast .msg{margin:0}
.msg{margin:10px 0;padding:10px;border:1px solid var(--border-l);background:var(--card)}
.msg.err{border-color:var(--err);color:var(--err)}
.hint{color:var(--muted);font-size:12px;margin-top:6px}
@media(max-width:760px){.grid{grid-template-columns:1fr}}
</style></head><body>
<header>
  <div class="bar"><div class="brand"><b class="wordmark" id="title">NumOS</b>
    <span class="tag">portal</span></div>
    <button onclick="goBack()">Back</button>
    <button onclick="flipTheme()">Contrast</button></div>
  <nav class="bar"><a class="tab" href="#/">Files</a><a class="tab" href="#/config">Config</a>
    <a class="tab" href="#/help">Help</a><span id="net"></span></nav>
  <div id="toast" class="bar"></div>
</header>
<main id="app">loading…</main>
<script>
function $(id){return document.getElementById(id)}
function esc(s){return (s||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;')}
function toast(m,bad){$('toast').innerHTML='<div class="msg'+(bad?' err':'')+'">'+esc(m)+'</div>';
  if(!bad)setTimeout(function(){$('toast').innerHTML=''},2500)}
function goBack(){if(history.length>1){history.back()}else{location.hash='#/'}}
function api(path,opt){return fetch(path,opt).then(function(r){return r.text().then(function(t){
  try{return JSON.parse(t)}catch(e){throw new Error(t||('HTTP '+r.status))}})})
  .then(function(j){if(j&&j.ok===false)throw new Error(j.error||'failed');return j})}
function params(h){var o={},q=h.split('?')[1];if(q)q.split('&').forEach(function(kv){var p=kv.split('=');
  o[decodeURIComponent(p[0])]=decodeURIComponent((p[1]||'').replace(/\+/g,' '))});return o}
function q(path,p){var s='?path='+encodeURIComponent(path);for(var k in (p||{}))
  s+='&'+k+'='+encodeURIComponent(p[k]);return s}
/* ---- Markdown: the device's subset, rendered offline ---- */
function md(src){
  var out=[],lines=esc(src||'').replace(/\r\n?/g,'\n').split('\n'),i=0,fence=false,buf=[];
  function flush(){if(out.length){var h='<p>'+out.join('<br>')+'</p>';buf.push(h);out=[]}}
  while(i<lines.length){
    var l=lines[i];
    if(/^```/.test(l)){fence=!fence;flush();buf.push(fence?'<pre><code>':'</code></pre>');i++;continue}
    if(fence){buf.push(l+'\n');i++;continue}
    if(/^\s*$/.test(l)){flush();i++;continue}
    if(/^-{3,}\s*$/.test(l)){flush();buf.push('<div class="pagebreak">&#8212;&#8212; page break &#8212;&#8212;</div>');i++;continue}
    var h=l.match(/^(#{1,6})\s+(.*)$/);
    if(h){flush();var n=h[1].length;buf.push('<h'+n+'>'+inline(h[2])+'</h'+n+'>');i++;continue}
    if(/^\s*([-*+]|\d+\.)\s+/.test(l)){flush();var tag=/^\s*\d+\./.test(l)?'ol':'ul';buf.push('<'+tag+'>');
      while(i<lines.length&&/^\s*([-*+]|\d+\.)\s+/.test(lines[i])){
        buf.push('<li>'+inline(lines[i].replace(/^\s*([-*+]|\d+\.)\s+/,''))+'</li>');i++}
      buf.push('</'+tag+'>');continue}
    if(/^\s*&gt;\s?/.test(l)){flush();buf.push('<blockquote>'+inline(l.replace(/^\s*&gt;\s?/,''))+'</blockquote>');i++;continue}
    out.push(inline(l));i++;
  }
  flush();if(fence)buf.push('</code></pre>');
  return buf.join('\n');
}
function inline(s){
  return s.replace(/`([^`]+)`/g,'<code>$1</code>')
          .replace(/\*\*([^*]+)\*\*/g,'<b>$1</b>')
          .replace(/(^|[^*])\*([^*]+)\*/g,'$1<i>$2</i>')
          .replace(/(^|[^_])_([^_]+)_/g,'$1<i>$2</i>')
          .replace(/\[([^\]]+)\]\([^)]*\)/g,'$1');
}
/* ---- views ---- */
function viewFiles(p){
  var path=p.path||'/';
  api('/api/files'+q(path)).then(function(j){
    var h='<div class="bar"><button onclick="goUp(\''+path+'\')">&#8593; Up</button>'+
      '<button class="p" onclick="newFile(\''+path+'\')">+ file</button>'+
      '<button onclick="newDir(\''+path+'\')">+ folder</button>'+
      '<label class="btn" style="margin:0">upload<input type="file" style="display:none" '+
      'onchange="upload(this,\''+path+'\')"></label></div>'+
      '<div class="hint">'+esc(path)+' &middot; '+j.free+' free</div>';
    if(!j.entries.length)h+='<div class="msg">empty</div>';
    j.entries.forEach(function(e){
      var full=(path=='/'?'':path)+'/'+e.name;
      var act=e.dir?"openDir('"+full+"')":"openFile('"+full+"')";
      h+='<div class="row"><span class="n '+(e.dir?'dir':(/\.md$/i.test(e.name)?'md':''))+
        '" onclick="'+act+'">'+esc(e.name)+(e.dir?'/':'')+'</span>'+
        '<span class="s">'+(e.dir?'':e.size+' B')+'</span>'+
        '<button onclick="rename(\''+full+'\')">rn</button>'+
        '<button class="d" onclick="del(\''+full+'\')">del</button></div>'});
    $('app').innerHTML=h;$('title').textContent='Files'});
}
function openDir(p){location.hash='#/browse?path='+encodeURIComponent(p)}
function openFile(p){location.hash='#/edit?path='+encodeURIComponent(p)}
function goUp(p){openDir(p=='/'?'/':p.substring(0,p.lastIndexOf('/'))||'/')}
function newFile(p){var n=prompt('new file name (.md)');if(!n)return;openFile((p=='/'?'':p)+'/'+n)}
function newDir(p){var n=prompt('new folder name');if(!n)return;
  api('/api/mkdir',{method:'POST',body:JSON.stringify({path:(p=='/'?'':p)+'/'+n})})
    .then(function(){toast('folder created');render()}).catch(function(e){toast(e.message,1)})}
function rename(p){var n=prompt('rename to',p.substring(p.lastIndexOf('/')+1));if(!n)return;
  var to=p.substring(0,p.lastIndexOf('/')+1)+n;
  api('/api/rename',{method:'POST',body:JSON.stringify({from:p,to:to})})
    .then(function(){toast('renamed');render()}).catch(function(e){toast(e.message,1)})}
function del(p){if(!confirm('delete '+p+' ?'))return;
  api('/api/delete',{method:'POST',body:JSON.stringify({path:p})})
    .then(function(){toast('deleted');render()}).catch(function(e){toast(e.message,1)})}
function upload(inp,path){
  var f=inp.files[0];if(!f)return;var fd=new FormData();fd.append('file',f,f.name);
  fetch('/upload'+q(path),{method:'POST',body:fd}).then(function(r){return r.json()})
    .then(function(j){if(!j.ok)throw new Error(j.error);toast('uploaded '+f.name);render()})
    .catch(function(e){toast(e.message,1)})}
function viewEdit(p){
  var path=p.path||'/';
  fetch('/api/file'+q(path)).then(function(r){
    if(!r.ok)throw new Error('cannot open '+path+' (HTTP '+r.status+')');
    return r.text()}).then(function(text){
    $('app').innerHTML='<div class="bar"><button onclick="goBack()">&#8592; Back</button>'+
      '<button class="p" onclick="save(\''+path+'\')">Save</button>'+
      '<span class="hint">'+esc(path)+'</span></div>'+
      '<div class="grid"><textarea id="src" spellcheck="false" style="height:70vh"></textarea>'+
      '<div id="prev"></div></div>';
    $('src').value=text;draw();
    $('src').addEventListener('input',function(){clearTimeout(window._t);
      window._t=setTimeout(draw,120)});
    $('title').textContent=path.substring(path.lastIndexOf('/')+1)})
  .catch(function(e){$('app').innerHTML='<div class="msg err">'+esc(e.message)+'</div>'})}
function draw(){$('prev').innerHTML=md($('src').value)}
function save(path){
  api('/api/file',{method:'POST',body:JSON.stringify({path:path,content:$('src').value})})
    .then(function(){toast('saved')}).catch(function(e){toast(e.message,1)})}
function viewConfig(){
  api('/api/config').then(function(j){
    $('app').innerHTML=
    '<label>Provider API key ('+(j.key_set?'currently set':'not set')+')</label>'+
    '<input id="key" type="password" autocomplete="off" placeholder="'+
      (j.key_set?'leave blank to keep':'paste your OpenRouter key')+'">'+
    '<div class="hint">Stored in NVS on the device, never written to a file and never shown again.</div>'+
    '<label>Model</label><input id="model" value="'+esc(j.model)+'">'+
    '<label>Base URL</label><input id="base_url" value="'+esc(j.base_url)+'">'+
    '<hr><label>Wolfram|Alpha AppID ('+(j.wa_appid_set?'currently set - from '+esc(j.wa_appid_source):'not set - no check offered')+')</label>'+
    '<input id="wa" type="password" autocomplete="off" placeholder="'+
      (j.wa_appid_set?'leave blank to keep':'paste your Wolfram|Alpha AppID')+'">'+
    '<div class="hint">Your own AppID, stored in NVS exactly like the API key: never written '+
    'to a file, never shown again. Get one at developer.wolframalpha.com - the free tier is '+
    'non-commercial and capped per month, and it is YOUR allowance because the device calls '+
    'Wolfram|Alpha directly. Leave this empty and the calculator never offers a check.</div>'+
    '<div class="bar" style="margin-top:10px"><button onclick="testWa()">Test AppID</button>'+
    (j.wa_appid_set?'<button class="d" onclick="clearWa()">Forget AppID</button>':'')+'</div>'+
    '<div class="bar" style="margin-top:14px"><button class="p" onclick="saveCfg()">Save settings</button>'+
    '<button onclick="clearKey()">Forget key</button></div>'+
    '<hr><label>Wi-Fi</label>'+
    '<div class="bar"><button onclick="scan()">Scan networks</button>'+
    '<select id="scan" style="flex:1" onchange="pick(this.value)">'+
    '<option value="">— tap to scan —</option></select></div>'+
    '<label>SSID</label><input id="ssid" value="'+esc(j.wifi_ssid)+'">'+
    '<label>Wi-Fi password</label><input id="wp" type="password" autocomplete="off" '+
      'placeholder="'+(j.wifi_set?'leave blank to keep':'network password')+'">'+
    '<div class="bar" style="margin-top:14px"><button onclick="testWifi()">Test</button>'+
    '<button class="p" onclick="saveWifi()">Save &amp; join</button>'+
    (j.wifi_set?'<button class="d" onclick="forgetWifi()">Forget</button>':'')+'</div>'+
    '<div class="hint">Joining stops this access point, so save it last. Scanning briefly '+
    'pauses the page. Only your own network can be configured here — a scan reports names '+
    'and signal only; it cannot read anybody\'s password.</div>'+
    '<div id="saved"></div>';
    $('title').textContent='Config';
    loadSaved()})}
function loadSaved(){api('/api/wifi-list').then(function(j){
  if(!j.nets.length)return;
  var h='<label>Saved networks on the device ('+j.nets.length+') — tried in this order</label>';
  j.nets.forEach(function(n,i){
    h+='<div class="row"><span class="n'+(n.ssid==j.active&&j.connected?' md':'')+'">'+
      (i+1)+'. '+esc(n.ssid)+(n.ssid==j.active&&j.connected?' &nbsp;connected':'')+'</span>'+
      '<button class="d" onclick="forgetOne(\''+esc(n.ssid).replace(/'/g,"\\'")+'\')">forget</button></div>'});
  $('saved').innerHTML=h}).catch(function(){})}
function forgetOne(ssid){if(!confirm('forget '+ssid+'?'))return;
  api('/api/wifi-forget',{method:'POST',body:JSON.stringify({ssid:ssid})})
    .then(function(){toast('forgotten');loadSaved()}).catch(function(e){toast(e.message,1)})}
function saveCfg(){
  var b={model:$('model').value,base_url:$('base_url').value};
  if($('key').value)b.api_key=$('key').value;
  if($('wa').value)b.wa_appid=$('wa').value;
  api('/api/config',{method:'POST',body:JSON.stringify(b)})
    .then(function(){toast('settings saved');$('key').value='';$('wa').value=''})
    .catch(function(e){toast(e.message,1)})}
function testWa(){
  var a=$('wa').value;
  if(!a){toast('type the AppID first',1);return}
  toast('asking Wolfram|Alpha…');
  api('/api/wa-test',{method:'POST',body:JSON.stringify({appid:a})})
    .then(function(j){toast('AppID works - '+j.interpretation)})
    .catch(function(e){toast(e.message,1)})}
function clearWa(){if(!confirm('remove the stored Wolfram|Alpha AppID?'))return;
  api('/api/config',{method:'POST',body:JSON.stringify({clear_wa:true})})
    .then(function(){toast('AppID removed');viewConfig()}).catch(function(e){toast(e.message,1)})}
function clearKey(){if(!confirm('remove the stored API key?'))return;
  api('/api/config',{method:'POST',body:JSON.stringify({clear_key:true})})
    .then(function(){toast('key removed');viewConfig()}).catch(function(e){toast(e.message,1)})}
function scan(){
  toast('scanning…');api('/api/wifi-scan').then(function(j){
    var s=$('scan');s.innerHTML='<option value="">— '+j.nets.length+' found —</option>';
    j.nets.sort(function(a,b){return b.rssi-a.rssi}).forEach(function(n){
      var o=document.createElement('option');o.value=n.ssid;
      o.textContent=n.ssid+'  ('+n.rssi+' dBm'+(n.open?', open':'')+')';s.appendChild(o)});
    toast(j.nets.length+' networks')}).catch(function(e){toast(e.message,1)})}
function pick(v){if(v)$('ssid').value=v}
function forgetWifi(){if(!confirm('forget the saved network?'))return;
  api('/api/wifi-forget',{method:'POST',body:'{}'})
    .then(function(){toast('forgotten');viewConfig()}).catch(function(e){toast(e.message,1)})}
function testWifi(){
  var b={ssid:$('ssid').value};if($('wp').value)b.pass=$('wp').value;
  toast('testing…');api('/api/wifi-test',{method:'POST',body:JSON.stringify(b)})
    .then(function(j){toast('joined '+j.ssid+' ('+j.ip+')')}).catch(function(e){toast(e.message,1)})}
function saveWifi(){
  var b={ssid:$('ssid').value};if($('wp').value)b.pass=$('wp').value;
  api('/api/wifi-save',{method:'POST',body:JSON.stringify(b)})
    .then(function(){toast('saved — the calculator is joining your network')})
    .catch(function(e){toast(e.message,1)})}
function viewHelp(){$('app').innerHTML='<div class="msg"><b>What this does</b><br>'+
  'Browses and edits every file in the calculator\'s flash, and sets the AI provider key, '+
  'model and Wi-Fi. Markdown renders as you type; a line of <code>---</code> is a page break, '+
  'which is how the calculator pages a note.<br><br><b>Getting here</b><br>'+
  'Settings &rarr; Web portal &rarr; ON, join <i>'+esc('NumOS-XXXX')+'</i> in your phone\'s '+
  'Wi-Fi list (password is on the calculator screen, shown under the toggle), then open '+
  'http://192.168.4.1.<br><br><b>Notes</b><br>No internet is available on this AP. '+
  'The portal closes itself after 15 idle minutes.</div>';$('title').textContent='Help'}
function render(){
  api('/api/status').then(function(s){$('net').textContent=
    (s.sta_connected?('wifi: '+s.sta_ssid+' '+s.sta_ip):'wifi: not joined')+
    ' | ap: '+s.stations+' | heap '+s.heap_kb+'K'}).catch(function(){});
  var h=location.hash||'#/',p=params(h),v=h.split('?')[0];
  if(v=='#/browse')return viewFiles(p);
  if(v=='#/edit')return viewEdit(p);
  if(v=='#/config')return viewConfig();
  if(v=='#/help')return viewHelp();
  return viewFiles({});
}
function flipTheme(){var d=document.documentElement;
  var t=d.getAttribute('data-theme')==='light'?'dark':'light';
  d.setAttribute('data-theme',t);
  try{localStorage.setItem('numos-theme',t)}catch(e){}}
try{var _nt=localStorage.getItem('numos-theme');
  if(_nt)document.documentElement.setAttribute('data-theme',_nt)}catch(e){}
window.addEventListener('hashchange',render);render();
</script></body></html>)PAGE";

// ── file-system handlers ────────────────────────────────────────────────────

void handleIndex() {
    touch();
    g_server.sendHeader("Cache-Control", "no-store");
    g_server.sendHeader("Connection", "close");
    g_server.send_P(200, "text/html; charset=utf-8", kPage);
}

void handleStatus() {
    const net::WifiState w = net::Wifi::state();
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "{\"ok\":true,\"ap\":\"%s\",\"url\":\"http://%s\",\"stations\":%d,"
                  "\"requests\":%u,\"sta_connected\":%s,\"sta_ssid\":\"%s\",\"sta_ip\":\"%s\","
                  "\"heap_kb\":%u,\"fs_total\":%u,\"fs_used\":%u}",
                  g_apSsid,
                  WiFi.softAPIP().toString().c_str(),
                  static_cast<int>(WiFi.softAPgetStationNum()),
                  static_cast<unsigned>(g_requests),
                  w.connected ? "true" : "false",
                  jsonEscape(w.ssid).c_str(),
                  jsonEscape(w.ip).c_str(),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024u),
                  static_cast<unsigned>(LittleFS.totalBytes()),
                  static_cast<unsigned>(LittleFS.usedBytes()));
    sendJson(200, buf);
}

void handleFiles() {
    std::string path = "/";
    if (g_server.hasArg("path") && !safePath(g_server.arg("path").c_str(), path)) {
        sendError(400, "bad path");
        return;
    }

    FsLock lock;
    File dir = LittleFS.open(path.c_str(), "r");
    if (!dir || !dir.isDirectory()) { sendError(404, "not a directory: " + path); return; }

    std::string body = "{\"ok\":true,\"path\":\"" + jsonEscape(path) + "\",\"entries\":[";
    bool first = true;
    std::vector<std::string> names;
    std::vector<size_t> sizes;
    std::vector<bool> dirs;
    while (File e = dir.openNextFile()) {
        std::string name = e.name();
        const size_t slash = name.find_last_of('/');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (name.empty() || name == "." || name == "..") continue;
        names.push_back(name);
        dirs.push_back(e.isDirectory());
        sizes.push_back(e.isDirectory() ? 0u : static_cast<size_t>(e.size()));
    }
    dir.close();

    for (size_t i = 0; i < names.size(); ++i) {
        if (!first) body += ',';
        first = false;
        char sz[32];
        std::snprintf(sz, sizeof(sz), "%u", static_cast<unsigned>(sizes[i]));
        body += "{\"name\":\"" + jsonEscape(names[i]) + "\",\"dir\":" +
                (dirs[i] ? "true" : "false") + ",\"size\":" + sz + "}";
    }
    body += "],\"free\":\"";
    char freebuf[32];
    const size_t used = LittleFS.usedBytes();
    const size_t total = LittleFS.totalBytes();
    std::snprintf(freebuf, sizeof(freebuf), "%uK of %uK",
                  static_cast<unsigned>((total - used) / 1024u),
                  static_cast<unsigned>(total / 1024u));
    body += freebuf;
    body += "\"}";
    sendJson(200, body);
}

/**
 * Raw bytes, not JSON: a note can be ~190 KB, and JSON-escaping that would
 * triple it in internal RAM on the HTTP task for no benefit. The page reads it
 * as text.
 */
void handleFileGet() {
    std::string path;
    if (!safePath(g_server.arg("path").c_str(), path)) { sendError(400, "bad path"); return; }

    FsLock lock;
    File f = LittleFS.open(path.c_str(), "r");
    if (!f || f.isDirectory()) { sendError(404, "no such file: " + path); return; }

    touch();
    g_server.sendHeader("Cache-Control", "no-store");
    g_server.sendHeader("Connection", "close");
    g_server.setContentLength(f.size());
    g_server.send(200, mimeFor(path), "");
    g_server.streamFile(f, mimeFor(path));
    f.close();
}

void handleFilePut() {
    const std::string body = g_server.arg("plain").c_str();
    std::string path, content;
    if (!jsonFindString(body, "path", path) || !safePath(path.c_str(), path)) {
        sendError(400, "bad path");
        return;
    }
    jsonFindString(body, "content", content);

    FsLock lock;
    const std::string dir = parentOf(path);
    if (dir != "/") ensureDir(dir);
    if (!writeTextAtomic(path, content)) { sendError(500, "write failed: " + path); return; }
    sendJson(200, "{\"ok\":true}");
}

void handleMkdir() {
    const std::string body = g_server.arg("plain").c_str();
    std::string path;
    if (!jsonFindString(body, "path", path)) { sendError(400, "no path"); return; }
    std::string clean;
    if (!safePath(path.c_str(), clean)) { sendError(400, "bad path"); return; }
    FsLock lock;
    const bool ok = ensureDir(clean);
    sendJson(ok ? 200 : 500, ok ? "{\"ok\":true}"
                                : "{\"ok\":false,\"error\":\"mkdir failed\"}");
}

void handleDelete() {
    const std::string body = g_server.arg("plain").c_str();
    std::string path;
    if (!jsonFindString(body, "path", path)) { sendError(400, "no path"); return; }
    std::string clean;
    if (!safePath(path.c_str(), clean) || clean == "/") { sendError(400, "bad path"); return; }
    FsLock lock;
    const bool ok = LittleFS.remove(clean.c_str());
    sendJson(ok ? 200 : 500, ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"delete failed\"}");
}

void handleRename() {
    const std::string body = g_server.arg("plain").c_str();
    std::string from, to, cleanFrom, cleanTo;
    if (!jsonFindString(body, "from", from) || !jsonFindString(body, "to", to)) {
        sendError(400, "from/to required");
        return;
    }
    if (!safePath(from.c_str(), cleanFrom) || !safePath(to.c_str(), cleanTo)) {
        sendError(400, "bad path");
        return;
    }
    FsLock lock;
    const bool ok = LittleFS.rename(cleanFrom.c_str(), cleanTo.c_str());
    sendJson(ok ? 200 : 500, ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"rename failed\"}");
}

// ── config handlers ─────────────────────────────────────────────────────────

/** Host + model: /ai/config.json. The key never travels this path, and the
 *  transport is not a setting any more — the board is wifi-only. */
void writeAiConfig(const std::string& baseUrl, const std::string& model) {
    const char* kPath = "/ai/config.json";
    if (!LittleFS.exists("/ai")) LittleFS.mkdir("/ai");
    if (!LittleFS.exists(kPath)) {
        writeTextAtomic(kPath,
                        "{\n"
                        "  \"base_url\": \"https://openrouter.ai/api/v1\",\n"
                        "  \"model\": \"google/gemini-2.5-flash-lite\",\n"
                        "  \"prompts_dir\": \"/ai/prompts\",\n"
                        "  \"results_dir\": \"/ai/results\",\n"
                        "  \"retention_max_files\": 200,\n"
                        "  \"max_tokens\": 1200\n"
                        "}\n");
    }
    if (!baseUrl.empty())  upsertJsonString(kPath, "base_url", baseUrl);
    if (!model.empty())    upsertJsonString(kPath, "model", model);
}

void handleConfigGet() {
    const ai::AiConfig cfg = ai::AiConfig::load("/ai/config.json");
    std::string ssid, pass;
    const bool wifiSet = net::Wifi::loadCredentials(ssid, pass);

    std::string body = "{\"ok\":true,\"model\":\"" + jsonEscape(cfg.model) +
                       "\",\"base_url\":\"" + jsonEscape(cfg.baseUrl) +
                       "\",\"key_set\":" + (cfg.apiKey.empty() ? "false" : "true") +
                       // The AppID's PRESENCE and where it came from; never the value.
                       ",\"wa_appid_set\":" + (cfg.waAppId.empty() ? "false" : "true") +
                       ",\"wa_appid_source\":\"" + jsonEscape(cfg.waKeySource()) +
                       ",\"wifi_ssid\":\"" + jsonEscape(ssid) +
                       "\",\"wifi_set\":" + (wifiSet ? "true" : "false") + "}";
    sendJson(200, body);
}

void handleConfigPost() {
    const std::string body = g_server.arg("plain").c_str();
    std::string v;
    bool clearKey = body.find("\"clear_key\":true") != std::string::npos;

    // ── the API key: NVS only, never a file, never echoed back ──────────────
    bool keyChanged = false;
    if (clearKey) {
        Preferences p;
        if (p.begin("numos-ai", false)) { p.remove("api_key"); p.end(); }
        keyChanged = true;
        Serial.println("[PORTAL] api key cleared from NVS");
    } else if (jsonFindString(body, "api_key", v) && !v.empty()) {
        Preferences p;
        if (p.begin("numos-ai", false)) {
            p.putString("api_key", v.c_str());
            p.end();
            keyChanged = true;
            // The key itself is never printed — only that one was stored.
            Serial.printf("[PORTAL] api key stored in NVS (%u chars)\n",
                          static_cast<unsigned>(v.size()));
        } else {
            sendError(500, "NVS open failed");
            return;
        }
    }

    // ── the Wolfram|Alpha AppID: NVS only, exactly like the API key ─────────
    // BYO is the whole point: a shipped AppID would put one non-commercial
    // allowance behind the entire fleet (voiding it), and a compiled string is
    // recoverable from a flash dump. Same namespace and key name AiConfig::load()
    // reads, so the app picks it up with no extra plumbing.
    bool waChanged = false;
    if (body.find("\"clear_wa\":true") != std::string::npos) {
        Preferences p;
        if (p.begin("numos-ai", false)) { p.remove("wa_appid"); p.end(); }
        waChanged = true;
        Serial.println("[PORTAL] Wolfram AppID cleared from NVS");
    } else if (jsonFindString(body, "wa_appid", v) && !v.empty()) {
        Preferences p;
        if (p.begin("numos-ai", false)) {
            p.putString("wa_appid", v.c_str());
            p.end();
            waChanged = true;
            // Never the value, only that one was stored and how long it is.
            Serial.printf("[PORTAL] Wolfram AppID stored in NVS (%u chars)\n",
                          static_cast<unsigned>(v.size()));
        } else {
            sendError(500, "NVS open failed");
            return;
        }
    }

    std::string baseUrl, model;
    jsonFindString(body, "base_url", baseUrl);
    jsonFindString(body, "model", model);
    {
        FsLock lock;
        writeAiConfig(baseUrl, model);
    }
    sendJson(200, std::string("{\"ok\":true,\"key_set\":") +
                      (keyChanged ? "true" : "false") +
                      ",\"wa_set\":" + (waChanged ? "true" : "false") + "}");
}

void handleWifiTest() {
    const std::string body = g_server.arg("plain").c_str();
    std::string ssid, pass;
    if (!jsonFindString(body, "ssid", ssid) || ssid.empty()) { sendError(400, "ssid required"); return; }
    if (!jsonFindString(body, "pass", pass)) {
        net::Wifi::loadCredentials(ssid, pass);   // blank field = keep stored pass
    }
    if (!net::Wifi::connect(ssid, pass, 20000)) {
        sendError(400, "could not join '" + ssid + "' - check the password");
        return;   // AP stays up
    }
    const net::WifiState w = net::Wifi::state();
    sendJson(200, std::string("{\"ok\":true,\"ssid\":\"") + jsonEscape(w.ssid) +
                      "\",\"ip\":\"" + jsonEscape(w.ip) + "\"}");
}

/**
 * Test an AppID BEFORE it is saved: one real GET to the LLM API with `input=1+1`.
 *
 * Built with ai::buildWolframUrl() rather than a hand-written URL, so what the
 * user validates here is the exact request shape the app will send — the same
 * host, path, `maxchars` and parameter order. The AppID rides in the
 * Authorization header only, is never logged, never echoed, and never stored by
 * this handler: a test is a test.
 *
 * A wrong AppID is the one failure this screen exists to catch, and the vendor's
 * published table gets it wrong (it claims 403; the wire answers 400 for a
 * missing AppID and 401 for a bad one), so the verdict comes from
 * classifyWolfram() reading the body.
 */
void handleWaTest() {
    const std::string body = g_server.arg("plain").c_str();
    std::string appid;
    if (!jsonFindString(body, "appid", appid) || appid.empty()) {
        sendError(400, "appid required");
        return;
    }

    ai::AiConfig cfg = ai::AiConfig::load("/ai/config.json");
    cfg.waAppId    = appid;
    cfg.waMaxChars = 64;                      // a test does not need a long answer

    net::HttpReq req{};
    req.url             = ai::buildWolframUrl(cfg, "1+1");
    req.method          = "GET";
    req.body            = nullptr;
    req.contentType.clear();
    req.accept          = "text/plain";
    req.bearer          = appid;              // the only copy of it in here
    req.userAgent       = "NumOS/1.0";
    req.timeoutMs       = cfg.waTimeoutMs;
    req.headerBufSize   = 2048;
    req.followRedirects = false;

    net::HttpStream stream;
    if (!stream.open(req)) {
        sendError(400, "could not reach Wolfram|Alpha: " + stream.error());
        return;
    }

    std::string raw;
    char buf[512];
    for (;;) {
        const int n = stream.read(buf, sizeof(buf));
        if (n <= 0) break;
        if (raw.size() < 4096) raw.append(buf, static_cast<size_t>(n));
    }
    const int http = stream.status();
    stream.close();

    std::string detail;
    const ai::WaStatus st = ai::classifyWolfram(http, raw, &detail);
    if (st != ai::WaStatus::Ok) {
        std::string msg = ai::waStatusText(st);
        if (http) msg += " (HTTP " + std::to_string(http) + ")";
        if (!detail.empty()) msg += ": " + detail;
        sendError(400, msg);
        return;
    }

    const ai::WolframResult r = ai::parseWolframBody(raw);
    const std::string said = r.interpretation.empty() ? std::string("1+1 = 2")
                                                      : r.interpretation;
    Serial.println("[PORTAL] Wolfram AppID accepted");
    sendJson(200, "{\"ok\":true,\"interpretation\":\"" + jsonEscape(said) + "\"}");
}

void handleWifiSave() {
    const std::string body = g_server.arg("plain").c_str();
    std::string ssid, pass;
    if (!jsonFindString(body, "ssid", ssid) || ssid.empty()) { sendError(400, "ssid required"); return; }
    if (!jsonFindString(body, "pass", pass)) net::Wifi::loadCredentials(ssid, pass);
    if (!net::Wifi::saveCredentials(ssid, pass)) { sendError(500, "NVS write failed"); return; }
    net::Wifi::setEnabled(true);
    Serial.printf("[PORTAL] wifi credentials stored for '%s'\n", ssid.c_str());
    sendJson(200, "{\"ok\":true}");
}

/**
 * Scan for nearby networks so the user PICKS an SSID instead of typing it.
 *
 * Honest caveats, and both are in the page: an ESP32 station scan briefly takes
 * the radio away from the access point (the phone may see this page stall for a
 * second or two), and only SSID/signal/encryption are reported.
 *
 * This is a network LIST, not a way to obtain anyone's key — a WPA2 PSK is never
 * transmitted, so there is nothing here to harvest.
 */
void handleWifiScan() {
    // The scan itself lives in net::Wifi, shared with the on-device screen, so
    // there is one scanner and one cache.
    net::Wifi::startScan();
    const uint32_t t0 = millis();
    while (net::Wifi::scanRunning() && (uint32_t)(millis() - t0) < 6000u) {
        delay(50);   // this task does nothing else while a scan is in flight
    }

    std::string body = "{\"ok\":true,\"nets\":[";
    const size_t n = net::Wifi::scanCount();
    for (size_t i = 0; i < n; ++i) {
        net::WifiScanEntry e;
        if (!net::Wifi::scanAt(i, e)) continue;
        char rssi[16];
        std::snprintf(rssi, sizeof(rssi), "%d", e.rssi);
        if (i) body += ',';
        body += "{\"ssid\":\"" + jsonEscape(e.ssid) + "\",\"rssi\":" + rssi +
                ",\"open\":" + (e.open ? "true" : "false") + "}";
    }
    body += "],\"stored\":[";
    const size_t stored = net::Wifi::networkCount();
    for (size_t i = 0; i < stored; ++i) {
        net::WifiNetwork net;
        if (!net::Wifi::networkAt(i, net)) continue;
        if (i) body += ',';
        body += "\"" + jsonEscape(net.ssid) + "\"";
    }
    body += "]}";
    sendJson(200, body);
}

/** The saved list, in priority order, with the live one marked. */
void handleWifiList() {
    const net::WifiState ws = net::Wifi::state();
    std::string body = "{\"ok\":true,\"active\":\"" + jsonEscape(ws.ssid) +
                       "\",\"connected\":" + (ws.connected ? "true" : "false") + ",\"nets\":[";
    const size_t n = net::Wifi::networkCount();
    for (size_t i = 0; i < n; ++i) {
        net::WifiNetwork net;
        if (!net::Wifi::networkAt(i, net)) continue;
        if (i) body += ',';
        body += "{\"ssid\":\"" + jsonEscape(net.ssid) + "\",\"primary\":" +
                (i == 0 ? "true" : "false") + "}";
    }
    body += "]}";
    sendJson(200, body);
}

/** Forget one network, or the whole list when no ssid is given. */
void handleWifiForget() {
    const std::string body = g_server.arg("plain").c_str();
    std::string ssid;
    if (jsonFindString(body, "ssid", ssid) && !ssid.empty()) {
        const bool ok = net::Wifi::forgetNetwork(ssid);
        Serial.printf("[PORTAL] wifi network forgotten: '%s'\n", ssid.c_str());
        sendJson(ok ? 200 : 404, ok ? "{\"ok\":true}"
                                    : "{\"ok\":false,\"error\":\"not stored\"}");
        return;
    }
    net::Wifi::disconnect(/*eraseCreds=*/true);
    Serial.println("[PORTAL] all wifi credentials erased");
    sendJson(200, "{\"ok\":true}");
}

// ── upload (multipart) ──────────────────────────────────────────────────────

void handleUploadWrite() {
    HTTPUpload& up = g_server.upload();
    FsLock lock;
    if (up.status == UPLOAD_FILE_START) {
        std::string dir = "/";
        if (g_server.hasArg("path")) safePath(g_server.arg("path").c_str(), dir);
        std::string name = up.filename.c_str();
        const size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        g_uploadError.clear();
        g_uploadPath.clear();
        g_uploadBytes = 0;
        if (g_uploadFile) g_uploadFile.close();
        if (name.empty()) { g_uploadError = "upload: no filename"; return; }
        if (!LittleFS.exists(dir.c_str())) ensureDir(dir);
        g_uploadPath = (dir == "/" ? std::string() : dir) + "/" + name;
        // Open ONCE and keep it open for the whole transfer. Reopening the file
        // per multipart chunk (the previous shape) cost a LittleFS open/close —
        // and a flash directory walk — for every 1-2 KB the browser sent, which
        // is what made uploads crawl.
        g_uploadFile = LittleFS.open((g_uploadPath + ".part").c_str(), "w");
        if (!g_uploadFile) {
            g_uploadError = "upload: cannot create " + g_uploadPath;
            g_uploadPath.clear();
            return;
        }
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (g_uploadPath.empty() || !g_uploadError.empty() || !g_uploadFile) return;
        if (g_uploadBytes + up.currentSize > kMaxUploadBytes) {
            g_uploadError = "upload: file too large";
            return;
        }
        g_uploadFile.write(up.buf, up.currentSize);
        g_uploadBytes += up.currentSize;
    } else if (up.status == UPLOAD_FILE_END) {
        if (g_uploadFile) g_uploadFile.close();
        if (g_uploadPath.empty() || !g_uploadError.empty()) return;
        const std::string part = g_uploadPath + ".part";
        LittleFS.remove(g_uploadPath.c_str());
        if (LittleFS.rename(part.c_str(), g_uploadPath.c_str())) {
            Serial.printf("[PORTAL] upload %s (%u bytes)\n", g_uploadPath.c_str(),
                          static_cast<unsigned>(g_uploadBytes));
        } else {
            LittleFS.remove(part.c_str());
            g_uploadError = "upload: rename failed";
        }
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        if (g_uploadFile) g_uploadFile.close();
        if (!g_uploadPath.empty()) LittleFS.remove((g_uploadPath + ".part").c_str());
        g_uploadError = "upload aborted by the client";
    }
}

void handleUploadDone() {
    if (!g_uploadError.empty()) {
        const std::string e = g_uploadError;
        g_uploadError.clear();
        sendError(500, e);
        return;
    }
    sendJson(200, std::string("{\"ok\":true,\"path\":\"") + jsonEscape(g_uploadPath) + "\"}");
}

// ── captive: anything unknown goes to the page ──────────────────────────────

void handleNotFound() {
    touch();
    const std::string host = g_server.hostHeader().c_str();
    const std::string url = std::string("http://") + WiFi.softAPIP().toString().c_str() + "/";
    // Android/iOS captive probes (generate_204, hotspot-detect.html, …) get the
    // redirect they interpret as "this network has a sign-in page".
    g_server.sendHeader("Location", url.c_str(), true);
    g_server.sendHeader("Cache-Control", "no-store");
    g_server.send(302, "text/plain", "http://192.168.4.1/\n");
    (void)host;
}

// ── task ────────────────────────────────────────────────────────────────────

void portalTask(void*) {
    for (;;) {
        if (g_quit) break;
        g_server.handleClient();
        g_dns.processNextRequest();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    g_server.stop();
    g_dns.stop();
    g_server.close();
    g_taskDone = true;
    vTaskDelete(nullptr);
}

void registerRoutes() {
    g_server.on("/", HTTP_GET, handleIndex);
    g_server.on("/index.html", HTTP_GET, handleIndex);
    g_server.on("/api/status", HTTP_GET, handleStatus);
    g_server.on("/api/files", HTTP_GET, handleFiles);
    g_server.on("/api/file", HTTP_GET, handleFileGet);
    g_server.on("/api/file", HTTP_POST, handleFilePut);
    g_server.on("/api/mkdir", HTTP_POST, handleMkdir);
    g_server.on("/api/delete", HTTP_POST, handleDelete);
    g_server.on("/api/rename", HTTP_POST, handleRename);
    g_server.on("/api/config", HTTP_GET, handleConfigGet);
    g_server.on("/api/config", HTTP_POST, handleConfigPost);
    g_server.on("/api/wa-test", HTTP_POST, handleWaTest);
    g_server.on("/api/wifi-test", HTTP_POST, handleWifiTest);
    g_server.on("/api/wifi-save", HTTP_POST, handleWifiSave);
    g_server.on("/api/wifi-scan", HTTP_GET, handleWifiScan);
    g_server.on("/api/wifi-list", HTTP_GET, handleWifiList);
    g_server.on("/api/wifi-forget", HTTP_POST, handleWifiForget);
    g_server.on("/upload", HTTP_POST, handleUploadDone, handleUploadWrite);
    g_server.onNotFound(handleNotFound);
    // collectHeaders wants an array + count in this core (there is no
    // single-string overload).
    static const char* kCollected[] = {"Host"};
    g_server.collectHeaders(kCollected, 1);
}

/**
 * Raise the AP + captive server + DNS (the pre-existing Portal::start() body).
 * Split out so the pre-scan path can call it LATER, from tick(), once it has
 * failed to find a reachable saved network. Sets g_phase on both outcomes.
 */
bool raiseAp() {
    const size_t internalFree    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t internalLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (internalFree < kMinInternalFree) {
        char why[96];
        std::snprintf(why, sizeof(why),
                      "low internal RAM for the AP (free %uK, largest %uK)",
                      static_cast<unsigned>(internalFree / 1024u),
                      static_cast<unsigned>(internalLargest / 1024u));
        g_lastError = why;
        g_phase     = PortalPhase::Failed;
        return false;
    }

    makeCredentials();
    if (!net::Wifi::startProvisioningAp(g_apSsid, g_apPass)) {
        char why[96];
        std::snprintf(why, sizeof(why),
                      "could not raise the AP (free %uK, largest %uK)",
                      static_cast<unsigned>(internalFree / 1024u),
                      static_cast<unsigned>(internalLargest / 1024u));
        g_lastError = why;
        g_phase     = PortalPhase::Failed;
        return false;
    }

    if (!g_fsMutex) g_fsMutex = xSemaphoreCreateMutex();
    if (!g_fsMutex) {
        net::Wifi::stopProvisioningAp();
        g_lastError = "mutex allocation failed";
        g_phase     = PortalPhase::Failed;
        return false;
    }

    static bool routesReady = false;
    if (!routesReady) {
        registerRoutes();
        routesReady = true;
    }

    g_quit      = false;
    g_taskDone  = false;
    g_requests  = 0;
    g_lastReqMs = millis();
    g_server.begin();

    g_dns.setErrorReplyCode(DNSReplyCode::NoError);
    if (!g_dns.start(53, "*", WiFi.softAPIP())) {
        Serial.println("[PORTAL] captive DNS failed; reach it at http://192.168.4.1");
    }

    if (xTaskCreatePinnedToCore(&portalTask, "numos_portal", kTaskStack, nullptr,
                                kTaskPrio, &g_task, kTaskCore) != pdPASS) {
        g_server.stop();
        g_dns.stop();
        net::Wifi::stopProvisioningAp();
        g_taskDone = true;
        g_lastError = "portal task create failed";
        g_phase     = PortalPhase::Failed;
        return false;
    }

    g_running = true;
    g_phase   = PortalPhase::Running;
    g_activity.clear();
    Serial.printf("[PORTAL] up  ap=%s  pass=%s  url=http://%s\n", g_apSsid, g_apPass,
                  WiFi.softAPIP().toString().c_str());
    return true;
}

/**
 * Turn the just-finished scan into an ordered candidate list: only saved
 * networks the scan actually saw, strongest first. signalFor() returns the
 * cached sighting RSSI (negative) or 0 when the SSID was not seen.
 */
void buildCandidates() {
    g_candidates.clear();
    const size_t n = net::Wifi::networkCount();
    for (size_t i = 0; i < n; ++i) {
        net::WifiNetwork net;
        if (!net::Wifi::networkAt(i, net)) continue;
        const int rssi = net::Wifi::signalFor(net.ssid);
        if (rssi < 0 && rssi >= kMinJoinRssi) {
            g_candidates.push_back(JoinCandidate{net.ssid, net.pass, rssi});
        }
    }
    for (size_t i = 1; i < g_candidates.size(); ++i) {   // insertion sort, desc
        JoinCandidate key = g_candidates[i];
        size_t j = i;
        while (j > 0 && g_candidates[j - 1].rssi < key.rssi) {
            g_candidates[j] = g_candidates[j - 1];
            --j;
        }
        g_candidates[j] = key;
    }
}

/** Kick off a non-blocking join to the current candidate. */
void startJoin() {
    const JoinCandidate& c = g_candidates[g_candidateIndex];
    net::Wifi::connectAsync(c.ssid, c.pass);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "joining %s", c.ssid.c_str());
    g_activity    = buf;   // the found-count rides along via PortalState
    g_phaseStartMs = millis();
    Serial.printf("[PORTAL] pre-join '%s' (%d dBm, %u found)\n", c.ssid.c_str(), c.rssi,
                  static_cast<unsigned>(g_scanned));
}

}  // namespace

// ── public API ──────────────────────────────────────────────────────────────

bool Portal::start() {
    if (g_running) return true;
    if (g_phase == PortalPhase::Preparing || g_phase == PortalPhase::Joining) return true;
    g_lastError.clear();

    // Pre-scan path: with anything saved, look for a reachable network BEFORE
    // raising the AP. If one joins, no hotspot is needed at all — and, just as
    // important, the channel-hopping scan/associate never happens under a live
    // AP. The AP is raised later from tick() only if every candidate fails.
    const net::WifiState ws = net::Wifi::state();
    const bool wantsJoin    = !ws.connected && net::Wifi::networkCount() > 0;
    const bool scanInFlight = net::Wifi::scanRunning();
    if (!ws.connected && (wantsJoin || scanInFlight)) {
        g_phase        = PortalPhase::Preparing;
        g_phaseStartMs = millis();
        g_scanned      = 0;
        g_candidateIndex = 0;
        g_candidates.clear();
        g_activity     = "scanning";
        if (!scanInFlight) net::Wifi::startScan();
        Serial.println("[PORTAL] pre-scan: looking for a saved network first");
        return true;
    }

    // Nothing to try (no saved networks, or already associated): raise it now.
    return raiseAp();
}

void Portal::stop() {
    // Cancel a pre-scan / pre-join that has not raised the AP yet. The STA is
    // left to its normal walk (begin() is idempotent), so cancelling costs
    // nothing and does not strand the radio.
    if (g_phase == PortalPhase::Preparing || g_phase == PortalPhase::Joining) {
        g_phase = PortalPhase::Off;
        g_activity.clear();
        g_candidates.clear();
        g_candidateIndex = 0;
        net::Wifi::begin();
        Serial.println("[PORTAL] start cancelled");
        return;
    }

    if (!g_running && g_taskDone) {
        // Still honour intent: a host that never started the portal must not
        // block a later STA join.
        g_phase = PortalPhase::Off;
        return;
    }
    g_quit = true;
    const uint32_t t0 = millis();
    while (!g_taskDone && (millis() - t0) < kQuitWaitMs) vTaskDelay(pdMS_TO_TICKS(10));
    if (!g_taskDone) {
        Serial.println("[PORTAL] WARN: portal task did not stop in time");
        g_task = nullptr;
    }
    g_running = false;
    g_phase   = PortalPhase::Off;
    g_activity.clear();

    net::Wifi::stopProvisioningAp();
    // The AP is gone: if credentials are stored, that is the moment the STA
    // takes over, so the user does not have to power-cycle after provisioning.
    net::Wifi::begin();
    Serial.println("[PORTAL] down");
}

bool Portal::running() { return g_running; }

PortalState Portal::state() {
    PortalState s;
    s.running  = g_running;
    s.phase    = g_phase;
    s.activity = g_activity;
    s.scannedNetworks = g_scanned;
    s.apSsid   = g_apSsid;
    s.apPass   = g_apPass;
    s.requests = g_requests;
    s.lastError = g_lastError;
    if (g_running) {
        s.url      = std::string("http://") + WiFi.softAPIP().toString().c_str();
        s.stations = static_cast<int>(WiFi.softAPgetStationNum());
    }
    const net::WifiState w = net::Wifi::state();
    s.staConnected = w.connected;
    s.staSsid      = w.ssid;
    s.staIp        = w.ip;
    return s;
}

void Portal::tick(uint32_t nowMs) {
    // ── Pre-scan: wait for the scan, then pick what to try ──────────────────
    if (g_phase == PortalPhase::Preparing) {
        if (net::Wifi::state().connected) {           // a retry beat us to it
            g_phase = PortalPhase::Off;
            g_activity.clear();
            Serial.println("[PORTAL] already associated - no AP needed");
            return;
        }
        const bool scanning = net::Wifi::scanRunning();
        if (scanning && (nowMs - g_phaseStartMs) < kPreScanMs) return;

        g_scanned = static_cast<int>(net::Wifi::scanCount());
        buildCandidates();
        Serial.printf("[PORTAL] pre-scan done: %d found, %u reachable saved\n",
                      g_scanned, static_cast<unsigned>(g_candidates.size()));
        if (g_candidates.empty()) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%d found, raising AP", g_scanned);
            g_activity = buf;
            raiseAp();
            return;
        }
        g_candidateIndex = 0;
        startJoin();
        return;
    }

    // ── Pre-join: one saved network at a time, then the AP as the fallback ──
    if (g_phase == PortalPhase::Joining) {
        if (net::Wifi::state().connected) {
            g_phase = PortalPhase::Off;
            g_activity.clear();
            Serial.println("[PORTAL] pre-join succeeded - no AP needed");
            return;
        }
        if ((nowMs - g_phaseStartMs) < kJoinMs) return;
        ++g_candidateIndex;
        if (g_candidateIndex >= g_candidates.size()) {
            Serial.println("[PORTAL] no saved network joined; raising the AP");
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%d found, raising AP", g_scanned);
            g_activity = buf;
            raiseAp();
            return;
        }
        startJoin();
        return;
    }

    if (!g_running) return;
    if ((uint32_t)(nowMs - g_lastReqMs) > kIdleStopMs) {
        Serial.println("[PORTAL] idle limit reached; stopping");
        stop();
    }
}

const char* Portal::unavailableReason() { return nullptr; }

}  // namespace net

#else  // ── non-Arduino build: named stub, never a silent no-op ──────────────────

namespace net {

const char* kPortalStubReason =
    "the web portal is firmware-only (it needs the Wi-Fi AP and the filesystem)";

bool Portal::start() { return false; }
void Portal::stop() {}
bool Portal::running() { return false; }
PortalState Portal::state() { PortalState s; s.lastError = kPortalStubReason; return s; }
void Portal::tick(uint32_t) {}
const char* Portal::unavailableReason() { return kPortalStubReason; }

}  // namespace net

#endif  // ARDUINO
