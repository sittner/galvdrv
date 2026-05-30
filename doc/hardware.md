# Hardware Design

## FPGA Selection

**XC7A15T-1CPG236C** (Artix-7) — chosen over Spartan-7 XC7S15 because:

- Same price as XC7S15
- HP (high-performance) I/O banks → ODELAYE2 available for high-resolution PWM
- More DSPs (45 vs 20) and BRAM (900 vs 360 Kb)
- 5 MMCMs vs 2

### Resource Usage Estimate

| Module | Logic Cells | DSP48 | BRAM |
|---|---|---|---|
| PWM gen × 2 | ~100 | 0 | 0 |
| Dead-time × 2 | ~50 | 0 | 0 |
| SPI/XADC ADC interface | ~200 | 0 | 0 |
| Current PID × 2 | ~200 | 4 | 0 |
| Position PID × 2 | ~200 | 4 | 0 |
| S-curve executor × 2 | ~100 | 0 | 0 |
| Segment FIFO × 2 | ~50 | 0 | 18 Kb |
| UART/SPI host interface | ~200 | 0 | 4 Kb |
| **Total** | **~1,100** | **12** | **22 Kb** |
| **XC7A15T capacity** | **16,640** | **45** | **900 Kb** |
| **Usage** | **7%** | **27%** | **2%** |

## Development Platform

**Trenz TE0890** (XC7S25 FTGB196) — already in hand for initial development.

- 64 GPIO pins (ports A–H, 8 bits each)
- 100 MHz oscillator
- HyperRAM
- FTDI UART
- XADC aux channels available on port pins (exact mapping to confirm in Vivado)

### Phase 0 (Characterization) Setup

The TE0890 is used together with an ESP32 Wi-Fi loader for bitstream configuration and a signal generator / scope engine:

```
ESP32 (Wi-Fi AP + HTTP)                  TE0890 (FPGA)
┌──────────────────────┐    JTAG         ┌─────────────────────┐
│ Bitstream upload     │────────────────▶│ Configuration       │
│ Siggen config (SPI)  │────SPI─────────▶│ I2S TX → PCM5102A  │──▶ Analog out
│ Scope readback       │◀───SPI──────────│ XADC scope engine   │◀── Analog in
└──────────────────────┘                 └─────────────────────┘
```

## Power Stage: DRV8874

| Parameter | Value |
|---|---|
| Topology | Full H-bridge (integrated) |
| Continuous current | 6 A |
| Supply voltage | 6–37 V |
| Max PWM frequency | 500 kHz |
| Control mode | IN/IN (complementary PWM from FPGA) |
| Current sensing | IPROPI analog output (~1 µA/mA) |
| Protection | Overcurrent, thermal, undervoltage (nFAULT) |
| Price | ~$2–3 |

### Starting Hardware

Pololu/Exp-Tech DRV8874 carrier board (~€8). Pololu specs 100 kHz but likely works at 500 kHz (no input RC filters visible). Verify with oscilloscope.

### Interface to FPGA

```
FPGA  ──IN1──▶  DRV8874  ──OUT1──┐
      ──IN2──▶            ──OUT2──┼──▶  Galvo Coil
      ◀─IPROPI (analog)──────────┘
      ◀─nFAULT───────────────────
```

## ADC Strategy

### Phase 1: XADC (on-chip, 12-bit)

- 12-bit, up to 1 MSPS shared across channels
- Input range: 0–1 V (needs resistor divider from ±10 V galvo signals)
- Front-end per channel:
  ```
  ±10V ──[100k]──┬──[4k7]──▶ 0.5V bias
                  │
                  ├── 100pF (anti-alias)
                  │
                  └──▶ XADC VAUX pin (0–1V)
  ```
- Sequencing options:
  - Position-only loop (2 ch × 500 kSPS) → 500 kHz PWM possible
  - Full cascaded (4 ch × 250 kSPS) → 250 kHz PWM
  - Prioritized: current at high rate, position at 50 kHz (sufficient for outer loop)

### Phase 2: External 16-bit SAR ADC

Target: **ADS8861** or **LTC2312-16** (16-bit, 1 MSPS, SPI)

- Same SPI interface from FPGA — swap ADC module only
- Requires analog front-end (op-amp attenuator for ±10 V → 0–Vref)
- On custom PCB: consider **AD7616** (±10 V native input, no front-end needed, but ~€20)

## PWM Resolution

### The Problem

```
100 MHz clock / 500 kHz PWM = 200 counts → ~8 bits raw
16-bit ADC ENOB ≈ 14.5 bits → PWM needs ≥14.5 effective bits
```

### Solution: OSERDES + ODELAY (Artix-7 only)

| Technique | Resolution | Notes |
|---|---|---|
| 100 MHz counter | 7.6 bits | Basic |
| OSERDESE2 8:1 DDR (800 Mb/s) | 10.6 bits | 1.25 ns edge resolution |
| + ODELAYE2 (32 taps × 78 ps) | +5 bits | HR bank pins required |
| **Combined** | **15.6 bits raw** | Exceeds ADC ENOB |
| + PID natural dithering | 18+ effective | Integrator oscillates between adjacent duty values |

### Key Insight: No Explicit Dithering Module Needed

The PID integrator naturally dithers between adjacent PWM duty values at a rate well above mechanical bandwidth. The control loop **is** the ditherer. Only requirement: PID internal math must be wide (24–32 bit fixed-point).

### OSERDES Implementation

- MMCM generates 400 MHz (DDR → 800 Mb/s output)
- Each fabric clock cycle, 8 bits are serialized
- PWM duty split into coarse (which clock cycle) and fine (which bit within the 8-bit word)
- Center-aligned (up/down counter) for synchronous ADC trigger at quiet moment

## Supply Voltage

- **36 V nominal** (DRV8874 max = 37 V)
- If higher voltage needed for CT-6800 slew rate: migrate to discrete FETs + UCC27211 gate drivers (48 V+)

## PCB Design Requirements (Phase 2 Custom Board)

- 4-layer minimum (ground plane between analog and digital)
- Analog/digital ground zone separation
- Short traces: FPGA → gate drive / ADC
- Shielded connectors for galvo sensor cables
- Synchronous ADC sampling eliminates most EMI concerns if layout is decent
