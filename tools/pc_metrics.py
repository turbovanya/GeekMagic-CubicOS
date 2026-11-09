"""Push Windows/Linux PC utilization to GeekMagic OS over a local Wi-Fi network.

Setup: pip install psutil nvidia-ml-py
Example: python tools/pc_metrics.py --host 192.168.1.42

The target must be your own device on a trusted local network.
"""

from __future__ import annotations

import argparse
from contextlib import closing
from http.client import HTTPConnection, HTTPException
import json
import time

MIB = 1024 * 1024


def read_gpu(nvml, device):
    def read(measure):
        try:
            return measure()
        except nvml.NVMLError:
            return None

    used = read(lambda: nvml.nvmlDeviceGetMemoryInfo(device).used)
    power = read(lambda: nvml.nvmlDeviceGetPowerUsage(device))
    return {
        "gpu": read(lambda: nvml.nvmlDeviceGetUtilizationRates(device).gpu),
        "gpu_temp": read(lambda: nvml.nvmlDeviceGetTemperature(device, nvml.NVML_TEMPERATURE_GPU)),
        "vram_mib": round(used / MIB) if used is not None and 0 <= used <= 1048576 * MIB else None,
        "gpu_power_w": round(power / 1000) if power is not None and 0 <= power <= 9999000 else None,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="GeekMagic OS PC telemetry sender")
    parser.add_argument("--host", required=True, help="Cube IP, e.g. 192.168.1.42")
    parser.add_argument("--period", type=float, default=1.5, help="Seconds between updates")
    args = parser.parse_args()

    if not 0.5 <= args.period <= 30:
        parser.error("--period must be between 0.5 and 30 seconds")

    import psutil  # pip install psutil

    gpu = None
    nvml_initialized = False
    try:
        import pynvml  # nvidia-ml-py
        pynvml.nvmlInit()
        nvml_initialized = True
        gpu = pynvml.nvmlDeviceGetHandleByIndex(0)
    except Exception as exc:
        print(f"NVIDIA telemetry unavailable ({exc}). GPU readings will be blank.")

    endpoint = "/api/metrics"
    url = f"http://{args.host}{endpoint}"
    print(f"Sending to {url}. Press Ctrl+C to stop.")
    psutil.cpu_percent(interval=None)
    try:
        while True:
            start = time.monotonic()
            cpu = int(round(psutil.cpu_percent(interval=0.2)))
            memory = psutil.virtual_memory()
            metrics = {
                "cpu": cpu,
                "ram": round(memory.percent),
                "ram_used_mib": round((memory.total - memory.available) / MIB),
                "gpu": None,
                "gpu_temp": None,
                "vram_mib": None,
                "gpu_power_w": None,
            }
            if gpu is not None:
                metrics.update(read_gpu(pynvml, gpu))

            body = json.dumps(metrics).encode("utf-8")
            try:
                with closing(HTTPConnection(args.host, timeout=3)) as connection:
                    connection.request("POST", endpoint, body, {"Content-Type": "application/json"})
                    response = connection.getresponse()
                    if response.status != 200:
                        print(f"Unexpected HTTP status: {response.status}")
                    else:
                        values = " · ".join(
                            f"{label} {metrics[key]}{unit}" if metrics[key] is not None else f"{label} --"
                            for key, label, unit in (
                                ("cpu", "CPU", "%"), ("gpu", "GPU", "%"),
                                ("ram_used_mib", "RAM", " MiB"), ("vram_mib", "VRAM", " MiB"),
                                ("gpu_power_w", "Power", " W"), ("gpu_temp", "Temp", " C"),
                            )
                        )
                        print(values)
            except (OSError, HTTPException) as exc:
                print(f"Cannot reach cube: {exc}")

            time.sleep(max(0.0, args.period - (time.monotonic() - start)))
    except KeyboardInterrupt:
        print("Stopped.")
    finally:
        if nvml_initialized:
            pynvml.nvmlShutdown()


if __name__ == "__main__":
    main()
