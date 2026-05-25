# TE0890 blink demo

Simple Vivado batch-mode FPGA demo for TE0890 (XC7S25FTBG196-1C):

- 100 MHz input clock on `L5`
- `LED1` (`D14`) and `LED2` (`C14`) blink alternately at ~1 Hz
- HyperRAM pins are constrained into a safe inactive state:
  - `hr_cs_l` is driven high
  - `hr_rst_l` is driven low
  - `hr_ck`, `hr_rwds`, and `hr_dq[7:0]` are inputs with pulldowns

## Build bitstream

```bash
cd fpga/blink
make bit
```

Result:

- `build/blink.bit`

## Upload

```bash
make upload
```
