# galvdrv

FPGA-based dual-axis galvo controller project for laser show systems, targeting:

- General Scanning **G120** (small, fast)
- Cambridge Technology **CT-6800** (large, powerful)

The controller architecture uses a digital position setpoint path (ILDA stream / Ethernet / SPI) and a mixed-signal feedback interface:

- Position feedback (ADC)
- Current feedback (ADC)
- Current command (PWM)

## System Architecture

- Dual-axis control implemented in FPGA for deterministic timing and low jitter
- Cascaded loops per axis:
  - Outer **position loop** in digital domain
  - Inner **current loop** driving a PWM H-bridge
- Analog feedback sampled synchronously to PWM timing to reduce switching-noise coupling

## Control Loop Architecture

- Cascaded control:
  - Outer position loop: ~**50 kHz**
  - Inner current loop: ~**500 kHz**
- PWM-based Class-D drive at **500 kHz**
  - Removes DAC + linear amplifier from the signal chain
  - Fits FPGA-native implementation well
- Galvo coil inductance provides natural filtering of PWM ripple current
- Mirror inertia gives >90 dB attenuation of PWM-frequency torque ripple at the mechanical output

## Power Stage

### Primary integrated option

- **TI DRV8874** integrated H-bridge
  - 6 A continuous
  - 6–37 V supply range
  - 500 kHz PWM capable
  - Built-in current mirror output (**IPROPI**) that may remove need for external shunt amplifier
  - Integrated fault protection (overcurrent, thermal, undervoltage)
- Planned nominal supply: **36 V** (DRV8874 max = 37 V)

### Higher-voltage alternative

- Discrete half-bridge implementation with **UCC27211** gate drivers
- Intended for designs requiring ~48 V operation

## Error Budget (5 m throw distance)

| Error Source | Magnitude | Mitigation |
|---|---|---|
| Tracking error (corners, 30kpps) | 50–200 mm | Feedforward, pre-distortion, higher PWM |
| EMI to position sensor (good layout) | 0.3–1 mm | Synchronous ADC sampling, shielding |
| EMI to position sensor (bad layout) | 3–30 mm | Fix layout |
| PWM current ripple → position | < 0.001 mm | Physics (inertia) |
| ADC quantization noise | 0.2–0.4 mm | 16-bit sufficient |

## Development Phases

### Phase 1

- Simulation-first development:
  - Python plant model
  - cocotb HDL co-simulation scaffold
- Eval hardware:
  - Digilent Arty A7
  - 2× DRV8874-EVM

### Phase 2

- Custom 4-layer PCB integrating:
  - FPGA
  - ADC front-end
  - DRV8874 power stages

## Key Design Decisions

- **Class-D PWM over linear amplifier**
  - Lower cost, lower thermal load, simpler FPGA integration
- **Synchronous ADC sampling at PWM center**
  - Critical for EMI rejection on position/current feedback
- **500 kHz PWM target**
  - Supports practical inner-loop current bandwidth near 50 kHz
- **DRV8874 as first H-bridge IC**
  - Best initial tradeoff of features, cost, and PWM capability

## Repository Structure

```text
README.md
esp32-loader/
  CMakeLists.txt
  sdkconfig.defaults
  README.md
  main/
    main.c
    wifi_ap.c/h
    http_server.c/h
    jtag_player.c/h
    Kconfig.projbuild
sim/
  plant.py
  pid.py
  utils.py
  run_step_response.py
  requirements.txt
  cocotb/
    test_galvo.py
  output/
    .gitkeep
```
