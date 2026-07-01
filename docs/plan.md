# La Marzocco Micra Brew Timer — External T-Display Plan

## Context

You have a La Marzocco **Linea Micra** on the newest firmware (Gateway v5+) and want a small
external display (LilyGO **T-Display-S3-AMOLED 1.43"**) that shows the live shot/brew timer,
exactly like the official La Marzocco Home app does when you start a brew.

Two facts from research shape the entire approach:

1. **No local API anymore.** La Marzocco removed local network/websocket access in Gateway v5
   (early 2025). The only way to read brew state is via their **cloud** API at
   `https://lion.lamarzocco.io/api/customer-app`.
2. **App coexistence is solved by polling, not websockets.** The cloud allows only **one active
   websocket** per machine — if our device opened one, it would kick the official app off (or get
   kicked). But **REST polling does *not* trigger this mutual exclusion.** So the display will
   **poll the dashboard endpoint ~1×/second** and compute the timer locally. The app keeps its
   websocket, and you can still log in to adjust machine parameters at any time — which is your
   stated requirement.

Intended outcome: a self-contained ESP32-S3 device on your WiFi that, when you pull a shot, shows a
large counting-up timer (and goes idle/clock otherwise), with the official app fully usable.

---

## Hardware

- **Board:** LilyGO T-Display-S3-AMOLED 1.43" (product code **H741**)
  - ESP32-S3-R8, 16MB flash, **8MB PSRAM** (plenty for TLS + framebuffer)
  - **466×466 round AMOLED**, driver **SH8601** over **QSPI** (note: *not* ST7789, so **TFT_eSPI
    does not work** here)
  - FT3168 capacitive touch (I2C), PCF8563 RTC, USB-C
- Confirm on arrival it is the **1.43"** (466×466), not the 1.64" (280×456, different CO5300 driver).

---

## Toolchain & Libraries

- **PlatformIO** (Arduino framework) — more reliable than Arduino IDE for S3 + PSRAM flags.
- Display: use the board's dedicated repo **`Xinyuan-LilyGO/T-Display-S3-AMOLED-1.43-1.75`** as the
  reference for pin config / panel init. Graphics via **Arduino_GFX** (`moononournation/Arduino_GFX`);
  optionally **LVGL 8.3.x** if we want a polished circular gauge UI. Start with Arduino_GFX for the
  numerals to keep memory simple; add LVGL only if the UI design calls for it.
- Networking: **`WiFiClientSecure`** (TLS, built into ESP32 core), **`bblanchon/ArduinoJson`** (v7)
  for parsing the dashboard JSON.
- Crypto for request signing: **mbedTLS** (bundled with ESP-IDF/Arduino-ESP32) for ECDSA-secp256r1,
  SHA-256, HMAC-SHA256, base64.
- Time: **NTP** (`configTime`) — required for TLS cert validity, for the `X-Timestamp` signature
  header, and to compute elapsed time from the server's `brewingStartTime` epoch.

Example `platformio.ini`:
```ini
[env:t-display-amoled-143]
platform = espressif32
board = lilygo-t-display-s3   ; generic S3 base; pins set in code from the 1.43" repo
framework = arduino
monitor_speed = 115200
build_flags =
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DLCD_USB_QSPI_DREVER=1
lib_deps =
    moononournation/GFX Library for Arduino
    bblanchon/ArduinoJson
    ; lvgl/lvgl @ ^8.3.5   ; only if we choose LVGL for the UI
```

---

## La Marzocco Cloud — protocol details (confirmed from `pylamarzocco` source)

- Base: `https://lion.lamarzocco.io/api/customer-app`
- **Sign in:** `POST /auth/signin` with body `{username, password}` → returns
  `{accessToken, refreshToken, expiresAt}`. Token lifetime ~1h; refresh via
  `POST /auth/refreshtoken` with `{username, refreshToken}`.
- **Signed headers required on requests:** `X-App-Installation-Id`, `X-Timestamp` (ms),
  `X-Nonce` (uuid), `X-Request-Signature` (ECDSA-SHA256 over the installation-id/nonce/timestamp/proof,
  using a per-install secp256r1 key). The signing + custom "proof" derivation lives in
  `pylamarzocco/util/_authentication.py` — this must be ported faithfully and is the riskiest part.
- **Read brew state (the timer):**
  `GET /things/{serialNumber}/dashboard` (Bearer token + signed headers). Response contains a
  `widgets` array; the relevant widget is `code == "CMMachineStatus"` with:
  - `output.status` — `"Brewing"` while a shot is pulling (else `PoweredOn`/`StandBy`/`Off`)
  - `output.brewingStartTime` — epoch **ms** when the shot started (`null` when not brewing)
  - top-level `connected` — cloud connectivity
- **Elapsed time** is computed on-device: `elapsed = now() - brewingStartTime`. The display counts
  up locally between polls for smoothness; each poll re-syncs against `brewingStartTime` so it never
  drifts.

---

## De-risking strategy (do this BEFORE writing firmware)

The auth signing is the only hard/uncertain piece. Validate it in Python first, on a PC:

1. `pip install pylamarzocco`; run its example to sign in with your LM Home credentials and call the
   dashboard endpoint. Confirm the serial number and that, **while pulling a shot**,
   `CMMachineStatus.output.status == "Brewing"` and `brewingStartTime` is populated.
2. Confirm REST polling (loop the dashboard GET ~1×/s) **does not** disconnect the official app —
   open the app, poll from Python, verify the app stays connected and parameter changes still work.
3. Capture one real signed request/response (e.g. via logging) to use as a golden test vector for
   the C++ signing port.
4. **Generate the installation EC keypair once on the PC** (via pylamarzocco's installation-key
   helper) and register it. We embed the **installation id + private key** into the device (NVS or
   a build-time secret) so the firmware only needs to *sign* — it never has to generate/register a
   key on-device. This sidesteps the trickiest crypto.

---

## Firmware structure (ESP32-S3)

Build incrementally; each step independently verifiable:

1. **Display bring-up** — port pin config from the 1.43"-1.75 repo, init SH8601 over QSPI with
   Arduino_GFX, draw a large centered "00:00" on the round panel. Confirm orientation/centering.
2. **WiFi + NTP** — connect to WiFi (credentials in NVS/config), `configTime` for accurate epoch.
3. **TLS + sign-in** — `WiFiClientSecure` to `lion.lamarzocco.io`, port the signing module
   (mbedTLS ECDSA/SHA-256/HMAC) using the embedded install key, perform `/auth/signin`, store tokens;
   implement refresh before `expiresAt`. Validate signatures against the Python golden vector.
4. **Dashboard poll loop** — every ~1s GET the dashboard, parse with ArduinoJson, extract
   `status` + `brewingStartTime`. Handle 401 → refresh token; handle transient errors with backoff.
5. **Timer UI / state machine**
   - *Idle* (`status != "Brewing"`): show a dim clock or LM logo.
   - *Brewing*: show big seconds (e.g. `M:SS` / `12.3s`), counting up locally from `brewingStartTime`,
     re-synced each poll. The round screen suits a circular progress ring around the numerals.
   - *Just stopped*: freeze final shot time for a few seconds before returning to idle.
6. **Polish** — brightness/dim, reconnect logic, optional touch to wake or switch views.

Key reused references: `Xinyuan-LilyGO/T-Display-S3-AMOLED-1.43-1.75` (panel init/pins),
`pylamarzocco/clients/_cloud.py` + `util/_authentication.py` (auth/signing to port),
`moononournation/Arduino_GFX` (rendering).

---

## Verification (end-to-end)

- **Python pre-flight (step above):** dashboard shows `Brewing` + `brewingStartTime` during a real
  shot; app stays connected during polling.
- **Signing parity:** C++ signing reproduces the Python golden-vector signature byte-for-byte.
- **On-device:** pull a real shot → display starts counting within ~1s of the paddle, tracks the
  app's timer within ~1s, and freezes/zeros correctly when the shot ends.
- **Coexistence:** with the display running, open the LM Home app, change a parameter (e.g. target
  temp / pre-infusion) and confirm it succeeds — the display keeps updating throughout.
- **Stability:** leave it running across token refresh (>1h) and a WiFi drop; confirm it recovers.

---

## Risks & notes

- **Auth signing is reverse-engineered and undocumented.** The `proof`/signature derivation must be
  ported exactly from `pylamarzocco/util/_authentication.py`; mismatches → 401s. Pin the
  pylamarzocco version you port from.
- **Cloud-dependent & subject to change.** La Marzocco has been tightening third-party access; the
  API may change and require updates. Polling cadence: start at ~2–5s and tighten toward ~1s while
  watching for `429`/throttling (no published rate limit found).
- **Credentials on device.** Your LM password (or the derived tokens) and the install private key
  live on the ESP32 — store in NVS, don't commit secrets to the repo.
- This is a new standalone project; it does not touch the current working directory's marketplace code.
