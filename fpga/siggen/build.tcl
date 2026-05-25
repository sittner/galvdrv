set script_dir [file dirname [file normalize [info script]]]
cd $script_dir

file mkdir [file join $script_dir build]

create_project -in_memory siggen -part xc7s25ftgb196-1

read_verilog [file join $script_dir src sine_lut.v]
read_verilog [file join $script_dir src dds_core.v]
read_verilog [file join $script_dir src i2s_tx.v]
read_verilog [file join $script_dir src spi_slave.v]
read_verilog [file join $script_dir src top.v]
read_xdc [file join $script_dir constraints siggen.xdc]

synth_design -top top -part xc7s25ftgb196-1
opt_design
place_design
route_design

write_bitstream -force [file join $script_dir build siggen.bit]
