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
    reg [15:0] tx_shift;
    reg [4:0] bit_count;
    reg       read_active;

    wire cs_active = ~spi_cs_n_sync[1];
    wire sclk_rise = (spi_sclk_sync[1:0] == 2'b01);
    wire [23:0] rx_word_next = {shift[22:0], spi_mosi_sync[1]};
    wire [6:0]  cmd_addr_next = rx_word_next[6:0];
    wire        cmd_is_read_next = rx_word_next[7];

    function [15:0] reg_read_data;
        input [6:0] addr;
        begin
            case (addr)
                7'h00: reg_read_data = ch0_phase_inc[15:0];
                7'h01: reg_read_data = ch0_phase_inc[31:16];
                7'h02: reg_read_data = {14'd0, ch0_waveform};
                7'h03: reg_read_data = ch0_amplitude;
                7'h04: reg_read_data = ch0_duty;
                7'h08: reg_read_data = ch1_phase_inc[15:0];
                7'h09: reg_read_data = ch1_phase_inc[31:16];
                7'h0A: reg_read_data = {14'd0, ch1_waveform};
                7'h0B: reg_read_data = ch1_amplitude;
                7'h0C: reg_read_data = ch1_duty;
                7'h10: reg_read_data = {14'd0, global_enable};
                default: reg_read_data = 16'd0;
            endcase
        end
    endfunction

    assign spi_miso = tx_shift[15];

    always @(posedge clk) begin
        if (rst) begin
            spi_sclk_sync <= 2'b00;
            spi_mosi_sync <= 2'b00;
            spi_cs_n_sync <= 2'b11;
            shift <= 24'd0;
            tx_shift <= 16'd0;
            bit_count <= 5'd0;
            read_active <= 1'b0;
            ch0_phase_inc <= 32'd0;
            ch0_waveform <= 2'd0;
            ch0_amplitude <= 16'h0000;
            ch0_duty <= 16'h8000;
            ch1_phase_inc <= 32'd0;
            ch1_waveform <= 2'd0;
            ch1_amplitude <= 16'h0000;
            ch1_duty <= 16'h8000;
            global_enable <= 2'b00;
        end else begin
            spi_sclk_sync <= {spi_sclk_sync[0], spi_sclk};
            spi_mosi_sync <= {spi_mosi_sync[0], spi_mosi};
            spi_cs_n_sync <= {spi_cs_n_sync[0], spi_cs_n};

            if (!cs_active) begin
                shift <= 24'd0;
                tx_shift <= 16'd0;
                bit_count <= 5'd0;
                read_active <= 1'b0;
            end else if (sclk_rise) begin
                shift <= rx_word_next;
                if (read_active) begin
                    tx_shift <= {tx_shift[14:0], 1'b0};
                end

                if (bit_count == 5'd7) begin
                    read_active <= cmd_is_read_next;
                    if (cmd_is_read_next) begin
                        tx_shift <= reg_read_data(cmd_addr_next);
                    end
                end

                if (bit_count == 5'd23) begin
                    bit_count <= 5'd0;
                    read_active <= 1'b0;
                    if (!rx_word_next[23]) begin
                        case (rx_word_next[22:16])
                            7'h00: ch0_phase_inc[15:0] <= rx_word_next[15:0];
                            7'h01: ch0_phase_inc[31:16] <= rx_word_next[15:0];
                            7'h02: ch0_waveform <= rx_word_next[1:0];
                            7'h03: ch0_amplitude <= rx_word_next[15:0];
                            7'h04: ch0_duty <= rx_word_next[15:0];
                            7'h08: ch1_phase_inc[15:0] <= rx_word_next[15:0];
                            7'h09: ch1_phase_inc[31:16] <= rx_word_next[15:0];
                            7'h0A: ch1_waveform <= rx_word_next[1:0];
                            7'h0B: ch1_amplitude <= rx_word_next[15:0];
                            7'h0C: ch1_duty <= rx_word_next[15:0];
                            7'h10: global_enable <= rx_word_next[1:0];
                            default: begin end
                        endcase
                    end
                end else begin
                    bit_count <= bit_count + 1'b1;
                end
            end
        end
    end
endmodule
