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

// AL_LOGIC_BRAM (TD logic block RAM macro) as used by the 0x1536 sequencer RAMs: simple dual port
// ("PDPW": port A writes, port B reads), equal port widths, NORMAL write mode, REGMODE_B "NOREG" or
// "OUTREG" (one more clock of read latency, oceb = 1). Addresses are sampled through a 1 ps delay
// like sample_memory above, so a same-time-step update of an address register cannot race the read.
module AL_LOGIC_BRAM #(
  parameter DATA_WIDTH_A = 9, parameter DATA_WIDTH_B = DATA_WIDTH_A,
  parameter ADDR_WIDTH_A = 10, parameter ADDR_WIDTH_B = ADDR_WIDTH_A,
  parameter DATA_DEPTH_A = 2 ** ADDR_WIDTH_A, parameter DATA_DEPTH_B = 2 ** ADDR_WIDTH_B,
  parameter MODE = "DP", parameter REGMODE_A = "NOREG", parameter REGMODE_B = "NOREG",
  parameter WRITEMODE_A = "NORMAL", parameter WRITEMODE_B = "NORMAL", parameter RESETMODE = "SYNC",
  parameter IMPLEMENT = "9K", parameter INIT_FILE = "NONE", parameter FILL_ALL = "NONE"
) (
  input  [DATA_WIDTH_A-1:0] dia,
  input  [ADDR_WIDTH_A-1:0] addra,
  input  cea, ocea, clka, wea, rsta,
  input  bea,
  input  [DATA_WIDTH_B-1:0] dib,
  input  [ADDR_WIDTH_B-1:0] addrb,
  input  ceb, oceb, clkb, web, rstb,
  input  beb,
  output [DATA_WIDTH_A-1:0] doa,
  output [DATA_WIDTH_B-1:0] dob
);
  reg [DATA_WIDTH_A-1:0] mem [0:DATA_DEPTH_A-1];
  reg [DATA_WIDTH_B-1:0] lat_b = 0, q_b = 0;
  wire [ADDR_WIDTH_A-1:0] addra_q;
  wire [ADDR_WIDTH_B-1:0] addrb_q;
  assign #0.001 addra_q = addra;
  assign #0.001 addrb_q = addrb;
  integer i;
  initial for (i = 0; i < DATA_DEPTH_A; i = i + 1) mem[i] = 0;
  always @(posedge clka) if (cea & wea) mem[addra_q] <= dia;
  always @(posedge clkb) begin
    if (ceb & ~web) lat_b <= mem[addrb_q];
    if (oceb) q_b <= lat_b;
  end
  assign dob = (REGMODE_B == "OUTREG") ? q_b : lat_b;
  assign doa = 0;
endmodule

// AL_LOGIC_IDDR (TD logic DDR input register, 0x153A ADC capture), as TD's sim/al/al_logic_io.v without the global
// reset: q1 = d at the rising edge, q2 = d at the falling edge, both handed over on the next rising edge.
module AL_LOGIC_IDDR #(parameter SRMODE = "SYNC") (output q1, output q2, input clk, input d, input rst);
  reg c0 = 0, c1 = 0, t0 = 0, t1 = 0;
  always @(negedge clk) c0 <= d;
  always @(posedge clk) begin c1 <= c0; t0 <= d; t1 <= t0; end
  assign q1 = t1;
  assign q2 = c1;
endmodule
