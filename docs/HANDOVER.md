# Handover

Repository state at this handover: `main` at commit `2580738`, working tree clean,
host tests green (22 checks), `./build.sh` green, `sim/run.sh` runs. Read
[`AGENTS.md`](../AGENTS.md) (conventions, build/deploy, gotchas) and
[`STATUS.md`](STATUS.md) (what is proven, and how) before starting.

If a cancelled background agent left any commits or edits after `2580738`, discard
them (`git reset --hard 2580738 && git clean -fd`) before continuing.

**Update:** the registration scaffold has landed on top of this (commit "Scaffold console
registration (PIN + PSN) with a simulated backend and persist settings"): overlay UI, QR, `session_reg_*` API with
a simulated backend, the `app_reg_*` state machine, and real config persistence (no more demo
consoles). What remains is listed under `TODO(libchiaki)` / `TODO(http)` / `TODO(test)` /
`TODO(path)`; see [`STATUS.md`](STATUS.md) "Registration" and "Config file". Do not reset to
`2580738` any more.

## Where the project is

Native LG webOS app **Chiaki webOS** (id `org.homebrew.chiakiwebos`, binary `chiaki`),
SDL2 + LVGL 8.3.11, on the LG 50UP75003LF (webOS TV Lite 6.5.3, 1080p).

- **Done:** UI shell (Hosts / Video / Input / Log), DS4 input (grab, hotplug, rumble),
  TV remote, the play-waves app icon + startup splash (`ui/logo.[ch]`, `ui/splash.c`),
  and a macOS simulator (`sim/run.sh`).
- **Not done / stubs:** libchiaki (no real PS5 session - `chiaki/session.c` is a stub),
  NDL media (`media/ndl.c`), config persistence, and **console registration**.
- The TV is **offline** (10.10.10.12:9922 times out), so there is no on-device check;
  verify in the simulator (`sim/run.sh`) and host tests (`tests/host/run.sh`).

## Next task: console registration (PIN + PSN OAuth)

Implement console registration end to end, both methods, following the repo
conventions (module prefixes, tokens from `ui/theme.h`, widgets from `ui/widgets.h`,
logging via `log_event`, 1920x1080 no overflow, works with both the DS4 and the TV
remote).

### Scope boundary (important)

libchiaki is **not linked** and there is **no HTTP client** yet. Build the full
**UI + `session`/`config`/`app` API with a simulated backend** that works in the
simulator and host tests, and leave the real calls as clearly-marked
`TODO(libchiaki)` / `TODO(http)` at the exact call sites. Do **not** build libchiaki
or add libcurl here (separate milestones).

### Method 1 - PIN (local, no browser)

User enters the console IP and the PIN shown on the PS5 (Settings > System >
Remote Play > Link Device). On success the console is added to the Hosts list.

- libchiaki: `chiaki_regist_start(&regist, &log, &(ChiakiRegistInfo){ .target = <ChiakiTarget>,
  .host = addr, .pin = <uint32_t>, ... }, cb, user)`. The callback receives a
  `ChiakiRegistEvent` (`CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS/FAILED/CANCELED`) with a
  `ChiakiRegisteredHost` (`rp_regist_key[16]`, `rp_key[16]`, `rp_key_type`,
  `server_nickname`, `server_mac[6]`, `console_pin`, `target`) -> fill a `config_host_t`.
  See `chiaki-ng/lib/include/chiaki/regist.h` and its driver in the Qt GUI,
  `chiaki-ng/gui/src/qmlbackend.cpp` (`QmlRegist`, ~line 254).
- Today: `app_register(addr, pin)` logs a placeholder; `config_host_add()` is a stub
  returning false.

### Method 2 - PSN account (OAuth via QR + manual code)

Show Sony's authorize URL as a **QR code** + the URL text; the user opens it on a
phone, logs in, copies the `code` from the redirect URL, and enters it into the app.

- Authorize URL base (`chiaki-ng/gui/include/psnaccountid.h`), with `duid=<device uid>&`
  appended:
  `https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize?service_entity=urn:service-entity:psn&response_type=code&client_id=ba495a24-818c-472b-b12d-ff231c1b5745&redirect_uri=https://remoteplay.dl.playstation.net/remoteplay/redirect&scope=psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update&request_locale=en_US&ui=pr&service_logo=ps&layout_type=popup&smcid=remoteplay&prompt=always&PlatformPrivacyWs1=minimal&duid=<device uid>`
- Redirect page: `https://remoteplay.dl.playstation.net/remoteplay/redirect`; the value
  to enter is its `code=` query param (see `handlePsnLoginRedirect` in qmlbackend.cpp).
- The `duid` normally comes from libchiaki's `chiaki_holepunch_generate_client_device_uid`;
  without libchiaki, generate a random one and persist it in `config` (a stable device UID).
- The token exchange (client secret + HTTP) is a `TODO(http)`.

### QR code

Render the URL as a scannable QR. Vendor a small permissively-licensed encoder with its
license header (e.g. Nayuki's `qrcodegen`, MIT) at a **tracked** path (NOT the gitignored
`third_party/`), and draw the module matrix on an `lv_canvas` (black on white, quiet zone
included) - see `src/ui/logo.c` for the canvas pattern.

### Files to touch

- `src/chiaki/session.[ch]`: registration/login API the UI needs (start/stop, an event on
  done/failed, a getter for the PSN authorize URL). Simulate completion (short delay, like
  the existing connect); mark real calls `TODO(libchiaki)`.
- `src/config/config.[ch]`: in-memory PSN credentials (access/refresh token, account id),
  the device duid, and a real in-memory host list so `config_host_add()` works (keep the
  file persistence itself a STUB).
- `src/app.[ch]`: registration state machine (method, in progress, error, result) announced
  through the existing `app_listener_t`.
- `src/ui/`: a registration overlay/screen (method switch PIN / PSN, keyboard text entry,
  QR + URL, progress/error/success), reachable from the Hosts "Add host" button
  (`src/ui/page_hosts.c`, `add_cb`). `ui/splash.c` shows the `lv_layer_top()` overlay pattern.
- `tests/host/harness.c`: assert the overlay opens; the PIN path reaches success and adds a
  host; the PSN path yields a URL/QR and its code entry reaches success. Keep all existing
  checks green; add overflow/focus-ring checks for the new screens.
- Docs: update `STATUS.md`; `AGENTS.md` if the module map changes.

## Design research already done (do not redo)

- **Native webOS apps have no embedded web engine / webview API.** WebKit is exposed to
  *web* apps only, so chiaki-ng's Qt-WebEngine OAuth capture does not port.
- **Launching the system browser works but is useless here:** `luna://com.webos.applicationManager/launch`
  with `{ "id": "com.webos.app.browser", "params": { "target": "<url>" } }` opens it, but
  launching another app suspends/backgrounds ours and there is **no callback or return
  value**, so the redirect URL cannot be captured (LG docs + forum confirm).
- Hence OAuth on the TV is **QR + manual code entry** (chosen), or a companion webOS *web*
  app (it has WebKit) returning the code over the Luna service bus (alternative, not chosen).

## Open questions

- A writable config path on the TV that survives app re-installs.
- Whether Sony's redirect page displays the code as visible text; the code is definitely in
  the URL query (a phone browser shows it in the address bar).

This file can be deleted once registration lands.