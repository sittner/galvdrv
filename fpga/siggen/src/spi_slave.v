module spi_slave (
    input  wire        rst,
    input  wire        spi_sclk,
    input  wire        spi_mosi,
    input  wire        spi_cs_n,
    output wire        spi_miso,
    output reg  [31:0] ch0_phase_inc,
    output reg  [1:0]  ch0_waveform,
    output reg  [15:0] ch0_amplitude,
    output reg  [15:0] ch0_duty,
    output reg  [31:0] ch1_phase_inc,
    output reg  [1:0]  ch1_waveform,
    output reg  [15:0] ch1_amplitude,
    output reg  [15:0] ch1_duty,
    output reg  [1:0]  global_enable
);
    reg [23:0] shift;
    reg [23:0] shift_next;
    reg [4:0] bit_count;

    assign spi_miso = 1'b0;

    always @(posedge spi_sclk or posedge spi_cs_n or posedge rst) begin
        if (rst) begin
            shift <= 24'd0;
            bit_count <= 5'd0;
            ch0_phase_inc <= 32'd0;
            ch0_waveform <= 2'd0;
            ch0_amplitude <= 16'hFFFF;
            ch0_duty <= 16'h8000;
            ch1_phase_inc <= 32'd0;
            ch1_waveform <= 2'd0;
            ch1_amplitude <= 16'hFFFF;
            ch1_duty <= 16'h8000;
            global_enable <= 2'b00;
        end else if (spi_cs_n) begin
            shift <= 24'd0;
            bit_count <= 5'd0;
        end else begin
            shift_next = {shift[22:0], spi_mosi};
            shift <= shift_next;
            if (bit_count == 5'd23) begin
                bit_count <= 5'd0;
                case (shift_next[23:16])
                    8'h00: ch0_phase_inc[15:0] <= shift_next[15:0];
                    8'h01: ch0_phase_inc[31:16] <= shift_next[15:0];
                    8'h02: ch0_waveform <= shift_next[1:0];
                    8'h03: ch0_amplitude <= shift_next[15:0];
                    8'h04: ch0_duty <= shift_next[15:0];
                    8'h08: ch1_phase_inc[15:0] <= shift_next[15:0];
                    8'h09: ch1_phase_inc[31:16] <= shift_next[15:0];
                    8'h0A: ch1_waveform <= shift_next[1:0];
                    8'h0B: ch1_amplitude <= shift_next[15:0];
                    8'h0C: ch1_duty <= shift_next[15:0];
                    8'h10: global_enable <= shift_next[1:0];
                    default: begin end
                endcase
            end else begin
                bit_count <= bit_count + 1'b1;
            end
        end
    end
endmodule
