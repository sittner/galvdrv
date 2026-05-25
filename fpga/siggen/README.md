# TE0890 dual-channel DDS signal generator

Vivado batch-mode project for TE0890 (XC7S25FTBG196-1C), generating two DDS channels and streaming them over I2S to a PCM5101A DAC.

## Highlights

- Input clock: 100 MHz on `L5`
- MMCM output: 32 MHz system clock (`DAC_SCK` = 256 × 125 kHz)
- I2S audio clocks:
  - `DAC_BCK` = 4 MHz (32 × fs)
  - `DAC_LCK` = 125 kHz (fs)
- 2× DDS channels (L/R):
  - 32-bit phase accumulator
  - Waveforms: sine, square (variable duty), ramp, triangle
  - 16-bit amplitude scaling
- SPI slave control (`8-bit addr + 16-bit data`, mode 0)

## Build

```bash
cd fpga/siggen
make bit
```

Output:

- `build/siggen.bit`

## Upload

```bash
make upload
```

## Register map

| Reg  | Function |
|------|----------|
| 0x00 | Ch0 phase increment [15:0] |
| 0x01 | Ch0 phase increment [31:16] |
| 0x02 | Ch0 waveform (0=sine, 1=square, 2=ramp, 3=triangle) |
| 0x03 | Ch0 amplitude |
| 0x04 | Ch0 duty cycle |
| 0x08 | Ch1 phase increment [15:0] |
| 0x09 | Ch1 phase increment [31:16] |
| 0x0A | Ch1 waveform |
| 0x0B | Ch1 amplitude |
| 0x0C | Ch1 duty cycle |
| 0x10 | Global enable (bit0=Ch0, bit1=Ch1) |
