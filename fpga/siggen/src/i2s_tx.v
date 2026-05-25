module i2s_tx (
    input  wire              clk_sck,
    input  wire              rst,
    input  wire signed [15:0] sample_l,
    input  wire signed [15:0] sample_r,
    output reg               bck,
    output reg               lrck,
    output reg               din,
    output reg               frame_tick
);
    reg [2:0] sck_div;
    reg [4:0] bit_idx;
    reg signed [15:0] latched_l;
    reg signed [15:0] latched_r;

    function automatic [0:0] i2s_bit;
        input [4:0] idx;
        input signed [15:0] left;
        input signed [15:0] right;
        begin
            if (idx < 5'd16) begin
                i2s_bit = left[15 - idx];
            end else begin
                i2s_bit = right[31 - idx];
            end
        end
    endfunction

    always @(posedge clk_sck) begin
        if (rst) begin
            sck_div <= 3'd0;
            bit_idx <= 5'd0;
            bck <= 1'b0;
            lrck <= 1'b0;
            din <= 1'b0;
            frame_tick <= 1'b0;
            latched_l <= 16'sd0;
            latched_r <= 16'sd0;
        end else begin
            frame_tick <= 1'b0;
            sck_div <= sck_div + 1'b1;

            if (sck_div == 3'd3) begin
                bck <= 1'b1;
            end

            if (sck_div == 3'd7) begin
                bck <= 1'b0;

                if (bit_idx == 5'd31) begin
                    bit_idx <= 5'd0;
                    frame_tick <= 1'b1;
                    latched_l <= sample_l;
                    latched_r <= sample_r;
                end else begin
                    bit_idx <= bit_idx + 1'b1;
                end

                lrck <= (bit_idx >= 5'd15);
                din <= i2s_bit(bit_idx, latched_l, latched_r);
            end
        end
    end
endmodule
