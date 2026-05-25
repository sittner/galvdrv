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
