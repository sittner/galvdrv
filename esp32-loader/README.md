# esp32-loader

ESP-IDF v5.x project for OTA FPGA programming of the TE0890 (XC7S25) over JTAG via WiFi.

## Features

- ESP32-S3 SoftAP mode (`galvo-dev`)
- REST endpoint: `POST /api/bitstream`
- Streaming SVF upload parser and JTAG bit-bang player
- Configurable JTAG GPIO mapping in Kconfig (defaults: GPIO4/5/6/7)

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

## Upload SVF

```bash
curl -X POST http://192.168.4.1/api/bitstream --data-binary @design.svf
```

Successful response example:

```json
{"ok":true,"bytes":123456,"statements":42,"sir":4,"sdr":32,"done_pin":null}
```

Failure response example:

```json
{"ok":false,"error":"..."}
```

## Notes

- Recommended format is SVF exported from Vivado (`write_hw_svf -force design.svf`).
- `STATE`, `SIR`, `SDR`, `RUNTEST`, `ENDIR`, `ENDDR` are implemented.
- `FREQUENCY` updates runtime TCK rate for `RUNTEST ... SEC`.
- `TRST` and `HIR/HDR/TIR/TDR` are accepted and ignored for the single-device chain.
- DONE pin status is not available in the default 4-wire JTAG-only setup.
