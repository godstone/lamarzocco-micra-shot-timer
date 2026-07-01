#!/usr/bin/env python3
"""Pre-flight check for the La Marzocco brew-timer display project.

Run this BEFORE writing/finishing the firmware. It proves the whole data path works
and captures reference data for the on-device C++ port.

What it does:
  1. Generates + persists an installation key (installation_key.json, reused across runs).
     This same key/keypair is what the firmware will embed so it only has to *sign*
     requests, never register.
  2. Signs in with your La Marzocco Home credentials (from .env).
  3. Lists your machines, picks one (LM_SERIAL or the first found).
  4. Dumps the raw dashboard once (dashboard_<serial>.json) as a schema reference.
  5. Polls the dashboard ~1x/second and prints: status + elapsed brew time.

While it runs:
  * Pull a shot  -> confirm status flips to "Brewing" and the elapsed timer counts up.
  * Open the official LM app -> confirm it STAYS connected and you can change a
    parameter (proves REST polling does not kick the app off its websocket).

Usage:
    python3 -m venv .venv && source .venv/bin/activate
    pip install -r requirements.txt
    cp .env.example .env   # then edit .env
    python preflight.py            # poll loop
    python preflight.py --dump     # just dump the dashboard once and exit
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import sys
import time
import uuid
from pathlib import Path

try:
    from dotenv import load_dotenv
except ImportError:  # dotenv is optional
    def load_dotenv(*_a, **_k):  # type: ignore
        return False

from aiohttp import ClientSession

from pylamarzocco import LaMarzoccoCloudClient
from pylamarzocco.util import InstallationKey, generate_installation_key

HERE = Path(__file__).resolve().parent
KEY_FILE = HERE / "installation_key.json"

# Machine status strings we care about (from CMMachineStatus widget output.status).
BREWING_STATES = {"Brewing", "BrewingMode"}


def load_or_create_installation_key() -> InstallationKey:
    """Reuse a saved installation key, or make one and persist it.

    The firmware will reuse THIS key (installation id + private key) so the device
    can sign requests without re-registering. Keep installation_key.json safe.
    """
    if KEY_FILE.exists():
        return InstallationKey.from_json(KEY_FILE.read_text(encoding="utf-8"))
    key = generate_installation_key(str(uuid.uuid4()).lower())
    KEY_FILE.write_text(key.to_json(), encoding="utf-8")
    print(f"[+] Generated new installation key -> {KEY_FILE.name}")
    return key


def deep_find(obj, key_substr: str) -> list:
    """Recursively collect values whose key contains key_substr (case-insensitive).

    Robust against schema nesting changes in the dashboard payload.
    """
    out: list = []

    def rec(o):
        if isinstance(o, dict):
            for k, v in o.items():
                if key_substr.lower() in str(k).lower():
                    out.append(v)
                rec(v)
        elif isinstance(o, list):
            for item in o:
                rec(item)

    rec(obj)
    return out


def extract_brew_state(dashboard: dict) -> tuple[str | None, int | None]:
    """Return (status, brewing_start_time_ms) from a dashboard dict."""
    statuses = [s for s in deep_find(dashboard, "status") if isinstance(s, str)]
    # Prefer a recognised machine status; otherwise fall back to the first string.
    status = next(
        (s for s in statuses if s in BREWING_STATES or s in {"PoweredOn", "StandBy", "Off"}),
        statuses[0] if statuses else None,
    )
    starts = [
        v for v in deep_find(dashboard, "brewingstarttime")
        if isinstance(v, (int, float))
    ]
    start_ms = int(starts[0]) if starts else None
    return status, start_ms


def fmt_elapsed(start_ms: int | None) -> str:
    if not start_ms:
        return "--:--"
    elapsed = max(0.0, time.time() - start_ms / 1000.0)
    return f"{int(elapsed) // 60:01d}:{int(elapsed) % 60:02d}.{int((elapsed * 10) % 10)}"


def dashboard_to_dict(dash) -> dict:
    """pylamarzocco returns a typed config object; normalise to a plain dict."""
    if isinstance(dash, dict):
        return dash
    for attr in ("to_dict",):
        fn = getattr(dash, attr, None)
        if callable(fn):
            return fn()
    # last resort
    return json.loads(json.dumps(dash, default=lambda o: getattr(o, "__dict__", str(o))))


async def resolve_serial(client: LaMarzoccoCloudClient) -> str:
    wanted = os.environ.get("LM_SERIAL")
    things = await client.list_things()
    if not things:
        sys.exit("[!] No machines found on this account.")
    serials = []
    for t in things:
        sn = getattr(t, "serial_number", None) or getattr(t, "serialNumber", None)
        if sn:
            serials.append(sn)
    print(f"[+] Machines on account: {serials}")
    if wanted:
        if wanted not in serials:
            print(f"[!] LM_SERIAL={wanted} not in account list; using it anyway.")
        return wanted
    return serials[0]


async def main() -> None:
    parser = argparse.ArgumentParser(description="La Marzocco brew-timer pre-flight")
    parser.add_argument("--dump", action="store_true", help="dump dashboard once and exit")
    parser.add_argument("--interval", type=float, default=None, help="poll interval seconds")
    args = parser.parse_args()

    load_dotenv(HERE / ".env")
    username = os.environ.get("LM_USERNAME")
    password = os.environ.get("LM_PASSWORD")
    if not username or not password:
        sys.exit("[!] Set LM_USERNAME and LM_PASSWORD in tools/preflight/.env")

    interval = args.interval or float(os.environ.get("POLL_INTERVAL", "1.0"))
    installation_key = load_or_create_installation_key()

    async with ClientSession() as session:
        client = LaMarzoccoCloudClient(
            username=username,
            password=password,
            installation_key=installation_key,
            client=session,
        )
        print("[+] Registering client / signing in ...")
        await client.async_register_client()
        token = await client.async_get_access_token()
        print(f"[+] Signed in. Access token length: {len(token)}")

        serial = await resolve_serial(client)
        print(f"[+] Using machine: {serial}")

        # One-time schema dump for reference + as a golden vector for the C++ port.
        dash = await client.get_thing_dashboard(serial)
        dash_dict = dashboard_to_dict(dash)
        dump_path = HERE / f"dashboard_{serial}.json"
        dump_path.write_text(json.dumps(dash_dict, indent=2, default=str), encoding="utf-8")
        print(f"[+] Wrote dashboard schema -> {dump_path.name}")

        status, start_ms = extract_brew_state(dash_dict)
        print(f"[+] Initial: status={status!r} brewingStartTime={start_ms}")
        if status is None:
            print("[!] Could not find a status field. Inspect the dumped JSON and adjust "
                  "extract_brew_state() (look for the CMMachineStatus widget).")

        if args.dump:
            return

        print(f"\n[+] Polling every {interval:.1f}s. Pull a shot now. Ctrl-C to stop.")
        print("    (Open the LM app too — confirm it stays connected.)\n")
        while True:
            try:
                dash = await client.get_thing_dashboard(serial)
                status, start_ms = extract_brew_state(dashboard_to_dict(dash))
                brewing = status in BREWING_STATES
                bar = "🔴 BREWING" if brewing else "   idle   "
                elapsed = fmt_elapsed(start_ms) if brewing else "  --  "
                print(f"\r{bar}  status={status:<10} elapsed={elapsed}   ", end="", flush=True)
            except Exception as exc:  # keep the loop alive; print and continue
                print(f"\n[!] poll error: {exc!r}")
            await asyncio.sleep(interval)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[+] Stopped.")
