"""Run a simple cascaded-loop step response simulation for a galvo axis."""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from pid import PIDController
from plant import CT6800_PARAMS, G120_PARAMS, GalvoPlant
from utils import adc_quantize, add_sensor_noise


def parse_args() -> argparse.Namespace:
    """Parse command-line options."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--galvo",
        choices=("g120", "ct6800"),
        default="g120",
        help="Plant parameter set to simulate.",
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=0.02,
        help="Simulation duration in seconds.",
    )
    return parser.parse_args()


def run_simulation(galvo_name: str, duration_s: float) -> Path:
    """Run cascaded current/position control step response and save a plot."""
    pwm_hz = 500_000.0
    outer_hz = 50_000.0
    dt = 1.0 / pwm_hz
    outer_divider = int(round(pwm_hz / outer_hz))
    supply_v = 36.0

    params = G120_PARAMS if galvo_name == "g120" else CT6800_PARAMS
    plant = GalvoPlant.from_params(params)

    # Conservative placeholder gains for bring-up; tune later per axis.
    pos_pid = PIDController(kp=8.0, ki=300.0, kd=0.0, dt=1.0 / outer_hz, output_limits=(-6.0, 6.0))
    cur_pid = PIDController(kp=6.0, ki=8000.0, kd=0.0, dt=dt, output_limits=(-supply_v, supply_v))

    steps = int(duration_s / dt)
    time = np.arange(steps) * dt
    theta_sp = np.zeros(steps)
    theta = np.zeros(steps)
    current = np.zeros(steps)

    # 10° step at t=1 ms.
    step_time = 1e-3
    step_rad = np.deg2rad(10.0)

    i_cmd = 0.0
    for k in range(steps):
        t = k * dt
        theta_sp[k] = step_rad if t >= step_time else 0.0

        # Position loop at reduced rate (outer loop).
        if k % outer_divider == 0:
            measured_theta = adc_quantize(add_sensor_noise(plant.theta, rms=np.deg2rad(0.002)))
            i_cmd = pos_pid.update(theta_sp[k], measured_theta)

        measured_i = adc_quantize(add_sensor_noise(plant.i, rms=0.002), full_scale=10.0)
        v_cmd = cur_pid.update(i_cmd, measured_i)
        plant.step(voltage=v_cmd, dt=dt)

        theta[k] = plant.theta
        current[k] = plant.i

    output_dir = Path(__file__).resolve().parent / "output"
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / f"step_response_{galvo_name}.png"

    fig, (ax_pos, ax_i) = plt.subplots(2, 1, figsize=(10, 7), sharex=True)
    ax_pos.plot(time * 1e3, np.rad2deg(theta_sp), label="Setpoint")
    ax_pos.plot(time * 1e3, np.rad2deg(theta), label="Position")
    ax_pos.set_ylabel("Angle [deg]")
    ax_pos.grid(True, alpha=0.3)
    ax_pos.legend()

    ax_i.plot(time * 1e3, current, color="tab:red", label="Coil current")
    ax_i.set_xlabel("Time [ms]")
    ax_i.set_ylabel("Current [A]")
    ax_i.grid(True, alpha=0.3)
    ax_i.legend()
    fig.suptitle(f"Galvo step response ({galvo_name.upper()})")
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)

    return output_path


if __name__ == "__main__":
    args = parse_args()
    result = run_simulation(args.galvo, args.duration)
    print(f"Saved plot to {result}")

