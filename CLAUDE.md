# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

ESP32-S3 firmware for a **LilyGO T-Display-S3-AMOLED 1.43"** (round 466×466 AMOLED): a standalone
realtime shot timer + status display for a **La Marzocco Linea Micra** espresso machine. The newest
LM firmware has no local API, so all data comes from La Marzocco's cloud (`lion.lamarzocco.io`) via
a reverse-engineered protocol (see `docs/plan.md` and the pylamarzocco project).

## Build & flash

```bash
cd firmware
pio run                                          # build
pio run -t upload --upload-port /dev/cu.usbmodem101   # flash (native USB CDC)
```

- **PlatformIO's penv bootstrap requires Python ≤ 3.13.** If the system `pio` runs under a broken
  Python (e.g. Homebrew python@3.14 with the pyexpat/libexpat mismatch), install platformio into a
  python3.12 venv and use that binary: `~/.platformio/pio312-venv/bin/pio`.
- Don't trust `pio run | tail` pipelines' exit codes — check for the `[SUCCESS]` line or PIPESTATUS.
- The platform is pinned to the **pioarduino** fork (Arduino core 3.x); official `espressif32` is stale.
- No unit tests. Verification = clean build (watch warnings), flash, serial log, and eyes on the panel.

### Serial log capture (non-interactive)

```bash
stty -f /dev/cu.usbmodem101 115200 cs8 -cstopb -parenb raw -echo
cat /dev/cu.usbmodem101 > /tmp/serial.log &   # kill after N seconds, then read
```

Reset into the app without flashing: open the port with pyserial and pulse `rts=True → False`
(0.2s). After any reset the USB port re-enumerates — loop until it reappears before reopening.

## Architecture (`firmware/src/`)

- **main.cpp** — everything runs in one `loop()` on core 1: mode state machine
  (idle → brew → frozen), gestures (swipe carousel, long-press = dev mode, double-tap = close),
  idle pages (status / stats / backflush / theme / dark-light), 3-page dev mode, demo gallery,
  boot-log console.
- **display.\*** — all rendering into a PSRAM `Arduino_Canvas`, flushed per frame (Arduino_GFX;
  CO5300 or SH8601 driver over QSPI, switch via `-DUSE_CO5300`). Owns the theme system: 6 schemes
  (Micra machine colors) × dark/light, loaded into the `COL_*` globals by `applyTheme()`;
  persisted in NVS ("display"/"scheme","dark"). Status colors (OK/WARN/ERR) are fixed semantics —
  never theme them per scheme.
- **lm_client.\*** — the data source. DEMO simulation + LIVE cloud client. LIVE runs a FreeRTOS
  task pinned to **core 0**: auth/token, STOMP-over-wss websocket (ping + staleness watchdog),
  REST-poll fallback, stats fetch, outbound command queue (backflush).
- **lm_auth.\*** — on-device ECDSA-secp256r1 request signing (mbedTLS), ported from pylamarzocco.
- **bootlog.\*** — persisted dev toggle; mirrors LOG lines to an on-screen console during boot.
- **ca_certs.h** — pinned Amazon/Starfield root CAs for all lamarzocco.io TLS
  (`-DLM_TLS_INSECURE=1` to bypass while debugging).
- **brew_state.h** — the shared state struct. **config.h** (in `include/`) — all tunables.

## Threading rules (important)

- `liveTask` (core 0) and the UI loop (core 1) share `g_state`: **every** access goes through
  `lockState()`/`unlockState()`, including reads — `uint64_t` reads tear on this 32-bit core.
- **Only the UI core touches the display canvas.** Core-0 code must never render; the bootlog
  console gets core-0 lines via a dirty flag that the UI loop consumes.
- `lmState()` returns a reference to a shared snapshot — treat it as invalidated by the next
  `lmState()` call; don't hold it across calls.

## Cloud protocol notes

- The cloud allows **one realtime websocket per machine**. The official app takes it when opened;
  on disconnect, fall back to REST and back off (`WS_RETRY_MS`) — never fight the app.
- **Never start SNTP with a hostname** (`configTime("pool.ntp.org")`). Arduino's `hostByName()`
  calls lwIP `dns_clear_cache()` on IP-state changes (first HTTPS connect after boot/reconnect);
  a pending SNTP DNS query then fires its callback on our task → lwIP thread-safety assert →
  boot loop. `startSntp()` in lm_client.cpp resolves the pool once and hands SNTP a literal IP.
  Also: SNTP stores the server-name *pointer* (no copy) and reads it later on the tcpip thread —
  the string passed to `configTime()` must be a static/immortal buffer, never a temporary.
- `parseDashboard()` is merge-style: websocket deltas contain only some widgets, so only update
  fields that are actually present. Widgets used: `CMMachineStatus`, `CMPreBrewing`,
  `CMCoffeeBoiler`, `CMSteamBoilerLevel`, `CMBackFlush`.
- Commands are queued from the UI (`lmRequestBackflush`) and dispatched by liveTask once signed
  in; queued commands must be cancellable (`lmCancelBackflush`) when the UI stops waiting.

## Conventions

- Use `LOGF`/`LOGLN` (log.h) for all logging — they also feed the on-screen boot log. Never LOG
  from inside `bootlog.cpp` (recursion).
- Button geometry (config.h): `BTN_A_Y`/`BTN_B_Y` = confirm modals + backflush page;
  `DEV_BTN_A/B/C_Y` = the three dev-actions buttons. Touch hit-boxes get a +12 px margin.
- The screen mockups in `docs/screenshots/` are generated by `tools/render_mockups.py` (needs
  Pillow) and mirror `display.cpp` — update the script and regenerate when screens change.
- Secrets flow: `tools/preflight/.env` → `tools/gen_secrets.py` → `firmware/include/secrets.h`
  (git-ignored). Never commit credentials, keys, or the generated `logo_lm.h`/`machine_off.h`
  (trademarked-artwork derived). Without `secrets.h` the firmware builds demo-only.
- Keep `README.md` (features/layout) and `docs/PRODUCT.md` (screen-by-screen guide) in sync when
  adding screens or gestures.
