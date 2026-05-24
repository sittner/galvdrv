set script_dir [file dirname [file normalize [info script]]]
cd $script_dir

file mkdir [file join $script_dir build]

create_project -in_memory blink -part xc7s25ftgb196-1

read_verilog [file join $script_dir src blink.v]
read_xdc [file join $script_dir constraints te0890.xdc]

synth_design -top blink -part xc7s25ftgb196-1
opt_design
place_design
route_design

write_bitstream -force [file join $script_dir build blink.bit]

# SVF generation via virtual JTAG target
open_hw_manager
connect_hw_server
create_hw_target virtual_target
open_hw_target [get_hw_targets */xilinx_tcf/Xilinx/virtual_target]
set device [create_hw_device -part xc7s25ftgb196-1]
set_property PROGRAM.FILE [file join $script_dir build blink.bit] $device
program_hw_devices -svf_file [file join $script_dir build blink.svf] $device
close_hw_target
close_hw_manager

