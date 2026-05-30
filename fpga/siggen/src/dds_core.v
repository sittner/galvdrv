module dds_core (
    input  wire        clk,
    input  wire        rst,
    input  wire        sample_tick,
    input  wire [31:0] phase_inc,
    input  wire [1:0]  waveform,
    input  wire [15:0] amplitude,
    input  wire [15:0] duty,
    input  wire        enable,
    output reg  [31:0] phase,
    output reg  signed [15:0] sample
);
    wire [9:0] lut_phase = phase[31:22];
    wire [1:0] quadrant = lut_phase[9:8];
    wire [7:0] lut_idx = lut_phase[7:0];
    wire [7:0] lut_mirror_idx = 8'hFF - lut_idx;
    wire [7:0] lut_addr = quadrant[0] ? lut_mirror_idx : lut_idx;

    wire [15:0] lut_val;
    sine_lut lut_i (
        .clk(clk),
        .addr(lut_addr),
        .data(lut_val)
    );

    reg signed [15:0] raw_sample;
    reg signed [31:0] scaled_sample;

    wire square_hi = (phase[31:16] < duty);
    wire [15:0] triangle_u = phase[31] ? ~phase[30:15] : phase[30:15];

    always @* begin
        raw_sample = quadrant[1] ? -$signed(lut_val) : $signed(lut_val);

        case (waveform)
            2'd0: begin end
            2'd1: raw_sample = square_hi ? 16'sh7FFF : -16'sh8000;
            2'd2: raw_sample = $signed(phase[31:16]);
            default: raw_sample = $signed(triangle_u);
        endcase

        scaled_sample = raw_sample * $signed({1'b0, amplitude});
    end

    always @(posedge clk) begin
        if (rst) begin
            phase <= 32'd0;
            sample <= 16'sd0;
        end else if (sample_tick) begin
            phase <= phase + phase_inc;
            if (enable) begin
                sample <= scaled_sample[31:16];
            end else begin
                sample <= 16'sd0;
            end
        end
    end
endmodule
