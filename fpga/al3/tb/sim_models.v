// Behavioural stand-ins for the Anlogic IP in fpga/al3/src/al_ip, for iverilog only.
// pll: clk0 = 200 MHz at 0 degrees, clk1 = 200 MHz at 90 degrees (1.25 ns later), as pll.ipc.
// sample_memory: 8192 x 32 simple dual port, synchronous write on clka when cea, synchronous
// read on clkb (REGMODE NOREG: data valid after the clock edge). The read address is sampled
// through a 1 ps transport delay so a blocking update of the address register in the same
// time step (zaklad.v uses `sample_read_offset = ... + 1`) cannot race the read, as in hardware.
`timescale 1ns / 1ps

module pll (input refclk, input reset, output reg clk0_out = 0, output reg clk1_out = 0);
  always #2.5 clk0_out = ~clk0_out;
  initial begin
    #1.25;
    forever #2.5 clk1_out = ~clk1_out;
  end
endmodule

module sample_memory (
  input  [31:0] dia,
  input  [12:0] addra,
  input         cea,
  input         clka,
  output reg [31:0] dob = 0,
  input  [12:0] addrb,
  input         clkb
);
  reg [31:0] mem [0:8191];
  wire [12:0] addrb_q;
  assign #0.001 addrb_q = addrb;
  integer i;
  initial for (i = 0; i < 8192; i = i + 1) mem[i] = 0;
  always @(posedge clka) if (cea) mem[addra] <= dia;
  always @(posedge clkb) dob <= mem[addrb_q];
endmodule
