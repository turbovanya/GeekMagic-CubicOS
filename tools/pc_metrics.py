"""Push Windows/Linux PC utilization to GeekMagic OS over a local Wi-Fi network.

Setup: pip install psutil nvidia-ml-py
Example: python tools/pc_metrics.py --host 192.168.1.42

The target must be your own device on a trusted local network.
"""

from __future__ import annotations

import argparse
import json
import time
import urllib.error
import urllib.request


def main() -> None:
    parser = argparse.ArgumentParser(description="GeekMagic OS PC telemetry sender")
    parser.add_argument("--host", required=True, help="Cube IP, e.g. 192.168.1.42")
    parser.add_argument("--period", type=float, default=1.5, help="Seconds between updates")
    args = parser.parse_args()

    if not 0.5 <= args.period <= 30:
        parser.error("--period must be between 0.5 and 30 seconds")

    import psutil  # pip install psutil

    try:
        import pynvml  # nvidia-ml-py
        pynvml.nvmlInit()
        gpu = pynvml.nvmlDeviceGetHandleByIndex(0)
    except Exception as exc:
        gpu = None
        print(f"NVIDIA telemetry unavailable ({exc}). Sending 0 for GPU values.")

    url = f"http://{args.host}:80/api/metrics"
    print(f"Sending to {url}. Press Ctrl+C to stop.")
    psutil.cpu_percent(interval=None)
    try:
        while True:
            start = time.monotonic()
            cpu = int(round(psutil.cpu_percent(interval=0.2)))
            ram = int(round(psutil.virtual_memory().percent))
            gpu_usage = gpu_temp = 0
            if gpu is not None:
                try:
                    gpu_usage = int(pynvml.nvmlDeviceGetUtilizationRates(gpu).gpu)
                    gpu_temp = int(pynvml.nvmlDeviceGetTemperature(gpu, pynvml.NVML_TEMPERATURE_GPU))
                except Exception as exc:
                    print(f"GPU read failed: {exc}")

            metrics = {"cpu": cpu, "gpu": gpu_usage, "ram": ram, "gpu_temp": gpu_temp}
            body = json.dumps(metrics).encode("utf-8")
            request = urllib.request.Request(
                url,
                data=body,
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            try:
                with urllib.request.urlopen(request, timeout=3) as response:
                    if response.status != 200:
                        print(f"Unexpected HTTP status: {response.status}")
                    else:
                        print("CPU {cpu:>3}% · GPU {gpu:>3}% · RAM {ram:>3}% · GPU {gpu_temp:>2}°C".format(**metrics))
            except (urllib.error.URLError, TimeoutError) as exc:
                print(f"Cannot reach cube: {exc}")

            time.sleep(max(0.0, args.period - (time.monotonic() - start)))
    except KeyboardInterrupt:
        print("Stopped.")
    finally:
        if gpu is not None:
            pynvml.nvmlShutdown()


if __name__ == "__main__":
    main()
