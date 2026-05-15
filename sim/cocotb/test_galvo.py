"""cocotb skeleton for HDL + plant co-simulation.

This file is intentionally a scaffold:
- HDL top-level and signal names are placeholders.
- The goal is to show structure for PWM drive, plant stepping, and ADC injection.
"""

from __future__ import annotations

import sys
from pathlib import Path

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer

SIM_DIR = Path(__file__).resolve().parents[1]
if str(SIM_DIR) not in sys.path:
    sys.path.insert(0, str(SIM_DIR))

from plant import G120_PARAMS, GalvoPlant


def _decode_hbridge_voltage(pwm_hi: bool, pwm_lo: bool, bus_voltage: float = 36.0) -> float:
    """Map complementary PWM leg states to an average bridge output voltage."""
    if pwm_hi and not pwm_lo:
        return bus_voltage
    if pwm_lo and not pwm_hi:
        return -bus_voltage
    return 0.0


@cocotb.test()
async def test_galvo_cosim_skeleton(dut):
    """Skeleton co-simulation test for PWM-driven galvo loop."""
    # Placeholder clock assumptions; align with actual HDL testbench clock.
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())
    plant = GalvoPlant.from_params(G120_PARAMS)
    dt = 1.0 / 500_000.0  # Inner loop / PWM integration timestep.

    # Bring DUT into reset (signal names are expected to be adjusted).
    dut.rst_n.value = 0
    await Timer(50, units="ns")
    dut.rst_n.value = 1

    for _ in range(1000):
        await RisingEdge(dut.clk)

        # Placeholder PWM decode: replace with actual duty extraction logic.
        pwm_high = bool(int(dut.pwm_hi.value))
        pwm_low = bool(int(dut.pwm_lo.value))
        drive_voltage = _decode_hbridge_voltage(pwm_high, pwm_low)

        # Advance analog plant from HDL drive command.
        plant.step(voltage=drive_voltage, dt=dt)

        # Inject quantized feedback into DUT ADC interfaces.
        # Example scaling:
        # - adc_pos: angle in microradians (rad * 1e6)
        # - adc_cur: current in 0.1 mA units (A * 1e4)
        # Replace with the exact fixed-point conventions used by RTL.
        # Production testbenches should additionally clamp/check values to the
        # destination signal width (ADC code range).
        dut.adc_pos.value = int(plant.theta * 1e6)
        dut.adc_cur.value = int(plant.i * 1e4)
