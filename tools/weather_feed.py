"""Send current Open-Meteo conditions to a CubicOS display on your LAN."""

import argparse
import json
import math
import re
import time
from urllib.error import URLError
from urllib.parse import urlencode
from urllib.request import ProxyHandler, Request, build_opener, urlopen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="Device IP or .local name")
    parser.add_argument("--latitude", type=float, required=True)
    parser.add_argument("--longitude", type=float, required=True)
    parser.add_argument("--label", required=True, help="Display name in Latin characters")
    parser.add_argument("--interval", type=int, default=600, help="Refresh interval in seconds")
    parser.add_argument("--once", action="store_true", help="Send one reading and exit")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9.\-]+", args.host):
        parser.error("--host must be a hostname or IPv4 address")
    if not math.isfinite(args.latitude) or not -90 <= args.latitude <= 90:
        parser.error("--latitude must be between -90 and 90")
    if not math.isfinite(args.longitude) or not -180 <= args.longitude <= 180:
        parser.error("--longitude must be between -180 and 180")
    if not re.fullmatch(r"[A-Za-z0-9 .'-]{1,18}", args.label) or not args.label.strip():
        parser.error("--label needs 1–18 printable characters")
    if not 60 <= args.interval <= 1800:
        parser.error("--interval must be between 60 and 1800 seconds")
    query = urlencode({
        "latitude": args.latitude,
        "longitude": args.longitude,
        "current": "temperature_2m,relative_humidity_2m,weather_code",
        "temperature_unit": "celsius",
    })
    lan = build_opener(ProxyHandler({}))
    endpoint = f"http://{args.host}/api/weather"
    while True:
        try:
            with urlopen("https://api.open-meteo.com/v1/forecast?" + query, timeout=12) as reply:
                current = json.load(reply)["current"]
            body = json.dumps({
                "place": args.label,
                "temperature": round(current["temperature_2m"]),
                "humidity": round(current["relative_humidity_2m"]),
                "code": int(current["weather_code"]),
            }).encode()
            request = Request(endpoint, data=body, headers={"Content-Type": "application/json"})
            with lan.open(request, timeout=5) as reply:
                if reply.read() != b"OK":
                    raise ValueError("Unexpected reply from CubicOS")
            print("Weather updated", flush=True)
            if args.once:
                return 0
        except (URLError, TimeoutError, ValueError, KeyError, TypeError) as error:
            print(f"Weather update failed: {error}", flush=True)
            if args.once:
                return 1
        time.sleep(args.interval)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(0)
