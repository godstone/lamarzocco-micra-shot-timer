# Product Guide — La Marzocco Micra External Display

A round 1.43" AMOLED companion display for the La Marzocco Linea Micra. This guide covers what each
screen shows and how to interact with the device. (For build/flash and architecture, see the
[README](../README.md) and [plan.md](plan.md).)

## At a glance

| State | Screen |
|-------|--------|
| Power on | La Marzocco lion logo (brand red) for ~2s |
| Machine off / unreachable | Machine illustration + "MACHINE OFF" |
| Warming up | Machine + steam heat-up rings with countdowns |
| Idle & ready | Shot stats (today + lifetime) |
| Pulling a shot | Pre-infusion phase, then coffee fills bottom→top with centered timer |
| Just finished | Final shot time held ~15s, then back to standby |
| WiFi not configured | "WIFI SETUP — join LaMarzocco-Display" (captive portal) |

## Screens

### 1. Startup logo
The La Marzocco lion in brand red on black, shown briefly at boot.

### 2. Machine off
Shown whenever the machine is powered off or not reachable on the cloud. A white illustration of the
machine with the label **MACHINE OFF**.

### 3. Heat-up rings (machine / steam)
While the boilers warm up, two arcs around the edge **deplete** clockwise from full — full when
cold, gone when ready:

- **MACHINE** (coffee boiler) — **pink/purple** outer ring + countdown.
- **STEAM** (steam boiler) — **blue** inner ring + countdown.

The centered labels show either the remaining `M:SS` or **READY** (faded mint). Labels are colored to
match their ring.

### 4. Shot timer (brewing)
- **Pre-infusion phase:** for the first N seconds of the shot (N = the pre-brew/pre-infusion time you
  set in the app, read automatically from the machine), a teal pre-infusion ring fills, with slow
  low-pressure drips and a "PRE-INFUSION" label. The cup stays empty until pre-infusion ends.
- **Extraction:** two espresso streams then pour in from the top — wavering, converging toward
  center, with a crema shimmer and a splash where they meet the surface. The cup fills bottom→top
  over ~30s with a crema band on a gently waving surface. The **elapsed time** (total, including
  pre-infusion) counts up in the center.
- **Over-extraction:** past ~30s the coffee washes out toward a pale "watery" tone and the timer
  shifts **white → amber → red** as a warning that the shot is running long.
- **Steam-while-pulling:** if you start a shot before the steam boiler is ready, the **blue steam
  ring** appears around the edge so you can see at a glance when steam will be set.
- **After the shot:** the final time and filled cup are held for ~15 seconds, then the display
  returns to standby.

### 5. Shot stats
- **SHOTS TODAY** — a large hero number with a row of crema dots (one per shot).
- **LIFETIME** — total shots pulled.
- Framed by a decorative double ring in brand red.

## Gestures (touch)

The panel is single-touch, so all gestures use one finger.

| Gesture | Action |
|---------|--------|
| **Swipe** | Move between screens (right-to-left = next, like flicking the screen away). Page dots at the bottom show which screen you're on; the carousel loops. |
| **Press & hold (~1.5s)** | Open **develop mode** (diagnostics). |
| **Double-tap** | Close develop mode. |
| **Tap the DEMO button** | (in develop mode) toggle demo mode on/off. |

### Page dots
Gallery-style dots near the bottom indicate the current screen and the total number of screens, like
a phone photo gallery. The filled dot is the current page.

## Develop mode

Press and hold the screen for ~1.5 seconds to open a diagnostics overlay. It shows:

- **demo** — ON / OFF (runtime demo toggle, see below)
- **wifi** — connected (has an IP) / down
- **ip** — local IP address
- **signin** — cloud token valid (yes/no)
- **cloud** — machine reachable on the cloud
- **status** — raw machine status (e.g. `Brewing`, `StandBy`, `Off`)
- **heap** — free memory
- **touch** — current touch count (handy to confirm touch works)
- **err** — last error, or `none`
- a tappable **DEMO ON/OFF** button

Close it with a quick **double-tap**.

## Demo mode

The device ships with **demo mode off** (so it shows MACHINE OFF until live data is wired up). Turn
it **on** from the DEMO button in develop mode to preview the UI:

- In demo mode, the screens become a **manual gallery** of all four screens
  (**Machine off → Heat-up rings → Shot → Stats**). Swipe left/right to browse them; each animates on
  its own (rings fill and loop, coffee pours and loops). The page dots show all four.
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
