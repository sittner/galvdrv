# System Architecture

## Overview

FPGA-based dual-axis galvo controller for laser show systems. Digital position setpoints feed into cascaded control loops that drive galvo coils via Class-D (PWM) H-bridges.

## Target Galvos

| Parameter | G120 (General Scanning) | CT-6800 (Cambridge Technology) |
|---|---|---|
| Type | Small, fast | Large, powerful |
| Typical use | Y axis | X axis |
| Coil resistance (R) | TBD | TBD |
| Coil inductance (L) | TBD | TBD |
| Torque constant (Kt) | TBD | TBD |
| Rotor + mirror inertia (J) | TBD | TBD |
| Viscous damping (B) | TBD | TBD |
| Mechanical resonance | TBD | TBD |
| Max continuous current | TBD (fuse = 3.15 A on TT2) | TBD |

**All electrical/mechanical parameters to be determined from:**
1. Turbotrack2 step response measurements (system identification)
2. Manufacturer datasheets (if obtainable)
3. Direct coil measurement (R, L with LCR meter)

## Block Diagram

```
Host PC                              FPGA (XC7A15T)
┌──────────────────┐                ┌────────────────────────────────────────┐
│ ILDA parser      │   UART/SPI     │                                        │
│ S-curve planner  │───────────────▶│  Segment FIFO (BRAM)                  │
│ Tuning UI        │                │       │                                │
└──────────────────┘                │       ▼                                │
                                    │  S-Curve Executor (3 adders/axis)      │
                                    │       │ pos*, vel*, acc*               │
                                    │       ▼                                │
                                    │  Position PID + Feedforward            │
                                    │       │ current reference              │
                                    │       ▼                                │
                                    │  Current PID                           │
                                    │       │ duty cycle                     │
                                    │       ▼                                │
                                    │  Center-Aligned PWM + Dead-Time        │
                                    │       │              │                 │
                                    │       │         ADC Trigger            │
                                    │       ▼              ▼                 │
                                    │   H-Bridge        ADC Interface        │
                                    │   (DRV8874)       (XADC / ext SPI)    │
                                    └───────┬──────────────┬─────────────────┘
                                            │              │
                                            ▼              ▼
                                       Galvo Coil    Pos/Cur Sensors
```

## Design Split: Host vs FPGA

| Function | Runs on | Rationale |
|---|---|---|
| ILDA stream parsing | Host PC | Complex protocol, easy in software |
| S-curve segment planning | Host PC | Complex math (segment time calculation) |
| Tuning / calibration UI | Host PC | User interface |
| S-curve execution (integration) | FPGA | Just 3 additions/cycle, deterministic |
| PID controllers | FPGA | Deterministic timing, low latency |
| PWM generation | FPGA | Sub-ns jitter, synchronized to ADC |
| ADC sampling | FPGA | Synchronized to PWM center |

## Key Architectural Advantages Over Standalone Galvo Drivers

1. **Integrated point engine**: The controller knows future trajectory points, enabling predictive feedforward. Standalone drivers (e.g., Pangolin) receive only analog setpoints with no future knowledge.

2. **S-curve trajectory planning**: Bounded-jerk profiles suppress mechanical resonance excitation at the source, rather than damping it after the fact.

3. **Feedforward from trajectory**: The S-curve executor outputs position, velocity, and acceleration at every cycle. These feed directly into feedforward terms:
   ```
   Current_cmd = PID(pos_error) + Kff_v × vel* + Kff_a × acc*
   ```

4. **Higher loop rates**: FPGA enables 500 kHz current loop (vs ~2.6 kHz in reference Turbotrack2 analog driver), giving ~200× faster disturbance rejection.
