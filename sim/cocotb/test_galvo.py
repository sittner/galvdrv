"""cocotb skeleton for HDL + plant co-simulation.

This file is intentionally a scaffold:
- HDL top-level and signal names are placeholders.
- The goal is to show structure for PWM drive, plant stepping, and ADC injection.
"""

from __future__ import annotations

import sys
from pathlib import Path

import cocotb
from cocotb.triggers import RisingEdge, Timer

SIM_DIR = Path(__file__).resolve().parents[1]
if str(SIM_DIR) not in sys.path:
    sys.path.insert(0, str(SIM_DIR))

from plant import G120_PARAMS, GalvoPlant


@cocotb.test()
async def test_galvo_cosim_skeleton(dut):
    """Skeleton co-simulation test for PWM-driven galvo loop."""
    # Placeholder clock assumptions; align with actual HDL testbench clock.
    dut.clk.value = 0
    plant = GalvoPlant.from_params(G120_PARAMS)
    dt = 1.0 / 500_000.0  # Inner loop / PWM integration timestep.

    # Bring DUT into reset (signal names are expected to be adjusted).
    dut.rst_n.value = 0
    for _ in range(5):
        dut.clk.value = 0
        await Timer(5, units="ns")
        dut.clk.value = 1
        await Timer(5, units="ns")
    dut.rst_n.value = 1

    for _ in range(1000):
        # Clock tick.
        dut.clk.value = 0
        await Timer(5, units="ns")
        dut.clk.value = 1
        await RisingEdge(dut.clk)

        # Placeholder PWM decode: replace with actual duty extraction logic.
        pwm_high = float(dut.pwm_hi.value)
        pwm_low = float(dut.pwm_lo.value)
        drive_voltage = 36.0 if pwm_high and not pwm_low else -36.0 if pwm_low and not pwm_high else 0.0

        # Advance analog plant from HDL drive command.
        plant.step(voltage=drive_voltage, dt=dt)

        # Inject quantized feedback into DUT ADC interfaces.
        # Replace scaling to match fixed-point format in RTL.
        dut.adc_pos.value = int(plant.theta * 1e6)
        dut.adc_cur.value = int(plant.i * 1e4)
