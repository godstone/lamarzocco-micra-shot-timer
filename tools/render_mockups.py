#!/usr/bin/env python3
"""Render stylized mockups of each firmware screen to round 466x466 PNGs.

Replicates the geometry, colors and fonts of firmware/src/display.cpp (whose drawing
coordinates equal the viewer's coordinates after the software rotation), so the images
closely match what's on the panel without photographing it. Output -> docs/screenshots/.

Requirements:  pip install Pillow
Font:  Luckiest Guy (Apache-2.0). Auto-downloaded to a cache dir on first run, or set
       LUCKIEST_GUY_TTF to a local copy.

Usage:  python tools/render_mockups.py
"""
import math, os, sys, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "docs", "screenshots")

W = H = 466
CX = CY = W // 2

FONT_URL = ("https://raw.githubusercontent.com/google/fonts/main/"
            "apache/luckiestguy/LuckiestGuy-Regular.ttf")

def find_font():
    p = os.environ.get("LUCKIEST_GUY_TTF")
    if p and os.path.exists(p):
        return p
    cache = os.path.join(HERE, ".cache")
    os.makedirs(cache, exist_ok=True)
    p = os.path.join(cache, "LuckiestGuy-Regular.ttf")
    if not os.path.exists(p):
        print("downloading Luckiest Guy (Apache-2.0)...")
        urllib.request.urlretrieve(FONT_URL, p)
    return p

# a monospace stand-in for the GFX built-in 5x7 font
def find_mono():
    for p in ("/System/Library/Fonts/Menlo.ttc",
              "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
              "/Library/Fonts/Courier New.ttf"):
        if os.path.exists(p):
            return p
    return None

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow is required:  pip install Pillow")

LG = find_font()
MONO = find_mono()
os.makedirs(OUT, exist_ok=True)

# --- palettes (RGB, straight from the THEMES table in display.cpp) ---
# 6 schemes (Micra machine colors) x dark/light; five themed roles per palette
# (bg/text/dim/accent/accent2). Status colors are fixed semantics per mode.
THEMES = {  # name: (dark, light), each (bg, text, dim, accent, accent2)
    "RED":    (((0,0,0),(245,245,245),(90,90,90),(226,28,44),(255,140,120)),
               ((250,245,243),(36,20,22),(148,128,126),(192,0,26),(232,88,76))),
    "YELLOW": (((0,0,0),(247,243,232),(104,96,72),(240,180,36),(255,222,120)),
               ((251,247,236),(40,32,16),(158,146,114),(196,140,10),(140,102,8))),
    "BLUE":   (((0,0,0),(236,246,250),(80,100,110),(134,196,222),(66,142,178)),
               ((242,248,251),(18,36,46),(128,150,161),(44,122,156),(100,170,198))),
    "WHITE":  (((0,0,0),(245,245,245),(92,92,92),(255,255,255),(198,190,176)),
               ((255,255,255),(26,26,26),(156,156,156),(70,70,70),(122,118,110))),
    "GRAY":   (((0,0,0),(240,243,245),(84,90,96),(186,192,199),(126,135,143)),
               ((242,244,246),(27,33,38),(138,147,155),(84,94,102),(128,138,146))),
    "BLACK":  (((0,0,0),(232,232,232),(70,70,70),(214,214,214),(128,128,128)),
               ((236,236,236),(16,16,16),(128,128,128),(10,10,10),(84,84,84))),
}
SCHEME_ORDER = list(THEMES)  # RED, YELLOW, BLUE, WHITE, GRAY, BLACK (swatch grid order)

CREMA=(175,115,60); LM_RED=(213,0,28)

def set_theme(scheme="RED", dark=True):
    """Load a scheme+mode palette into the module globals (mirrors applyTheme())."""
    global BG, FG, DIM, ACCENT, ACCENT2, OK, WARN, ERR, DARK_MODE
    BG, FG, DIM, ACCENT, ACCENT2 = THEMES[scheme][0 if dark else 1]
    OK   = (120,200,165) if dark else (20,135,88)
    WARN = (255,170,40)  if dark else (190,115,0)
    ERR  = (230,60,50)   if dark else (196,36,28)
    DARK_MODE = dark

set_theme()  # default: RED, dark

BTN_X, BTN_W, BTN_H, BTN_A_Y, BTN_B_Y = 96, 274, 66, 156, 258  # config.h
DEV_BTN_A_Y, DEV_BTN_B_Y, DEV_BTN_C_Y = 120, 213, 306  # dev actions page (3 buttons)
SW_W, SW_H = 150, 64  # theme swatches (THEME_SW_* in config.h)
SW_XS, SW_YS = (70, 246), (130, 206, 282)
IDLE_PAGES = 3  # public carousel: status, stats, backflush
DEV_PAGES = 6   # settings area: info, demo/bootlog, device, theme, dark/light, setup QR

def lg(px):   return ImageFont.truetype(LG, px)
def mono(px): return ImageFont.truetype(MONO, px) if MONO else ImageFont.load_default()

def lerp(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(int(a[i] + (b[i]-a[i])*t) for i in range(3))

def centered(d, text, cy, font, color):
    d.text((CX, cy), text, font=font, fill=color, anchor="mm")

def classic(d, text, cy, size, color):        # GFX built-in font ~ 8*size px cell
    d.text((CX, cy), text, font=mono(int(8.0*size)), fill=color, anchor="mm")

def classic_left(d, text, x, y, size, color):
    d.text((x, y), text, font=mono(int(8.0*size)), fill=color, anchor="la")

def progress_arc(d, r_out, r_in, frac, color):  # clockwise from 12 o'clock
    if frac <= 0: return
    frac = min(1.0, frac)
    r = (r_out + r_in) / 2.0
    d.arc([CX-r, CY-r, CX+r, CY+r], -90, -90+360*frac, fill=color, width=int(round(r_out-r_in)))

def page_dots(d, active, count):
    if count <= 1: return
    y, gap, r = 432, 24, 5
    startx = CX - (count-1)*gap//2
    for i in range(count):
        x = startx + i*gap
        if i == active: d.ellipse([x-r, y-r, x+r, y+r], fill=FG)
        else:           d.ellipse([x-r, y-r, x+r, y+r], outline=DIM)

def conn_icon(d, mode):
    if mode == 0: return
    cx, y = CX, 40
    if mode == 1:  # websocket lightning bolt
        d.polygon([(cx+3, y-8), (cx-4, y+2), (cx+1, y+1)], fill=OK)
        d.polygon([(cx-3, y+8), (cx+4, y-2), (cx-1, y-1)], fill=OK)
    else:          # cloud
        d.ellipse([cx-11, y-3, cx-1, y+7], fill=DIM)
        d.ellipse([cx+1, y-3, cx+11, y+7], fill=DIM)
        d.ellipse([cx-6, y-9, cx+6, y+3], fill=DIM)
        d.rectangle([cx-7, y+1, cx+7, y+6], fill=DIM)

def button(d, x, y, w, h, label, color):
    d.rounded_rectangle([x, y, x+w, y+h], radius=14, outline=color, width=3)
    classic(d, label, y + h//2, 3, color)

def fmt_elapsed(seconds):
    total = int(seconds); mins = total//60; secs = total%60
    tenths = int(seconds*10) % 10
    return f"{mins}:{secs:02d}.{tenths}" if mins > 0 else f"{secs}.{tenths}"

def fmt_mmss(secs):
    secs = max(0, secs)
    return f"{secs//60}:{secs%60:02d}"

def canvas(): return Image.new("RGB", (W, H), BG)

def save(img, name):
    out = img.convert("RGBA")
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, W-1, H-1], fill=255)  # round AMOLED
    out.putalpha(mask)
    out.save(os.path.join(OUT, name))
    print("wrote", name)

def render_timer(name, seconds, pre=0.0, steam_frac=0.35, conn=1):
    img = canvas(); d = ImageDraw.Draw(img)
    COFFEE_FILL, OVEREX = 30.0, 15.0
    in_pre = (pre > 0.1 and seconds < pre)
    shot = max(0.0, (seconds - pre) if pre > 0.1 else seconds)
    frac = min(1.0, shot / COFFEE_FILL)
    base = int(H * (1.0 - frac))
    over = max(0.0, min(1.0, (shot - COFFEE_FILL) / OVEREX))
    body = lerp((70,35,14), (150,122,96), over)
    crema = lerp((175,115,60), (188,168,142), over)
    if not in_pre:
        for x in range(W):
            sy = base + int(5.0*math.sin(x*0.045 + seconds*3.0))
            sy = max(0, sy)
            if sy >= H: continue
            d.line([(x, sy), (x, H-1)], fill=body)
            cband = 10 if (sy+10 < H) else (H-sy)
            if cband > 0: d.line([(x, sy), (x, sy+cband-1)], fill=crema)
        if base > 2:
            stream = lerp((120,70,34), (150,122,96), over)
            phase = int(seconds*150.0)
            for s in range(2):
                dr = -1.0 if s == 0 else 1.0; impactX = CX
                for y in range(base):
                    t = y/base
                    spread = 16.0*(1-t) + 5.0*t
                    wob = math.sin(y*0.07 + seconds*7.0 + s*math.pi)*(1.0+3.0*t)
                    sx = int(CX + dr*spread + wob)
                    half = 1 if t < 0.5 else 2
                    hi = (((y-phase) % 13)+13) % 13 < 4
                    d.line([(sx-half, y), (sx+half, y)], fill=(crema if hi else stream))
                    impactX = sx
                d.line([(impactX-7, base-1), (impactX+7, base-1)], fill=crema)
                d.line([(impactX-4, base-2), (impactX+4, base-2)], fill=crema)
    if in_pre:
        progress_arc(d, 231, 223, seconds/pre, ACCENT2)
        phase = int(seconds*38.0)
        for sidx in range(2):
            sx = CX + (-10 if sidx == 0 else 10)
            for y in range(CY+30):
                if (((y-phase) % 40)+40) % 40 < 3: d.line([(sx-1, y), (sx+1, y)], fill=ACCENT2)
        classic(d, "PRE-INFUSION", CY-78, 2, ACCENT2)
    if steam_frac >= 0 and not in_pre:
        progress_arc(d, 231, 223, steam_frac, ACCENT2)
    if over <= 0:    tcol = FG
    elif over < 0.5: tcol = lerp((245,245,245),(255,170,40), over/0.5)
    else:            tcol = lerp((255,170,40),(230,45,30), (over-0.5)/0.5)
    centered(d, fmt_elapsed(seconds), CY, lg(96), tcol)
    conn_icon(d, conn); save(img, name)

def render_status(name, coffee_frac, coffee_in, steam_frac, steam_in, conn=1):
    img = canvas(); d = ImageDraw.Draw(img)
    progress_arc(d, 231, 223, 1.0-coffee_frac, ACCENT)
    progress_arc(d, 219, 211, 1.0-steam_frac, ACCENT2)
    classic(d, "MACHINE", 116, 3, ACCENT)
    centered(d, "READY" if coffee_frac >= 1 else fmt_mmss(coffee_in), 166, lg(68),
             OK if coffee_frac >= 1 else WARN)
    d.line([(123, 233), (343, 233)], fill=DIM)
    classic(d, "STEAM", 286, 3, ACCENT2)
    centered(d, "READY" if steam_frac >= 1 else fmt_mmss(steam_in), 336, lg(68),
             OK if steam_frac >= 1 else WARN)
    page_dots(d, 0, IDLE_PAGES); conn_icon(d, conn); save(img, name)

def render_stats(name, today, total, conn=2):
    img = canvas(); d = ImageDraw.Draw(img)
    d.ellipse([4, 4, W-5, H-5], outline=ACCENT)
    d.ellipse([7, 7, W-8, H-8], outline=tuple(int(c*0.45) for c in ACCENT))
    classic(d, "SHOTS TODAY", 112, 2, DIM)
    centered(d, str(today), 188, lg(96), ACCENT)
    n = min(9, today); spacing = 26; startx = CX - (n-1)*spacing//2
    for i in range(n):
        x = startx + i*spacing; d.ellipse([x-5, 245, x+5, 255], fill=CREMA)
    d.line([(123, 286), (343, 286)], fill=DIM)
    classic(d, "LIFETIME", 320, 2, DIM)
    centered(d, str(total), 374, lg(68), FG)
    page_dots(d, 1, IDLE_PAGES); conn_icon(d, conn); save(img, name)

def render_machine_off(name):
    img = canvas(); d = ImageDraw.Draw(img)
    r = 66
    d.arc([CX-r, 150-r, CX+r, 150+r], -60, 240, fill=DIM, width=8)
    d.line([(CX, 150-r-6), (CX, 150-6)], fill=DIM, width=8)
    classic(d, "MACHINE OFF", 360, 4, DIM)
    page_dots(d, 0, IDLE_PAGES); save(img, name)

def render_settings_theme(name, active=0):
    img = canvas(); d = ImageDraw.Draw(img)
    centered(d, "THEME", 88, lg(68), ACCENT)
    for i, label in enumerate(SCHEME_ORDER):
        accent = THEMES[label][0 if DARK_MODE else 1][3]
        x, y = SW_XS[i % 2], SW_YS[i // 2]
        if i == active:
            d.rounded_rectangle([x, y, x+SW_W, y+SW_H], radius=14, fill=accent)
            d.text((x+SW_W//2, y+SW_H//2), label, font=mono(16), fill=BG, anchor="mm")
        else:
            d.rounded_rectangle([x, y, x+SW_W, y+SW_H], radius=14, outline=accent, width=3)
            d.text((x+SW_W//2, y+SW_H//2), label, font=mono(16), fill=accent, anchor="mm")
    classic(d, "machine colors", 372, 2, DIM)
    page_dots(d, 3, DEV_PAGES); save(img, name)

def render_settings_mode(name, dark=True):
    img = canvas(); d = ImageDraw.Draw(img)
    centered(d, "DISPLAY", 88, lg(68), ACCENT)
    if dark:
        d.rounded_rectangle([BTN_X, BTN_A_Y, BTN_X+BTN_W, BTN_A_Y+BTN_H], radius=14, fill=FG)
        classic(d, "DARK", BTN_A_Y + BTN_H//2, 3, BG)
        button(d, BTN_X, BTN_B_Y, BTN_W, BTN_H, "LIGHT", DIM)
    else:
        button(d, BTN_X, BTN_A_Y, BTN_W, BTN_H, "DARK", DIM)
        d.rounded_rectangle([BTN_X, BTN_B_Y, BTN_X+BTN_W, BTN_B_Y+BTN_H], radius=14, fill=FG)
        classic(d, "LIGHT", BTN_B_Y + BTN_H//2, 3, BG)
    classic(d, "dark saves power (AMOLED)", 372, 2, DIM)
    page_dots(d, 4, DEV_PAGES); save(img, name)

def render_backflush(name, days_ago=2):
    img = canvas(); d = ImageDraw.Draw(img)
    centered(d, "BACKFLUSH", 100, lg(68), ACCENT2)
    button(d, BTN_X, BTN_A_Y, BTN_W, BTN_H, "START", OK)
    classic(d, "cleaned %d days ago" % days_ago, 272, 2, DIM)
    page_dots(d, 2, IDLE_PAGES); conn_icon(d, 1); save(img, name)

def render_dev_actions(name, demo_on=True, bootlog_on=False):
    img = canvas(); d = ImageDraw.Draw(img)
    centered(d, "DEV", 64, lg(68), ACCENT)
    button(d, BTN_X, DEV_BTN_A_Y, BTN_W, BTN_H, "DEMO ON" if demo_on else "DEMO OFF",
           OK if demo_on else DIM)
    button(d, BTN_X, DEV_BTN_B_Y, BTN_W, BTN_H, "BOOTLOG ON" if bootlog_on else "BOOTLOG OFF",
           OK if bootlog_on else DIM)
    page_dots(d, 1, DEV_PAGES); save(img, name)

def render_dev_display(name, rot=1):
    img = canvas(); d = ImageDraw.Draw(img)
    centered(d, "DEV", 64, lg(68), ACCENT)
    cable = ["left", "bottom", "right", "top"]
    classic(d, "%d deg - cable %s" % (rot * 90, cable[rot & 3]), 108, 2, DIM)
    button(d, BTN_X, DEV_BTN_A_Y, BTN_W, BTN_H, "ROTATE 90", ACCENT2)
    button(d, BTN_X, DEV_BTN_B_Y, BTN_W, BTN_H, "SETUP PORTAL", WARN)
    button(d, BTN_X, DEV_BTN_C_Y, BTN_W, BTN_H, "RESET DEVICE", ERR)
    page_dots(d, 2, DEV_PAGES); save(img, name)

def render_settings_setup(name, ip="192.168.1.53"):
    img = canvas(); d = ImageDraw.Draw(img)
    centered(d, "SETUP", 88, lg(68), ACCENT)
    url = "http://%s/param" % ip
    import qrcode as qrlib
    qr = qrlib.QRCode(version=3, error_correction=qrlib.constants.ERROR_CORRECT_M, box_size=1,
                      border=0)
    qr.add_data(url); qr.make(fit=False)
    m = qr.get_matrix(); n = len(m); scale = 6; quiet = 3 * scale
    size = n * scale; x0 = (W - size) // 2; y0 = 150
    d.rectangle([x0 - quiet, y0 - quiet, x0 + size + quiet, y0 + size + quiet], fill=(255,255,255))
    for y in range(n):
        for x in range(n):
            if m[y][x]:
                d.rectangle([x0 + x*scale, y0 + y*scale, x0 + (x+1)*scale - 1,
                             y0 + (y+1)*scale - 1], fill=(0,0,0))
    classic(d, url, 372, 2, FG)
    classic(d, "scan to set up the machine", 400, 2, DIM)
    page_dots(d, 5, DEV_PAGES); save(img, name)

def render_dev_info(name):
    img = canvas(); d = ImageDraw.Draw(img)
    centered(d, "DEV", 70, lg(68), ACCENT)
    x, y, dy = 78, 116, 25
    rows = [("demo:   ON", OK), ("wifi:   up", OK),
            ("ip:     192.168.1.42", FG), ("signin: yes", OK),
            ("cloud:  connected", OK), ("status: PoweredOn", FG),
            ("heap:   8231044", DIM), ("touch:  0", DIM), ("err:    none", OK)]
    for i, (txt, c) in enumerate(rows):
        classic_left(d, txt, x, y + i*dy, 2, c)
    page_dots(d, 0, DEV_PAGES); save(img, name)

def render_reset_confirm(name):
    img = canvas(); d = ImageDraw.Draw(img)
    classic(d, "RESET DEVICE?", 92, 3, ERR)
    classic(d, "clears WiFi + LM account", 132, 2, DIM)
    button(d, BTN_X, BTN_A_Y, BTN_W, BTN_H, "CONFIRM", ERR)
    button(d, BTN_X, BTN_B_Y, BTN_W, BTN_H, "CANCEL", FG)
    save(img, name)

if __name__ == "__main__":
    # default theme (RED, dark)
    render_timer("01-brewing.png", 17.4, pre=0.0, steam_frac=0.35, conn=1)
    render_timer("02-preinfusion.png", 2.4, pre=4.0, conn=1)
    render_timer("03-overextraction.png", 40.0, pre=0.0, steam_frac=-1, conn=1)
    render_status("04-readiness.png", 0.45, 42, 1.0, 0, conn=1)
    render_stats("05-stats.png", 3, 1287, conn=2)
    render_machine_off("06-machine-off.png")
    render_dev_actions("07-dev-actions.png", demo_on=True)
    render_dev_info("08-dev-info.png")
    render_reset_confirm("09-reset-confirm.png")
    render_dev_display("10-dev-display.png")
    render_settings_theme("11-settings-theme.png", active=0)
    render_settings_mode("12-settings-mode.png", dark=True)
    render_backflush("13-backflush.png", days_ago=2)
    render_settings_setup("14-settings-setup.png")
    print("done ->", OUT)
