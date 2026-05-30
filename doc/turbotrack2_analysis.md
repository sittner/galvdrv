# Turbotrack2 Reference Driver Analysis

## Overview

The Turbotrack2 is a **linear** (Class-AB) galvo driver using two LM12 power op-amps in bridge configuration. It serves as the reference system to characterize the G120 galvo and validate our simulation model.

## Power Stage

```
           +24V                    +24V
            │                       │
       MUR140 (D4)             MUR140 (D2)
            │                       │
       ┌────┴────┐            ┌────┴────┐
       │  IC1    │            │  IC2    │
       │  LM12   │            │  LM12   │
       └────┬────┘            └────┬────┘
            │                       │
         GALVO-              R1 (1Ω/5W) → Fuse (3.15A) → GALVO+
            │                       │
       MUR140 (D3)             MUR140 (D1)
            │                       │
           -24V                    -24V
```

- Supply: ±24 V
- Current sense: 1 Ω / 5 W resistor (R1) in series with coil
- Fuse: 3.15 A (defines max continuous current)
- INA117P differential amplifier measures voltage across R1

## Control Loop Structure

Three nested loops:

### 1. Current Loop (innermost)

- **Compensator**: U211 (RC4156N quad op-amp)
- Key components: R42 = 2.74 kΩ, C44 = 22 nF
- Estimated bandwidth: ~2.6 kHz (limited by RC4156 + LM12 GBW)
- Current sense: INA117P across 1 Ω shunt, gain ≈ 5.86× (R95+R96 / R97)

### 2. Velocity Loop (middle)

- Derived from position sensor output (analog differentiation)
- U111 TL074P conditions the position signal and produces velocity
- Components: R91/R88 = 1 kΩ input, R92/R87 = 100 kΩ feedback, C19/C17 = 22 pF

### 3. Position Loop (outermost)

- **Compensator**: U121 (RC4156N quad op-amp)
- Lead-lag network: R69 = 10 kΩ / C25 = 470 pF → zero at 33.9 kHz
- High-frequency pole: R71 = 90.9 kΩ / C27 = 68 pF → pole at 25.8 kHz
- Position feedback: R73 = 100 kΩ, C22 = 220 pF / R78 = 6.19 kΩ

## Reference Voltage

- IC3 (AD587): +10.000 V precision reference
- U292 (741 op-amp): inverts to -10 V via R25/R35 = 10 kΩ / 10 kΩ

## DIP Switch Banks (Mode Selection)

The Turbotrack2 uses DIP switches to configure for different galvo types. In G120 mode, specific switches route different compensation networks.

- SA0, SB0: 4-switch banks selecting gain/damping resistor networks
- S312: 4-switch bank selecting feedback paths
- S391: 8-switch bank for additional compensation options
- S5: 8-switch bank for protection/AGC paths

## Trim Pots (G120 Calibration)

| Pot | Function | Range | Circuit Location |
|---|---|---|---|
| VR111 | Position offset | 20 kΩ | Between ±10V ref → position null adjustment |
| VR141 | Servo offset | 20 kΩ | Between ±10V ref → servo bias |
| VR142 | Velocity balance | 10 kΩ | Between VA/VB nodes |
| VR161/162 | Damping (per bank) | 20 kΩ | U121 compensation network |
| VR181/182 | Position gain (per bank) | 20 kΩ | U121 feedback scaling |
| VR191/192 | Velocity gain (per bank) | 20 kΩ | U121 velocity path |
| VR251/252 | Input gain (per bank) | 100 kΩ | Input scaling |

## Key Design Parameters Extracted

```
Supply voltage:                   ±24 V
Max current (fuse):               3.15 A
Current sense resistor:           1 Ω
Current sense amplifier gain:     5.86×
Position sensor conditioning:     gain = 100× (100k/1k), pole at 72 kHz
Current loop BW:                  ~2.6 kHz (R42/C44 = 2.74k/22n)
LM12 GBW:                        ~700 kHz
LM12 slew rate:                   ~9 V/µs
```

## Comparison: Turbotrack2 vs Our Design

| Aspect | Turbotrack2 | Our FPGA Design |
|---|---|---|
| Power stage | Linear (LM12, ±24V) | Switching (DRV8874, 36V) |
| Efficiency | ~30–50% (72W heat at 3A) | ~90–95% (~2W heat) |
| Current loop BW | ~2.6 kHz | ~50 kHz (20× faster) |
| Compensation | Fixed analog (RC networks) | Digital PID (adjustable) |
| Tuning | Trim pots (drift, manual) | Digital coefficients (stable, remote) |
| Trajectory knowledge | None (analog input) | Full (integrated planner) |
| PWM noise | None (linear) | Managed via sync sampling + inertia |

## System Identification Plan

Using the simplified netlist (with actual trim values) + scope captures:

1. **Fit second-order model** to small step response:
   ```
   Position(t) = K × (1 - e^(-ζωn·t) × (cos(ωd·t) + ζ/√(1-ζ²) × sin(ωd·t)))
   ```
   Extracts: ωn (natural frequency), ζ (damping ratio), K (DC gain), td (transport delay)

2. **Extract physical parameters**:
   - J = Kt × Ks / ωn² (from loop gain and resonance)
   - B from decay rate
   - Kt from current-to-acceleration ratio

3. **Validate against netlist model**: Simulate the full TT2 compensation network in Python, compare with measured response.

## Connector Pinout (F15H — Galvo)

| Pin | Signal | Notes |
|---|---|---|
| 1 | GALVO+ | Motor positive |
| 2 | GALVO- | Motor negative |
| 3 | POS+ | Position sensor + |
| 4 | GND | Sensor ground |
| 5 | GND | |
| 11 | CURR_OUT | Current monitor output |

## Connector Pinout (F15H — External Interface)

| Pin | Signal | Notes |
|---|---|---|
| 1 | IN+ | Position command + |
| 2 | IN- | Position command - |
| 3 | POS_OUT | Position monitor output |
| 4 | VEL_OUT | Velocity monitor output |
