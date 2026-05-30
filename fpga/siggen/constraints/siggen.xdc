set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]

set_property PACKAGE_PIN L5 [get_ports {clk_100m}]
set_property IOSTANDARD LVCMOS33 [get_ports {clk_100m}]
create_clock -period 10.000 -name clk_100m [get_ports clk_100m]
set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets clk_100m_IBUF]

set_property PACKAGE_PIN K3 [get_ports {dac_sck}]
set_property IOSTANDARD LVCMOS33 [get_ports {dac_sck}]
set_property PACKAGE_PIN L2 [get_ports {dac_bck}]
set_property IOSTANDARD LVCMOS33 [get_ports {dac_bck}]
set_property PACKAGE_PIN M2 [get_ports {dac_din}]
set_property IOSTANDARD LVCMOS33 [get_ports {dac_din}]
set_property PACKAGE_PIN M4 [get_ports {dac_lck}]
set_property IOSTANDARD LVCMOS33 [get_ports {dac_lck}]

set_property PACKAGE_PIN J12 [get_ports {spi_sclk}]
set_property IOSTANDARD LVCMOS33 [get_ports {spi_sclk}]
set_property PACKAGE_PIN M14 [get_ports {spi_mosi}]
set_property IOSTANDARD LVCMOS33 [get_ports {spi_mosi}]
set_property PACKAGE_PIN K12 [get_ports {spi_miso}]
set_property IOSTANDARD LVCMOS33 [get_ports {spi_miso}]
set_property PACKAGE_PIN M12 [get_ports {spi_cs_n}]
set_property IOSTANDARD LVCMOS33 [get_ports {spi_cs_n}]

## XADC analog inputs - UPDATE PIN ASSIGNMENTS to match TE0890 wiring
## These must be on XADC-capable dual-function pins (check UG480 for XC7S25 FTGB196)
## vauxp[0]/vauxn[0] = VAUX0, vauxp[1]/vauxn[1] = VAUX1
## vauxp[2]/vauxn[2] = VAUX8, vauxp[3]/vauxn[3] = VAUX9
#set_property PACKAGE_PIN ?? [get_ports {vauxp[0]}]
#set_property PACKAGE_PIN ?? [get_ports {vauxn[0]}]
#set_property PACKAGE_PIN ?? [get_ports {vauxp[1]}]
#set_property PACKAGE_PIN ?? [get_ports {vauxn[1]}]
#set_property PACKAGE_PIN ?? [get_ports {vauxp[2]}]
#set_property PACKAGE_PIN ?? [get_ports {vauxn[2]}]
#set_property PACKAGE_PIN ?? [get_ports {vauxp[3]}]
#set_property PACKAGE_PIN ?? [get_ports {vauxn[3]}]
