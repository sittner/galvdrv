module top (
    input  wire clk_100m,
    input  wire spi_sclk,
    input  wire spi_mosi,
    input  wire spi_cs_n,
    output wire spi_miso,
    output wire dac_sck,
    output wire dac_bck,
    output wire dac_din,
    output wire dac_lck
);
    wire clkfb;
    wire mmcm_clk_sck;
    wire mmcm_clk_logic;
    wire mmcm_locked;
    wire clk_sck;
    wire clk_logic_unused;

    MMCME2_BASE #(
        .CLKIN1_PERIOD(10.0),
        .CLKFBOUT_MULT_F(8.0),
        .DIVCLK_DIVIDE(1),
        .CLKOUT0_DIVIDE_F(25.0),
        .CLKOUT1_DIVIDE(8)
    ) mmcm_i (
        .CLKIN1(clk_100m),
        .CLKFBIN(clkfb),
        .RST(1'b0),
        .CLKFBOUT(clkfb),
        .CLKOUT0(mmcm_clk_sck),
        .CLKOUT1(mmcm_clk_logic),
        .LOCKED(mmcm_locked),
        .PWRDWN(1'b0)
    );

    BUFG bufg_sck (
        .I(mmcm_clk_sck),
        .O(clk_sck)
    );

    BUFG bufg_logic (
        .I(mmcm_clk_logic),
        .O(clk_logic_unused)
    );

    wire rst = ~mmcm_locked;

    wire [31:0] ch0_phase_inc;
    wire [1:0]  ch0_waveform;
    wire [15:0] ch0_amplitude;
    wire [15:0] ch0_duty;
    wire [31:0] ch1_phase_inc;
    wire [1:0]  ch1_waveform;
    wire [15:0] ch1_amplitude;
    wire [15:0] ch1_duty;
    wire [1:0]  global_enable;

    spi_slave spi_slave_i (
        .rst(rst),
        .spi_sclk(spi_sclk),
        .spi_mosi(spi_mosi),
        .spi_cs_n(spi_cs_n),
        .spi_miso(spi_miso),
        .ch0_phase_inc(ch0_phase_inc),
        .ch0_waveform(ch0_waveform),
        .ch0_amplitude(ch0_amplitude),
        .ch0_duty(ch0_duty),
        .ch1_phase_inc(ch1_phase_inc),
        .ch1_waveform(ch1_waveform),
        .ch1_amplitude(ch1_amplitude),
        .ch1_duty(ch1_duty),
        .global_enable(global_enable)
    );

    wire frame_tick;
    wire signed [15:0] sample0;
    wire signed [15:0] sample1;
    wire [31:0] phase0;
    wire [31:0] phase1;

    dds_core dds_ch0 (
        .clk(clk_sck),
        .rst(rst),
        .sample_tick(frame_tick),
        .phase_inc(ch0_phase_inc),
        .waveform(ch0_waveform),
        .amplitude(ch0_amplitude),
        .duty(ch0_duty),
        .enable(global_enable[0]),
        .phase(phase0),
        .sample(sample0)
    );

    dds_core dds_ch1 (
        .clk(clk_sck),
        .rst(rst),
        .sample_tick(frame_tick),
        .phase_inc(ch1_phase_inc),
        .waveform(ch1_waveform),
        .amplitude(ch1_amplitude),
        .duty(ch1_duty),
        .enable(global_enable[1]),
        .phase(phase1),
        .sample(sample1)
    );

    i2s_tx i2s_tx_i (
        .clk_sck(clk_sck),
        .rst(rst),
        .sample_l(sample0),
        .sample_r(sample1),
        .bck(dac_bck),
        .lrck(dac_lck),
        .din(dac_din),
        .frame_tick(frame_tick)
    );

    assign dac_sck = clk_sck;
endmodule
