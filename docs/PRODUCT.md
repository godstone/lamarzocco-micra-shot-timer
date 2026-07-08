# Product Guide — La Marzocco Micra External Display

A round 1.43" AMOLED companion display for the La Marzocco Linea Micra. This guide covers what each
screen shows and how to interact with the device. (For build/flash and architecture, see the
[README](../README.md) and [plan.md](plan.md).)

## At a glance

| State | Screen |
|-------|--------|
| Power on | La Marzocco lion logo (brand red) for ~2s — or the boot-log console if enabled |
| Machine off / unreachable | Machine illustration + "MACHINE OFF" |
| Warming up | Machine + steam heat-up rings with countdowns |
| Idle & ready | Swipe carousel: status / shot stats / backflush / theme / dark-light |
| Pulling a shot | Pre-infusion phase, then coffee fills bottom→top with centered timer |
| Just finished | Final shot time held ~15s, then back to standby |
| 15 min without activity | Screen fully dark (standby) — tap or machine activity wakes it |
| No known WiFi in range | "WIFI SETUP — join LaMarzocco-Display" (captive portal; the device remembers up to 5 networks and auto-connects to whichever is visible). A status line shows progress: waiting for phone → phone connected → connected! |

## Screens

### 1. Startup logo
The La Marzocco lion in brand red on black, shown briefly at boot.

### 2. Machine off
Shown whenever the machine is powered off or not reachable on the cloud. A white illustration of the
machine with the label **MACHINE OFF**.

### 3. Heat-up rings (machine / steam)
While the boilers warm up, two arcs around the edge **deplete** clockwise from full — full when
cold, gone when ready:

- **MACHINE** (coffee boiler) — outer ring + countdown, in the theme's **accent** color.
- **STEAM** (steam boiler) — inner ring + countdown, in the theme's **secondary accent**.

The centered labels show either the remaining `M:SS` (amber) or **READY** (green). Labels are
colored to match their ring.

### 4. Shot timer (brewing)
- **Pre-infusion phase:** for the first N seconds of the shot (N = the pre-brew/pre-infusion time you
  set in the app, read automatically from the machine), a pre-infusion ring fills, with slow
  low-pressure drips and a "PRE-INFUSION" label. The cup stays empty until pre-infusion ends.
- **Extraction:** two espresso streams then pour in from the top — wavering, converging toward
  center, with a crema shimmer and a splash where they meet the surface. The cup fills bottom→top
  over ~30s with a crema band on a gently waving surface. The **elapsed time** (total, including
  pre-infusion) counts up in the center.
- **Over-extraction:** past ~30s the coffee washes out toward a pale "watery" tone and the timer
  shifts **white → amber → red** as a warning that the shot is running long.
- **Steam-while-pulling:** if you start a shot before the steam boiler is ready, the **steam
  ring** appears around the edge so you can see at a glance when steam will be set.
- **After the shot:** the final time and filled cup are held for ~15 seconds, then the display
  returns to standby.

### 5. Shot stats
- **SHOTS TODAY** — a large hero number with a row of crema dots (one per shot).
- **LIFETIME** — total shots pulled.
- Framed by a decorative double ring in the theme's accent color.

### 6. Backflush (cleaning)
The third idle page. Tap **START** (only available while the machine is on), insert the blind
filter + detergent, and **CONFIRM** — the display sends the machine's backflush command and shows a
live spinner (**STARTING**, then **CLEANING** while the cycle runs), followed by a brief **DONE**
screen when the machine reports the cycle finished. When idle, the page shows when the machine was
last cleaned ("cleaned N days ago").

### 7. Theme settings
The fourth idle page. Six tappable swatches — one **color scheme per Linea Micra machine color**:
**RED**, **YELLOW**, **BLUE**, **WHITE**, **GRAY** (covers the Silver Satin / Stainless Steel /
Satin metal finishes) and **BLACK**. Each swatch is drawn in that scheme's own accent color; the
active scheme is filled. A scheme restyles the whole UI (background, text, accents, rings, titles)
and is saved on the device. Status colors are **not** themed — green always means ready, amber
warming, red error/over-extraction — so readings stay glanceable in every scheme.

### 8. Display settings (dark / light)
The fifth idle page. Every scheme has a **dark** and a **light** palette; tap **DARK** or
**LIGHT** to switch (saved). Dark keeps the background pure black, which on an AMOLED means unlit
pixels — less power and no burn-in — so it's the recommended default. Standby always turns the
panel fully black regardless of mode.

### 9. Screen standby
After **15 minutes** with no touch and no machine events, the screen goes **fully dark** — on an
AMOLED that means the pixels are off, which protects the panel from burn-in and extends its life.
It wakes instantly on **any touch** (the waking tap is swallowed, so it can't accidentally press a
button) or on **any machine activity**: turning the machine on or off, a brew starting (the display
wakes straight into the shot timer), a boiler reaching ready, a backflush cycle, or the WiFi setup
portal opening. The timeout is `STANDBY_AFTER_MS` in `config.h`.

### 10. Boot-log console (optional)
When the **BOOTLOG** dev toggle is on, every startup skips the logo and instead shows the boot log
live on screen (display/touch init, WiFi, cloud sign-in, websocket) — handy for debugging
connection problems without a computer. Long lines wrap; tap anywhere to continue to the normal UI
(it auto-continues after 2 minutes). The toggle persists across reboots until switched off.

## Gestures (touch)

The panel is single-touch, so all gestures use one finger.

| Gesture | Action |
|---------|--------|
| **Swipe** | Move between screens (right-to-left = next, like flicking the screen away). Page dots at the bottom show which screen you're on; the carousel loops. |
| **Press & hold (~1.5s)** | Open **develop mode** (diagnostics + actions). |
| **Double-tap** | Close develop mode. |
| **Tap** | Buttons (backflush START/CONFIRM/CANCEL, theme swatches, DARK/LIGHT, dev actions); leave the boot-log console. |

### Page dots
Gallery-style dots near the bottom indicate the current screen and the total number of screens, like
a phone photo gallery. The filled dot is the current page.

### Connection icon
A small icon at the top center shows the live data path: a **lightning bolt** (green) for the
realtime websocket, a **cloud** (muted) for REST polling, nothing when disconnected.

## Develop mode

Press and hold the screen for ~1.5 seconds to open develop mode — three pages, swipe between them,
close with a quick **double-tap**.

**Page 1 — diagnostics** (live values):

- **demo** — ON / OFF (runtime demo toggle, see below)
- **wifi** — connected (has an IP) / down
- **ip** — local IP address
- **signin** — cloud token valid (yes/no)
- **cloud** — machine reachable on the cloud
- **status** — raw machine status (e.g. `Brewing`, `StandBy`, `Off`)
- **heap** — free memory
- **touch** — current touch count (handy to confirm touch works)
- **err** — last error, or `none`

**Page 2 — actions** (three buttons):

- **DEMO ON/OFF** — toggle demo mode.
- **BOOTLOG ON/OFF** — persisted toggle for the boot-log console shown on future startups.
- **RESET DEVICE** — factory reset (confirm modal): clears all remembered WiFi networks, the LM
  installation key, saved stats, the boot-log toggle, the rotation setting, and the theme (back
  to RED, dark), then restarts into the captive portal.

**Page 3 — display**:

- **ROTATE 90** — cycles the screen orientation 0° → 90° → 180° → 270°, applied instantly and
  saved across reboots. Touch input is remapped with it, so buttons and swipes always match what
  you see — just tap until it looks right for how your device is mounted (the page shows which
  side the USB cable ends up on).

## Demo mode

The device ships with **demo mode off** (so it shows MACHINE OFF until live data is wired up). Turn
it **on** from the DEMO button in develop mode to preview the UI:

- In demo mode, the screens become a **manual gallery** of all five screens
  (**Machine off → Heat-up rings → Shot → Stats → Backflush**). Swipe left/right to browse them;
  each animates on its own (rings fill and loop, coffee pours and loops, the cleaning spinner
  spins). The page dots show all five.
- With demo off, the device is **state-driven** — it shows whatever the machine is actually doing.

## Generating image assets

The lion logo (`firmware/src/logo_lm.h`) and machine illustration (`firmware/src/machine_off.h`) are
generated from SVGs and are **git-ignored** (they derive from La Marzocco's trademarked artwork — do
not publish them). To build your own from an SVG (needs `rsvg-convert` + ImageMagick + Python):

```bash
# 1-bpp logo, drawn in one color on the device:
rsvg-convert -w 320 -h 320 logo.svg -o /tmp/l.png
magick /tmp/l.png -alpha extract -threshold 50% -compress none /tmp/l.pgm
#   then pack /tmp/l.pgm into logo_lm.h  (LM_LOGO_W/H + LM_LOGO_BITMAP, MSB-first rows)

# RGB565 machine illustration (white on black, anti-aliased):
rsvg-convert -w 600 -h 600 machine.svg -o /tmp/m.png
magick /tmp/m.png -trim +repage -resize 260x260 -background none -gravity center -extent 260x260 \
       -alpha extract -compress none /tmp/m.pgm
#   then pack /tmp/m.pgm into machine_off.h  (MACHINE_OFF_W/H + MACHINE_OFF_BITMAP, RGB565)
```

(See the git history / ask for the exact Python packers.) A text-only fallback can be added so the
firmware compiles without these headers — open an issue if you want it.

## Notes & limitations

- **Realtime via websocket, REST fallback.** The device subscribes to the cloud's realtime websocket
  for instant (~1–2s) brew start/stop. The cloud allows only one realtime connection per machine, so
  opening the official app takes over; the display then falls back to (laggier) REST polling and
  reconnects after you close the app. The app always wins when you need it.
- Heat-up times, over-extraction threshold, and the live timer offset are configurable in
  `firmware/include/config.h`.
- The cloud API is reverse-engineered and may change; this is an independent, unofficial project.
- Not affiliated with or endorsed by La Marzocco.
