"""Simple floating-point PID model for control-loop prototyping."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Tuple


@dataclass
class PIDController:
    """PID controller with output clamping and anti-windup.

    Anti-windup strategy:
    - Integrator is frozen when output is saturated and error would drive
      saturation further.
    """

    kp: float
    ki: float
    kd: float
    dt: float
    output_limits: Tuple[Optional[float], Optional[float]] = (None, None)
    integral: float = 0.0
    prev_error: float = 0.0

    def reset(self) -> None:
        """Reset controller dynamic state."""
        self.integral = 0.0
        self.prev_error = 0.0

    def _clamp(self, value: float) -> float:
        lo, hi = self.output_limits
        if lo is not None and value < lo:
            return lo
        if hi is not None and value > hi:
            return hi
        return value

    def update(self, setpoint: float, measurement: float) -> float:
        """Compute controller output for one sample."""
        error = setpoint - measurement
        derivative = (error - self.prev_error) / self.dt

        # Candidate integration before anti-windup check.
        candidate_integral = self.integral + error * self.dt
        unclamped = (
            self.kp * error
            + self.ki * candidate_integral
            + self.kd * derivative
        )
        clamped = self._clamp(unclamped)

        # Freeze integrator when saturated in the same direction as error.
        saturated_high = clamped != unclamped and clamped > 0 and error > 0
        saturated_low = clamped != unclamped and clamped < 0 and error < 0
        if not (saturated_high or saturated_low):
            self.integral = candidate_integral

        # Recompute with accepted integrator state to keep output coherent.
        output = self.kp * error + self.ki * self.integral + self.kd * derivative
        output = self._clamp(output)
        self.prev_error = error
        return output

