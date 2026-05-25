module spi_slave (
    input  wire        clk,
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
    reg [1:0] spi_sclk_sync;
    reg [1:0] spi_mosi_sync;
    reg [1:0] spi_cs_n_sync;

    reg [23:0] shift;
    reg [4:0] bit_count;

    wire cs_active = ~spi_cs_n_sync[1];
    wire sclk_rise = (spi_sclk_sync[1:0] == 2'b01);
    wire [23:0] rx_word_next = {shift[22:0], spi_mosi_sync[1]};

    assign spi_miso = 1'b0;

    always @(posedge clk) begin
        if (rst) begin
            spi_sclk_sync <= 2'b00;
            spi_mosi_sync <= 2'b00;
            spi_cs_n_sync <= 2'b11;
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
        end else begin
            spi_sclk_sync <= {spi_sclk_sync[0], spi_sclk};
            spi_mosi_sync <= {spi_mosi_sync[0], spi_mosi};
            spi_cs_n_sync <= {spi_cs_n_sync[0], spi_cs_n};

            if (!cs_active) begin
                shift <= 24'd0;
                bit_count <= 5'd0;
            end else if (sclk_rise) begin
                shift <= rx_word_next;
                if (bit_count == 5'd23) begin
                    bit_count <= 5'd0;
                    case (rx_word_next[23:16])
                        8'h00: ch0_phase_inc[15:0] <= rx_word_next[15:0];
                        8'h01: ch0_phase_inc[31:16] <= rx_word_next[15:0];
                        8'h02: ch0_waveform <= rx_word_next[1:0];
                        8'h03: ch0_amplitude <= rx_word_next[15:0];
                        8'h04: ch0_duty <= rx_word_next[15:0];
                        8'h08: ch1_phase_inc[15:0] <= rx_word_next[15:0];
                        8'h09: ch1_phase_inc[31:16] <= rx_word_next[15:0];
                        8'h0A: ch1_waveform <= rx_word_next[1:0];
                        8'h0B: ch1_amplitude <= rx_word_next[15:0];
                        8'h0C: ch1_duty <= rx_word_next[15:0];
                        8'h10: global_enable <= rx_word_next[1:0];
                        default: begin end
                    endcase
                end else begin
                    bit_count <= bit_count + 1'b1;
                end
            end
        end
    end
endmodule
