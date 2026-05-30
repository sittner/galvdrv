# Control Loop Design

## Loop Structure

Cascaded dual-loop per axis:

```
                    Outer (Position) Loop              Inner (Current) Loop
                    ──────────────────────              ────────────────────
Setpoint ──(-)──▶ Position PID + FF ──▶ I_ref ──(-)──▶ Current PID ──▶ PWM ──▶ H-bridge
   ▲                                                        ▲                      │
   │                                                        │                      ▼
   └───────────── Position ADC ◀──── Galvo ◀───── Current ADC ◀──── Coil ◀────────┘
```

### Loop Rates

| Loop | Rate | Bandwidth Target |
|---|---|---|
| Inner (current) | 500 kHz | ~50 kHz |
| Outer (position) | 50 kHz | ~2–5 kHz |

### Feedforward

The S-curve executor provides position, velocity, and acceleration at every cycle:

```
Current_command = PID_pos(pos_error)
               + Kff_velocity × ω*         (anticipates velocity)
               + Kff_acceleration × α*     (anticipates acceleration)
```

The PID only corrects small residual errors. Feedforward does the heavy lifting for known trajectories.

## S-Curve Trajectory Planning

### Why S-Curves

Traditional ILDA point streaming sends step commands → infinite jerk → excites galvo mechanical resonances → ringing and overshoot.

S-curve profiles bound jerk, resulting in smooth acceleration profiles that avoid resonance excitation.

### The Magic: Jerk Time = 1/(2 × f_resonance)

Setting the jerk phase duration `t_j = 1 / (2 × f_r)` creates a null in the S-curve's frequency content at exactly the resonance frequency. This is equivalent to built-in input shaping.

| Galvo | Resonance | Optimal t_j |
|---|---|---|
| G120 | ~1000 Hz | 0.5 ms |
| CT-6800 | ~250 Hz | 2.0 ms |

### 7-Segment Profile

```
Phase      Jerk       Result
─────      ────       ──────
1          +J_max     Accel ramps up
2          0          Constant acceleration (A_max)
3          -J_max     Accel ramps down to zero
4          0          Constant velocity (V_max)
5          -J_max     Decel ramps up
6          0          Constant deceleration
7          +J_max     Decel ramps down to zero
```

Short moves degenerate to 5 or 3 segments (never reaching V_max or A_max).

### FPGA Executor (Trivial)

The executor is just 3 cascaded integrators running every PWM cycle:

```verilog
accel <= accel + jerk;
vel   <= vel   + accel;
pos   <= pos   + vel;
```

~50 logic cells per axis. The complex planning math runs on the host PC and streams (jerk, duration) segment pairs to the FPGA via a FIFO.

## PWM Generation

### Center-Aligned PWM

Up/down counter creates symmetric switching:

```
Counter:  0 → peak → 0 → peak → ...
          ╱╲    ╱╲    ╱╲
         ╱  ╲  ╱  ╲  ╱  ╲

ADC trigger at peak (both FETs in same state = quiet moment)
```

Benefits:
- Synchronous ADC sampling at PWM center rejects switching noise
- Symmetric edges reduce even harmonics

### Dead-Time Insertion

~50–100 ns dead-time (5–10 clock cycles at 100 MHz) between high-side turn-off and low-side turn-on to prevent shoot-through. Generated in FPGA — precise and repeatable.

### Direction Control

For bipolar current through H-bridge:
- Duty > 50%: current flows one direction
- Duty < 50%: current flows opposite direction
- PID output is signed → maps to duty around center point

## Error Budget (5 m throw distance)

| Error Source | Magnitude | Visible? | Mitigation |
|---|---|---|---|
| Tracking error (corners, 30 kpps) | 50–200 mm | Yes | S-curve + feedforward |
| EMI → position sensor (good layout) | 0.3–1 mm | No | Sync ADC sampling, shielding |
| EMI → position sensor (bad layout) | 3–30 mm | Yes | Fix layout |
| PWM current ripple → position | < 0.001 mm | No | Mirror inertia filters it (-96 dB) |
| ADC quantization (16-bit) | 0.2–0.4 mm | No | Sufficient resolution |

### EMI Mitigation Priority

1. **Synchronous ADC sampling at PWM center** — the single most effective technique (free in FPGA)
2. Physical separation of power and sensor traces
3. 4-layer PCB with ground plane
4. Shielded sensor cables
5. Slower FET switching (gate resistor) if needed

## Performance Estimates vs Pangolin

| Metric | Pangolin FB4 (estimated) | This design (mature) |
|---|---|---|
| Current loop BW | ~10–20 kHz (DSP) | 50 kHz (FPGA) |
| Position loop BW | ~1–2 kHz | 2–5 kHz |
| Loop latency | Multiple µs (DSP pipeline) | < 1 µs (pipelined HDL) |
| Effective scan rate | 40–60 kpps | 60–80+ kpps (with S-curve) |
| Corner sharpness | Very good | Better (feedforward advantage) |
| Trajectory knowledge | None (analog setpoint) | Full (integrated point engine) |

The architectural advantage — knowing future trajectory — is fundamental and cannot be matched by standalone drivers receiving analog setpoints.
