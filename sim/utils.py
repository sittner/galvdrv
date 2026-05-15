"""Simulation utility helpers for ADC/PWM non-ideal behavior."""

from __future__ import annotations

from collections import deque
from typing import Deque, Optional

import numpy as np


def adc_quantize(value: float, bits: int = 16, full_scale: float = 10.0) -> float:
    """Quantize a value as if measured by a signed ADC.

    Args:
        value: Analog value to quantize.
        bits: ADC resolution.
        full_scale: Positive full-scale value for ±full_scale range.
    """
    clipped = float(np.clip(value, -full_scale, full_scale))
    levels = 1 << bits
    step = (2.0 * full_scale) / levels
    return round((clipped + full_scale) / step) * step - full_scale


def add_sensor_noise(
    value: float, rms: float = 0.0, rng: Optional[np.random.Generator] = None
) -> float:
    """Add zero-mean Gaussian noise with configurable RMS."""
    if rms <= 0.0:
        return value
    generator = rng if rng is not None else np.random.default_rng()
    return value + float(generator.normal(0.0, rms))


def pwm_dead_time_compensation(
    duty_cycle: float, dead_time_s: float, pwm_period_s: float, direction: int = 1
) -> float:
    """Apply first-order duty compensation for switching dead-time loss.

    This compensates average voltage error by nudging duty in the intended
    current direction.
    """
    if pwm_period_s <= 0.0:
        return duty_cycle
    correction = dead_time_s / pwm_period_s
    compensated = duty_cycle + (correction if direction >= 0 else -correction)
    return float(np.clip(compensated, 0.0, 1.0))


class TransportDelayBuffer:
    """Fixed-length delay line for simulating transport/compute delay."""

    def __init__(self, delay_steps: int, initial_value: float = 0.0) -> None:
        if delay_steps < 0:
            raise ValueError("delay_steps must be >= 0")
        self._buffer: Deque[float] = deque(
            [initial_value] * (delay_steps + 1),
            maxlen=delay_steps + 1,
        )

    def push(self, value: float) -> float:
        """Push a new sample and return delayed output sample."""
        self._buffer.append(value)
        return self._buffer[0]
