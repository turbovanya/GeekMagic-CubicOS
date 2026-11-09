# GeekMagic CubicOS

CubicOS is community firmware for the **GeekMagic HelloCubic-Lite** with an ESP8266, 4 MiB of flash and a 240×240 ST7789 display viewed through a prism.

- Large clock and date with two faces, PC telemetry, a 16-band audio visualizer, current weather and device information.
- CubicOS Studio controls the screen and saves a palette, brightness, UTC offset and screen rotation interval. The preview fetches a frame from the cube.
- The setup access point can scan for 2.4 GHz networks. The cube saves new network credentials after connecting successfully.
- Firmware updates use the password-protected `/update` page and preserve stored Wi-Fi credentials and display settings.

## Build and install

```sh
python -m pip install platformio==6.1.19
pio run
```

The build writes `.pio/build/hellocubic_lite/firmware.bin`. On a cube already running CubicOS, use **Firmware** on `/update`. The username is `admin` and the password is the one shown on the cube. For first installation, confirm the chip and 4 MiB flash size, back up the full flash using esptool, then write the image at offset `0x0` in the ESP8266 serial bootloader. This image targets the HelloCubic-Lite hardware listed above.

The cube shows its `CubicOS-…` access-point name and password on the display when it needs setup. Connect to it and open [Wi-Fi setup](http://192.168.4.1/wifi). Choose a network and wait for the LAN address. The **Device** screen shows the LAN IP, and `/api/status` reports the version, firmware digest and local hostname.

## Studio and feeds

Open the cube's home page and choose **Открыть конструктор**. The access-point credentials also protect changes in Studio. Set the UTC offset in minutes for your location. To run the music visualizer, select an audio file in the browser and play it. The browser sends 16 frequency levels, and audio playback stays on your computer.

Use the bundled PC and weather feeds from another computer on the LAN.

```sh
python -m pip install psutil nvidia-ml-py
python tools/pc_metrics.py --host CUBE_IP
python tools/weather_feed.py --host CUBE_IP --latitude 52.52 --longitude 13.41 --label Berlin
```

Replace the sample location with yours. Weather data comes from [Open-Meteo](https://open-meteo.com/). Check its [terms](https://open-meteo.com/en/terms) before using the bundled feed commercially. Any compatible local producer can post data directly to CubicOS.

| Route | Method | Data |
|---|---|---|
| `/api/status` | GET | Version, firmware MD5, page, uptime and network state |
| `/api/page` | POST | Form field `page`, 0–4 |
| `/api/metrics` | POST | JSON with `cpu`, `gpu`, `ram` and `gpu_temp`, integers 0–100 |
| `/api/spectrum` | POST | JSON with `bands`, 16 integers 0–100 |
| `/api/weather` | POST | JSON with `place`, `temperature` (°C), `humidity` (%) and `code` (WMO) |
| `/debug/screen.bmp` | GET | 240×240 logical frame with the access-point password omitted |

CubicOS is designed for a trusted local network. The firmware and settings routes use HTTP Basic authentication, and the device controls and data feeds are accessible on the LAN.

Version 0.5.0 was tested on one HelloCubic-Lite. Checks covered OTA, Wi-Fi scanning, Studio settings, display capture, PC telemetry, browser audio and sample weather data. The cube's first-time connection to a new router still needs testing across hardware and router revisions.

The display initialization is based on the hardware work documented by [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware). See [LICENSE](LICENSE) for GPL-3.0 terms.
