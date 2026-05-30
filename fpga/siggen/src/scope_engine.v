module scope_engine (
    input  wire        clk,
    input  wire        rst,

    // XADC interface
    output reg  [6:0]  xadc_daddr,
    output reg         xadc_den,
    input  wire [15:0] xadc_do,
    input  wire        xadc_drdy,
    input  wire        xadc_eoc,

    // Control (from SPI registers)
    input  wire [1:0]  trig_channel,   // 0-3
    input  wire        trig_edge,      // 0=rising, 1=falling
    input  wire [1:0]  trig_mode,      // 0=single, 1=normal, 2=manual
    input  wire [15:0] sample_div,     // clock divider for sample rate
    input  wire        arm,            // pulse to arm
    input  wire        force_trig,     // pulse for manual trigger

    // Status
    output reg  [1:0]  state,          // 0=IDLE, 1=ARMED, 2=TRIGGERED, 3=DONE
    output reg  [12:0] trig_ptr,       // trigger position in buffer

    // Buffer read interface
    input  wire [12:0] rd_addr,        // 0..5119 (1280 samples * 4 channels)
    output wire [11:0] rd_data
);
    localparam SAMPLES_PER_CH = 1280;
    localparam NUM_CHANNELS   = 4;
    localparam BUF_DEPTH      = SAMPLES_PER_CH * NUM_CHANNELS; // 5120
    localparam PRE_TRIG       = SAMPLES_PER_CH / 2;            // 640

    localparam ST_IDLE      = 2'd0;
    localparam ST_ARMED     = 2'd1;
    localparam ST_TRIGGERED = 2'd2;
    localparam ST_DONE      = 2'd3;

    // XADC channel addresses (VAUX0-3, adjust to match wiring)
    // XADC data registers: 0x10=VAUX0, 0x11=VAUX1, ..., 0x1F=VAUX15
    localparam [6:0] XADC_CH0_ADDR = 7'h10;  // VAUX0
    localparam [6:0] XADC_CH1_ADDR = 7'h11;  // VAUX1
    localparam [6:0] XADC_CH2_ADDR = 7'h18;  // VAUX8
    localparam [6:0] XADC_CH3_ADDR = 7'h19;  // VAUX9

    // Ring buffer (BRAM inferred)
    reg [11:0] buffer [0:BUF_DEPTH-1];
    reg [12:0] wr_ptr;
    reg [10:0] post_trig_count;  // counts down from PRE_TRIG (640 per channel pass = 640*4 total writes)

    // Sample rate divider
    reg [15:0] div_count;
    wire       sample_tick = (div_count == 0);

    // Channel sequencer (round-robin 0-3)
    reg [1:0]  seq_ch;
    reg [11:0] sample_data [0:3];

    // Trigger detection
    reg [11:0] prev_trig_sample;
    wire [11:0] cur_trig_sample = sample_data[trig_channel];
    wire        trig_rising  = (prev_trig_sample < 12'h800) && (cur_trig_sample >= 12'h800);
    wire        trig_falling = (prev_trig_sample >= 12'h800) && (cur_trig_sample < 12'h800);
    wire        trig_fire    = trig_edge ? trig_falling : trig_rising;

    // Buffer read port
    assign rd_data = buffer[rd_addr];

    // XADC channel address lookup
    function [6:0] xadc_addr;
        input [1:0] ch;
        begin
            case (ch)
                2'd0: xadc_addr = XADC_CH0_ADDR;
                2'd1: xadc_addr = XADC_CH1_ADDR;
                2'd2: xadc_addr = XADC_CH2_ADDR;
                2'd3: xadc_addr = XADC_CH3_ADDR;
            endcase
        end
    endfunction

    // Sample rate divider
    always @(posedge clk) begin
        if (rst || state == ST_IDLE || state == ST_DONE) begin
            div_count <= 0;
        end else begin
            if (div_count == sample_div) begin
                div_count <= 0;
            end else begin
                div_count <= div_count + 1'b1;
            end
        end
    end

    // Main sequencer, buffer write, and trigger logic (single always block)
    reg [1:0] seq_state;
    reg [1:0] wr_ch;
    reg       samples_valid;

    always @(posedge clk) begin
        if (rst) begin
            seq_ch <= 0;
            seq_state <= 0;
            wr_ch <= 0;
            wr_ptr <= 0;
            xadc_den <= 0;
            xadc_daddr <= 0;
            state <= ST_IDLE;
            trig_ptr <= 0;
            post_trig_count <= 0;
            prev_trig_sample <= 0;
            samples_valid <= 0;
        end else begin
            xadc_den <= 0;

            // Arm command
            if (arm && (state == ST_IDLE || state == ST_DONE)) begin
                state <= ST_ARMED;
                wr_ptr <= 0;
                samples_valid <= 0;
                seq_state <= 0;
            end

            // Manual/force trigger
            if (force_trig && state == ST_ARMED) begin
                state <= ST_TRIGGERED;
                trig_ptr <= wr_ptr;
                post_trig_count <= PRE_TRIG - 1;
            end

            // Sequencer state machine
            case (seq_state)
                2'd0: begin // idle, wait for sample_tick
                    if (sample_tick && (state == ST_ARMED || state == ST_TRIGGERED)) begin
                        seq_ch <= 0;
                        seq_state <= 2'd1;
                    end
                end
                2'd1: begin // issue XADC read
                    xadc_daddr <= xadc_addr(seq_ch);
                    xadc_den <= 1;
                    seq_state <= 2'd2;
                end
                2'd2: begin // wait for data ready
                    if (xadc_drdy) begin
                        sample_data[seq_ch] <= xadc_do[15:4];
                        if (seq_ch == 2'd3) begin
                            wr_ch <= 0;
                            seq_state <= 2'd3;
                        end else begin
                            seq_ch <= seq_ch + 1'b1;
                            seq_state <= 2'd1;
                        end
                    end
                end
                2'd3: begin // write samples to buffer one at a time
                    buffer[wr_ptr] <= sample_data[wr_ch];
                    wr_ptr <= (wr_ptr == BUF_DEPTH - 1) ? 13'd0 : wr_ptr + 1'b1;

                    if (wr_ch == 2'd3) begin
                        seq_state <= 2'd0;

                        // Buffer has wrapped — pre-trigger data available
                        if (wr_ptr >= (BUF_DEPTH - 1)) begin
                            samples_valid <= 1;
                        end

                        // Trigger detection
                        prev_trig_sample <= sample_data[trig_channel];
                        if (state == ST_ARMED && samples_valid) begin
                            if (trig_fire || trig_mode == 2'd2) begin
                                state <= ST_TRIGGERED;
                                trig_ptr <= wr_ptr;
                                post_trig_count <= PRE_TRIG - 1;
                            end
                        end

                        // Post-trigger countdown
                        if (state == ST_TRIGGERED) begin
                            if (post_trig_count == 0) begin
                                state <= ST_DONE;
                            end else begin
                                post_trig_count <= post_trig_count - 1'b1;
                            end
                        end
                    end else begin
                        wr_ch <= wr_ch + 1'b1;
                    end
                end
            endcase
        end
    end
endmodule
