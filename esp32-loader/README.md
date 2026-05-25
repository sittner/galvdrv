# esp32-loader

ESP-IDF v5.x project for OTA FPGA programming of the TE0890 (XC7S25) over JTAG via WiFi.

## Features

- ESP32-S3 SoftAP mode (`galvo-dev`)
- REST endpoint: `POST /api/bitstream` (.bit)
- Signal generator control API: `GET/POST /api/siggen`
- Embedded control UI: `GET /`
- Streaming raw `.bit` upload and JTAG bit-bang player
- Configurable JTAG GPIO mapping in Kconfig (defaults: GPIO4/5/6/7)
- Configurable SIGGEN SPI GPIO mapping in Kconfig (defaults: GPIO10/11/12/13)

## Default JTAG wiring

- GPIO4 -> TCK
- GPIO5 -> TMS
- GPIO6 -> TDI
- GPIO7 <- TDO
- GND between ESP32 and TE0890

## Build and flash

```bash
cd esp32-loader
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Upload raw `.bit`

```bash
curl -X POST http://192.168.4.1/api/bitstream --data-binary @build/blink.bit
```

Successful response example:

```json
{"ok":true,"bytes":123456}
```

Failure response example:

```json
{"ok":false,"error":"..."}
```

## Signal generator API

Set one channel:

```bash
curl -X POST http://192.168.4.1/api/siggen \\
  -H 'Content-Type: application/json' \\
  -d '{"channel":0,"waveform":"sine","freq_hz":1000.0,"amplitude":0.8,"duty":0.5,"enable":true}'
```

Read both channels:

```bash
curl http://192.168.4.1/api/siggen
```

Open UI:

```bash
xdg-open http://192.168.4.1/
```

## Notes

- DONE pin status is not available in the default 4-wire JTAG-only setup.
- The signal-generator UI shows both channels side-by-side, supports mouse-wheel value changes, and reads current FPGA registers as the source of truth.
