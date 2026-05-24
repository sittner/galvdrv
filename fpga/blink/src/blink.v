module blink (
    input  wire       clk_100m,
    output wire       led1,
    output wire       led2,
    output wire       hr_cs_l,
    output wire       hr_rst_l,
    input  wire       hr_ck,
    input  wire       hr_rwds,
    input  wire [7:0] hr_dq
);
    localparam integer HALF_PERIOD_TICKS = 50_000_000;

    reg [25:0] counter = 26'd0;
    reg        phase = 1'b0;

    always @(posedge clk_100m) begin
        if (counter == HALF_PERIOD_TICKS - 1) begin
            counter <= 26'd0;
            phase <= ~phase;
        end else begin
            counter <= counter + 1'b1;
        end
    end

    assign led1 = phase;
    assign led2 = ~phase;

    assign hr_cs_l = 1'b1;
    assign hr_rst_l = 1'b0;

endmodule
