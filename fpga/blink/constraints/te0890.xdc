set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]

set_property PACKAGE_PIN L5  [get_ports {clk_100m}]
set_property IOSTANDARD LVCMOS33 [get_ports {clk_100m}]
create_clock -period 10.000 -name clk_100m [get_ports clk_100m]
set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets clk_100m_IBUF]

set_property PACKAGE_PIN D14 [get_ports {led1}]
set_property IOSTANDARD LVCMOS33 [get_ports {led1}]
set_property PACKAGE_PIN C14 [get_ports {led2}]
set_property IOSTANDARD LVCMOS33 [get_ports {led2}]

set_property PACKAGE_PIN P2 [get_ports {hr_cs_l}]
set_property IOSTANDARD LVCMOS33 [get_ports {hr_cs_l}]

set_property PACKAGE_PIN P3 [get_ports {hr_rst_l}]
set_property IOSTANDARD LVCMOS33 [get_ports {hr_rst_l}]

set_property PACKAGE_PIN N1 [get_ports {hr_ck}]
set_property IOSTANDARD LVCMOS33 [get_ports {hr_ck}]
set_property PULLDOWN true [get_ports {hr_ck}]

set_property PACKAGE_PIN P4 [get_ports {hr_rwds}]
set_property IOSTANDARD LVCMOS33 [get_ports {hr_rwds}]
set_property PULLDOWN true [get_ports {hr_rwds}]

set_property PACKAGE_PIN P11 [get_ports {hr_dq[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hr_dq[0]}]
set_property PULLDOWN true [get_ports {hr_dq[0]}]

set_property PACKAGE_PIN P12 [get_ports {hr_dq[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hr_dq[1]}]
set_property PULLDOWN true [get_ports {hr_dq[1]}]

set_property PACKAGE_PIN N4 [get_ports {hr_dq[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hr_dq[2]}]
set_property PULLDOWN true [get_ports {hr_dq[2]}]

set_property PACKAGE_PIN P10 [get_ports {hr_dq[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hr_dq[3]}]
set_property PULLDOWN true [get_ports {hr_dq[3]}]

set_property PACKAGE_PIN P5 [get_ports {hr_dq[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hr_dq[4]}]
set_property PULLDOWN true [get_ports {hr_dq[4]}]

set_property PACKAGE_PIN N10 [get_ports {hr_dq[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hr_dq[5]}]
set_property PULLDOWN true [get_ports {hr_dq[5]}]

set_property PACKAGE_PIN N11 [get_ports {hr_dq[6]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hr_dq[6]}]
set_property PULLDOWN true [get_ports {hr_dq[6]}]

set_property PACKAGE_PIN P13 [get_ports {hr_dq[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {hr_dq[7]}]
set_property PULLDOWN true [get_ports {hr_dq[7]}]
