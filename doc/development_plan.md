# Development Plan

## Phase 0 — Characterization (Current)

**Goal**: Measure the reference Turbotrack2 driver + G120 galvo to build an accurate plant model.

### Hardware Setup

- TE0890 (XC7S25 FPGA) as signal generator + scope
- ESP32 as Wi-Fi configuration interface / JTAG loader
- PCM5102A I2S DAC for analog signal generation (step commands to TT2)
- XADC for capturing TT2 outputs (POS_OUT, CURR_OUT)
- Turbotrack2 driver with G120 galvo (reference system)

### Measurement Plan

1. Small step response (~1V = ~2°): extract linear model parameters (J, B, Kt, sensor gain)
2. Large step response (~5V = ~10°): identify saturation, nonlinearity
3. Triangle wave at various frequencies: phase lag vs bandwidth
4. ILDA patterns: real-world multi-axis performance

### Deliverables

- Galvo plant model parameters fitted from measurements
- Turbotrack2 compensation network model (from netlist analysis)
- Validated Python simulation matching measured step responses

## Phase 1 — Simulation

**Goal**: Prove control algorithm before touching hardware.

### Python Plant Model

- Galvo electrical model: `V = R·i + L·(di/dt) + Kt·ω`
- Galvo mechanical model: `Kt·i = J·(dω/dt) + B·ω`
- PWM quantization effects
- ADC quantization and noise
- Transport delay (ADC conversion time)

### Simulation Tasks

1. Tune PID gains for G120 and CT-6800
2. Validate S-curve vs step vs trapezoidal trajectories
3. Resonance suppression verification (t_j = 1/2f_r)
4. Feedforward gain tuning (Kff_v, Kff_a)
5. Fixed-point overflow analysis (32-bit integer simulation)
6. Segment FIFO depth sizing
7. PWM quantization noise analysis (OSERDES resolution sufficient?)

### cocotb Co-Simulation

- HDL driving Python plant model cycle-by-cycle
- Verify fixed-point implementation matches float model
- Add noise, quantization, dead-time effects

## Phase 2 — Custom Development Board

**Goal**: Working dual-axis galvo controller on custom PCB.

### FPGA: XC7A15T-1CPG236C

- OSERDES + ODELAY for high-resolution PWM
- XADC for initial ADC (12-bit, upgradeable)
- All control loops in HDL

### Board Features

| Block | Components |
|---|---|
| FPGA | XC7A15T, MMCM for 400 MHz OSERDES clock |
| Power stage × 2 | DRV8874 (36 V, 6 A) |
| ADC (Phase 2a) | XADC with resistor divider front-end |
| ADC (Phase 2b) | External ADS8861 × 4 (16-bit, SPI) |
| Host interface | UART (FTDI) or SPI |
| Power | 36 V input, 3.3 V / 1.2 V regulators |
| Mechanical | 4-layer PCB, ground plane, analog/digital separation |
| Connectors | Shielded for galvo sensor cables |
| Debug | JTAG header, status LEDs |

### FPGA Gateware Modules

| Module | Description |
|---|---|
| `pwm_oserdes` | Center-aligned PWM with OSERDES + ODELAY |
| `deadtime` | Complementary outputs with dead-time insertion |
| `adc_xadc` | XADC DRP interface + sequencer |
| `adc_spi` | SPI master for external ADS8861 (Phase 2b) |
| `pid` | Fixed-point PID with anti-windup |
| `scurve_exec` | 3-adder trajectory integrator |
| `seg_fifo` | BRAM FIFO for trajectory segments |
| `uart_if` | Host communication (register read/write) |

### Software (Host PC)

- S-curve planner (Python): computes (jerk, duration) segment pairs
- ILDA parser: converts point stream to trajectory segments
- Tuning UI: adjust PID gains, observe step response
- System identification: fit model from measured data

## Tool Requirements

| Tool | Purpose |
|---|---|
| Vivado ML Standard (free) | Synthesis, P&R, bitstream for Artix-7 |
| Python + NumPy + Matplotlib | Plant modeling, PID tuning |
| cocotb + Verilator | HDL simulation with Python testbench |
| GTKWave | Waveform viewing |
| KiCad | PCB design |

Note: Open-source FPGA tools (Yosys + nextpnr) cannot target this design due to missing XADC, OSERDES, and MMCM support for Xilinx 7-series.

## Timeline Expectation

```
Phase 0:  Characterization with TE0890 + ESP32 setup
Phase 1:  Python simulation → validate algorithms
Phase 2a: Custom board with XADC (12-bit) → close loop on real hardware
Phase 2b: Upgrade to external 16-bit ADC → final performance
```

## Alternative: RP2350 Path (Deferred)

An RP2350 (Raspberry Pi Pico 2) was considered as an alternative to FPGA:

- **Pros**: 5–10× faster development, €4, easy debugging (printf/GDB), S-curve planner trivial in C
- **Cons**: limited to ~250 kHz PWM (SPI ADC bottleneck), time-multiplexed axes, 10–50 ns jitter vs <1 ns

Decision: Start with FPGA (performance ceiling is higher). The RP2350 remains a fallback or hybrid option (RP2350 for trajectory + comms, FPGA for inner loops) if development time becomes critical.
