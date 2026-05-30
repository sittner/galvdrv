module top (
    input  wire clk_100m,
    input  wire spi_sclk,
    input  wire spi_mosi,
    input  wire spi_cs_n,
    output wire spi_miso,
    output wire dac_sck,
    output wire dac_bck,
    output wire dac_din,
    output wire dac_lck,
    // XADC analog inputs (active pairs only)
    input  wire [3:0] vauxp,
    input  wire [3:0] vauxn
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

    // Scope control signals
    wire [1:0]  scope_trig_ch;
    wire        scope_trig_edge;
    wire [1:0]  scope_trig_mode;
    wire [15:0] scope_sample_div;
    wire        scope_arm;
    wire        scope_force_trig;
    wire [1:0]  scope_state;
    wire [12:0] scope_trig_ptr;
    wire [12:0] scope_rd_addr;
    wire [11:0] scope_rd_data;

    spi_slave spi_slave_i (
        .clk(clk_sck),
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
        .global_enable(global_enable),
        .scope_trig_ch(scope_trig_ch),
        .scope_trig_edge(scope_trig_edge),
        .scope_trig_mode(scope_trig_mode),
        .scope_sample_div(scope_sample_div),
        .scope_arm(scope_arm),
        .scope_force_trig(scope_force_trig),
        .scope_state(scope_state),
        .scope_trig_ptr(scope_trig_ptr),
        .scope_rd_addr(scope_rd_addr),
        .scope_rd_data(scope_rd_data)
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

    // XADC
    wire [6:0]  xadc_daddr;
    wire        xadc_den;
    wire [15:0] xadc_do;
    wire        xadc_drdy;
    wire        xadc_eoc;

    XADC #(
        .INIT_40(16'h0000),         // Configuration reg 0: averaging off
        .INIT_41(16'h21AF),         // Configuration reg 1: continuous seq, calibration
        .INIT_42(16'h0400),         // Configuration reg 2: ADCCLK = clk/4
        .INIT_48(16'h0000),         // Sequencer ch sel: none (aux only)
        .INIT_49(16'h0303),         // Sequencer ch sel: VAUX0, VAUX1, VAUX8, VAUX9
        .INIT_4A(16'h0000),         // Sequencer averaging: none
        .INIT_4B(16'h0000),         // Sequencer averaging: none
        .INIT_4C(16'h0000),         // Sequencer input mode: unipolar
        .INIT_4D(16'h0000),         // Sequencer input mode: unipolar
        .INIT_4E(16'h0000),         // Sequencer settle time: none
        .INIT_4F(16'h0000),         // Sequencer settle time: none
        .SIM_MONITOR_FILE(""),
        .IS_CONVSTCLK_INVERTED(1'b0),
        .IS_DCLK_INVERTED(1'b0)
    ) xadc_i (
        .DCLK(clk_sck),
        .RESET(rst),
        .DEN(xadc_den),
        .DWE(1'b0),
        .DADDR(xadc_daddr),
        .DI(16'd0),
        .DO(xadc_do),
        .DRDY(xadc_drdy),
        .EOC(xadc_eoc),
        .EOS(),
        .BUSY(),
        .CHANNEL(),
        .JTAGBUSY(),
        .JTAGLOCKED(),
        .JTAGMODIFIED(),
        .MUXADDR(),
        .ALM(),
        .OT(),
        .CONVST(1'b0),
        .CONVSTCLK(1'b0),
        .VP(1'b0),
        .VN(1'b0),
        // Map 4 input pairs to VAUX0, VAUX1, VAUX8, VAUX9
        .VAUXP({6'b0, vauxp[3], vauxp[2], 6'b0, vauxp[1], vauxp[0]}),
        .VAUXN({6'b0, vauxn[3], vauxn[2], 6'b0, vauxn[1], vauxn[0]})
    );

    scope_engine scope_i (
        .clk(clk_sck),
        .rst(rst),
        .xadc_daddr(xadc_daddr),
        .xadc_den(xadc_den),
        .xadc_do(xadc_do),
        .xadc_drdy(xadc_drdy),
        .xadc_eoc(xadc_eoc),
        .trig_channel(scope_trig_ch),
        .trig_edge(scope_trig_edge),
        .trig_mode(scope_trig_mode),
        .sample_div(scope_sample_div),
        .arm(scope_arm),
        .force_trig(scope_force_trig),
        .state(scope_state),
        .trig_ptr(scope_trig_ptr),
        .rd_addr(scope_rd_addr),
        .rd_data(scope_rd_data)
    );

    assign dac_sck = clk_sck;
endmodule
