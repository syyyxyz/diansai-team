create_clock -name clk_50m -period 20.000 [get_ports {clk}]
derive_pll_clocks
derive_clock_uncertainty

# Reset and SPI are asynchronous board inputs and are synchronized in RTL.
set_false_path -from [get_ports {rst_n spi_cs_n spi_sck spi_mosi ad_data[*]}]
