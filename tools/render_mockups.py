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

# --- palette (RGB, straight from color565() args in display.cpp) ---
BG=(0,0,0); FG=(245,245,245); DIM=(90,90,90); CREMA=(175,115,60)
LM_RED=(213,0,28); MACHINE=(232,70,200); STEAM=(40,130,255); PREINF=(80,210,200)
ST_GREEN=(120,200,165); ST_AMBER=(255,170,40); WS_MINT=(120,210,175)
CLOUD=(140,155,175); DEV_GRN=(40,200,90); DEV_RED=(230,60,50); ACT_AMB=(255,150,60)

BTN_X, BTN_W, BTN_H, BTN_A_Y, BTN_B_Y = 96, 274, 66, 156, 258  # config.h
DEV_BTN_A_Y, DEV_BTN_B_Y, DEV_BTN_C_Y = 120, 213, 306  # dev actions page (3 buttons)

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
    if mode == 1:  # websocket lightning bolt (mint)
        d.polygon([(cx+3, y-8), (cx-4, y+2), (cx+1, y+1)], fill=WS_MINT)
        d.polygon([(cx-3, y+8), (cx+4, y-2), (cx-1, y-1)], fill=WS_MINT)
    else:          # cloud (soft blue-gray)
        d.ellipse([cx-11, y-3, cx-1, y+7], fill=CLOUD)
        d.ellipse([cx+1, y-3, cx+11, y+7], fill=CLOUD)
        d.ellipse([cx-6, y-9, cx+6, y+3], fill=CLOUD)
        d.rectangle([cx-7, y+1, cx+7, y+6], fill=CLOUD)

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
        progress_arc(d, 231, 223, seconds/pre, PREINF)
        phase = int(seconds*38.0)
        for sidx in range(2):
            sx = CX + (-10 if sidx == 0 else 10)
            for y in range(CY+30):
                if (((y-phase) % 40)+40) % 40 < 3: d.line([(sx-1, y), (sx+1, y)], fill=PREINF)
        classic(d, "PRE-INFUSION", CY-78, 2, PREINF)
    if steam_frac >= 0 and not in_pre:
        progress_arc(d, 231, 223, steam_frac, STEAM)
    if over <= 0:    tcol = FG
    elif over < 0.5: tcol = lerp((245,245,245),(255,170,40), over/0.5)
    else:            tcol = lerp((255,170,40),(230,45,30), (over-0.5)/0.5)
    centered(d, fmt_elapsed(seconds), CY, lg(96), tcol)
    conn_icon(d, conn); save(img, name)

def render_status(name, coffee_frac, coffee_in, steam_frac, steam_in, conn=1):
    img = canvas(); d = ImageDraw.Draw(img)
    progress_arc(d, 231, 223, 1.0-coffee_frac, MACHINE)
    progress_arc(d, 219, 211, 1.0-steam_frac, STEAM)
    classic(d, "MACHINE", 116, 3, MACHINE)
    centered(d, "READY" if coffee_frac >= 1 else fmt_mmss(coffee_in), 166, lg(68),
             ST_GREEN if coffee_frac >= 1 else ST_AMBER)
    d.line([(123, 233), (343, 233)], fill=DIM)
    classic(d, "STEAM", 286, 3, STEAM)
    centered(d, "READY" if steam_frac >= 1 else fmt_mmss(steam_in), 336, lg(68),
             ST_GREEN if steam_frac >= 1 else ST_AMBER)
    page_dots(d, 0, 2); conn_icon(d, conn); save(img, name)

def render_stats(name, today, total, conn=2):
    img = canvas(); d = ImageDraw.Draw(img)
    d.ellipse([4, 4, W-5, H-5], outline=LM_RED)
    d.ellipse([7, 7, W-8, H-8], outline=(95,20,24))
    classic(d, "SHOTS TODAY", 112, 2, DIM)
    centered(d, str(today), 188, lg(96), LM_RED)
    n = min(9, today); spacing = 26; startx = CX - (n-1)*spacing//2
    for i in range(n):
        x = startx + i*spacing; d.ellipse([x-5, 245, x+5, 255], fill=CREMA)
    d.line([(123, 286), (343, 286)], fill=DIM)
    classic(d, "LIFETIME", 320, 2, DIM)
    centered(d, str(total), 374, lg(68), FG)
    page_dots(d, 1, 2); conn_icon(d, conn); save(img, name)

def render_machine_off(name):
    img = canvas(); d = ImageDraw.Draw(img)
    r = 66
    d.arc([CX-r, 150-r, CX+r, 150+r], -60, 240, fill=DIM, width=8)
    d.line([(CX, 150-r-6), (CX, 150-6)], fill=DIM, width=8)
    classic(d, "MACHINE OFF", 360, 4, DIM)
    page_dots(d, 0, 2); save(img, name)

def render_dev_actions(name, demo_on=True, bootlog_on=False):
    img = canvas(); d = ImageDraw.Draw(img)
    centered(d, "DEV", 64, lg(68), LM_RED)
    button(d, BTN_X, DEV_BTN_A_Y, BTN_W, BTN_H, "DEMO ON" if demo_on else "DEMO OFF",
           DEV_GRN if demo_on else DIM)
    button(d, BTN_X, DEV_BTN_B_Y, BTN_W, BTN_H, "BOOTLOG ON" if bootlog_on else "BOOTLOG OFF",
           DEV_GRN if bootlog_on else DIM)
    button(d, BTN_X, DEV_BTN_C_Y, BTN_W, BTN_H, "RESET DEVICE", ACT_AMB)
    page_dots(d, 1, 2); save(img, name)

def render_dev_info(name):
    img = canvas(); d = ImageDraw.Draw(img)
    centered(d, "DEV", 70, lg(68), LM_RED)
    x, y, dy = 78, 116, 25
    rows = [("demo:   ON", DEV_GRN), ("wifi:   up", DEV_GRN),
            ("ip:     192.168.1.42", FG), ("signin: yes", DEV_GRN),
            ("cloud:  connected", DEV_GRN), ("status: PoweredOn", FG),
            ("heap:   8231044", DIM), ("touch:  0", DIM), ("err:    none", DEV_GRN)]
    for i, (txt, c) in enumerate(rows):
        classic_left(d, txt, x, y + i*dy, 2, c)
    page_dots(d, 0, 2); save(img, name)

def render_reset_confirm(name):
    img = canvas(); d = ImageDraw.Draw(img)
    classic(d, "RESET DEVICE?", 92, 3, DEV_RED)
    classic(d, "clears WiFi + LM account", 132, 2, DIM)
    button(d, BTN_X, BTN_A_Y, BTN_W, BTN_H, "CONFIRM", DEV_RED)
    button(d, BTN_X, BTN_B_Y, BTN_W, BTN_H, "CANCEL", FG)
    save(img, name)

if __name__ == "__main__":
    render_timer("01-brewing.png", 17.4, pre=0.0, steam_frac=0.35, conn=1)
    render_timer("02-preinfusion.png", 2.4, pre=4.0, conn=1)
    render_timer("03-overextraction.png", 40.0, pre=0.0, steam_frac=-1, conn=1)
    render_status("04-readiness.png", 0.45, 42, 1.0, 0, conn=1)
    render_stats("05-stats.png", 3, 1287, conn=2)
    render_machine_off("06-machine-off.png")
    render_dev_actions("07-dev-actions.png", demo_on=True)
    render_dev_info("08-dev-info.png")
    render_reset_confirm("09-reset-confirm.png")
    print("done ->", OUT)
