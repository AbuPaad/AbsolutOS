# MATLAB-04-MATLAB-ADDON — a MATLAB add-on that talks to the calc

Date: 2026-10-18 · Decision record + build spec. No firmware change required for v0.
Depends on: `MATLAB-02-USECASE.md` §4 (math-out, not MATLAB-in).

---

## 1. Decision

Build a **MATLAB add-on (`.mltbx`) that acts as the HTTP client**, pulling from
the calculator's existing `Portal` server. The calculator stays dumb: it writes
text files and serves them. MATLAB does all the MATLAB.

Two consequences worth stating plainly:

- **v0 needs zero firmware work.** Every route below already exists.
- **The reverse direction is free** — `POST /api/file` already writes, so MATLAB
  can push a result *down* to the calc as a bench display.

## 2. Route contract (verified in `src/net/Portal.cpp`)

| Route | Line | Method | Payload | Use |
|:--|:--|:--|:--|:--|
| `/api/status` | 1208 | GET | — | heartbeat; also keeps Portal alive |
| `/api/files` | 1209 | GET | — | list calc contents |
| `/api/file` | 1210 | GET | `?path=` | **pull** a snippet |
| `/api/file` | 1211 | POST | JSON `{path, content}` | **push** to the calc |
| `/api/delete` | 1213 | POST | JSON | housekeeping |

Server is `WebServer g_server(80)` (`Portal.cpp:70`), ticked from `main.cpp:443`,
so it runs independently of whichever app is on screen.

**Gotcha worth pre-empting:** `handleFilePut` reads the raw body via
`g_server.arg("plain")` and parses it with `jsonFindString(body, "path")`. So the
POST **must be JSON**, not form-encoded. In MATLAB that means
`weboptions("MediaType", "application/json")` — the default `webwrite` encoding
will be silently rejected with a 400.

## 3. Package layout

```
numos/
  +numos/
    connect.m      % set/verify host, store via setpref
    pull.m         % fetch a snippet, return text
    eval_snippet.m % fetch + evalin('base', code)
    paste.m        % fetch + insert into the ACTIVE editor document
    push.m         % send a matrix/text to the calc
    list.m         % files on the calc
    status.m       % heartbeat + dashboard data
  numosApp.m       % uifigure: Pull / Push / status / recent
  Contents.m
  README.md
  LICENSE
```

## 4. The four calls that matter

```matlab
function tf = connect(host)
  if nargin < 1, host = getpref("numos","host",""); end
  s = webread("http://" + host + "/api/status");
  setpref("numos","host",host);
  tf = true;
end

function code = pull(host, path)
  code = webread("http://" + host + "/api/file", "path", path);
end

function paste(host, path)
  code = pull(host, path);
  d = matlab.desktop.editor.getActive;
  d.insertTextAtPositionInLine(code, 1, 1);   % lands in the script you're in
end

function push(host, A, name)
  opts = weboptions("MediaType","application/json");
  body = struct("path", "/mx/" + name + ".txt", ...
                "content", string(mat2str(A, 17)));
  webwrite("http://" + host + "/api/file", body, opts);
end
```

`matlab.desktop.editor.getActive` + `insertTextAtPositionInLine` is the trick
that makes it feel native — MathWorks uses the same API in their own
`matlab-agentic-toolkit`. It is the difference between "a file appeared in a
folder" and "the matrix is in my script".

## 5. The app

`numosApp` — one `uifigure`: host field, a file list from `/api/files`, a Pull
button, a matrix text area with a Push button, and a status strip. Registered via
the `.prj` so it appears under the **APPS** tab. This is the thing that shows up
in a screenshot, and screenshots are what sell an add-on.

## 6. Packaging

- Floor: **R2017a** (`string`, `jsondecode`, `webread` together). `packageToolbox`
  itself is R2016a.
- Build: `matlab.addons.toolbox.packageToolbox(projectFile)` → `.mltbx`.
  Free, base MATLAB, no MathWorks approval. Double-click installs.
- **You don't need MATLAB to author it** — it's plain `.m` text. Ship the folder
  on GitHub (`addpath`) and the `.mltbx` is polish for people who want a button.
- **Do** build and test it once in a real MATLAB before publishing. An add-on
  that fails on install costs more trust than one that never shipped.
- Precedent that this is a normal, accepted distribution route: NXP ships their
  Model-Based Design Toolbox as an offline `.mltbx`, and MathWorks' own
  ThingSpeak toolbox is the same `webread`/`webwrite` pattern.

## 7. Failure modes — write the message, not the stack trace

| Symptom | Cause | Message to show |
|:--|:--|:--|
| connection refused | Portal idle-stopped (15 min) or calc asleep | "Calc isn't serving — open Settings ▸ Wi-Fi ▸ Portal" |
| name not resolved | `numos.local` mDNS | "Use the IP the Portal screen shows; mDNS isn't supported" |
| 400 on push | form encoding instead of JSON | internal; fixed by `MediaType` |
| 404 on pull | file not written yet | "No snippet on the calc — press Send on the matrix first" |
| works once, dies | DHCP moved the calc | `connect` re-checks each session |

MATLAB cannot start the Portal remotely (there is no route for it), and cannot
resolve mDNS. Those two facts define the whole onboarding: **user reads an IP off
the calc screen and types it once.**

## 8. Prerequisite before this is a *promoted* feature

`Portal` has **no authentication**, sits on port 80, and exposes
`POST /api/delete` plus arbitrary file write under LittleFS. While it is an
undocumented convenience that is a demo-box detail. The moment a shipping add-on
advertises "your calculator is an HTTP server", it becomes a documented,
unauthenticated filesystem API on a device carried onto campus Wi-Fi. **Add a
token before the campaign copy mentions the add-on.** A shared secret in
`/ai/config.json` and one header check in `handleFilePut`/`handleDelete`/`handleFileGet`
is the whole fix.

## 9. Why this is a genuinely strong feature (and its limit)

**Strong:** no OEM can ship it — TI, Casio and HP would have to be MathWorks
partners. It needs no infrastructure, no account, no per-query cost, which is
exactly the BYOK/no-cloud posture already chosen. And it hands NumOS the desktop
companion app that TI Connect and HP Connectivity Kit provide but NumOS has never
had — for free, living inside a program engineers already have open.

**Limit:** it is a **tail feature, not a headline.** It requires a NumOS, a MATLAB
licence, and both on one network. Most people reading a Crowd Supply page have
none of the three. Lead with the calculator; let the add-on be the thing an
engineering student discovers in week two and tells their lab partner about.

**Sequencing:** it is a pipe. Build the `matrixToCode` printer first
(`MATLAB-02-USECASE.md` §8) so there is something worth pulling. Printer → push
route → add-on.

---

### Evidence

- `src/net/Portal.cpp:70, 1206-1214, 1280` — port, routes, server start
- `src/net/Portal.cpp` `handleFileGet` / `handleFilePut` — streaming read; JSON
  `{path, content}` write
- `src/net/Portal.h:92-109` — `start`/`stop`/`running`/`tick`, `kIdleStopMs`
- `main.cpp:443` — `net::Portal::tick(millis())`
- MathWorks `matlab.addons.toolbox.packageToolbox` — R2016a, base
- MathWorks `webread` / `webwrite` — base MATLAB, no toolbox
- MathWorks `matlab-agentic-toolkit` — uses `matlab.desktop.editor` themselves
- NXP Model-Based Design Toolbox — third-party `.mltbx` distribution precedent
- File Exchange 52244 ThingSpeak Support Toolbox — `webread`/`webwrite` precedent
