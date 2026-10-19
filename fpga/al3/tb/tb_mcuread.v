// MCU readout timing testbench (fpga/al3/src/zaklad.v, 0x1539). Run with tb/mcuread.sh or tb/run.sh (ONLY=mcuread).
//
// Bench fault on 0x1538 (fw 0.2.67 diag, 2026-10-19): bytes read from the sample memory near code 128 came back as
// mixes of two successive bytes (0x7F / 0x80 -> 0xFF, 0xC3, ...). 0x1532..0x1538 load the read byte o_mcu_data on the
// rising edge of i_mcu_clk, and the MCU (fpga_read_byte: clock low, clock high, read the port) samples the bus right
// after that same edge. TD's report with i_mcu_clk declared as a clock gives 8.8 to 10.9 ns from the clock pad to the
// data pads (TT 85 C), different per bit, so a sample taken sooner than that sees some bits of the new byte and some
// of the old one. 0x1539 loads the byte on the falling edge (the start of the strobe).
//
// The MCU model here is the firmware's: control lines set, then the clock pulsed low for t_low ns, then the bus
// sampled t_s ns after the rising edge, then the next access. The pads are modelled as per-bit transport delays from
// o_mcu_data to the bus the MCU sees (TD's clock-pad-to-data-pad delays of the 0x1538 build, +margin), with the RTL
// flop itself at zero delay. The sample memory is preloaded with patterns that flip many bits between words (0x7F /
// 0x80, 0x00 / 0xFF, a ramp through all 256 codes, random), then read with the firmware's sequences:
//   - READ_RAW (pc_hal_read_core): 0x1F = start, command 0x20..0x23, one dummy strobe, n reads: word start + i;
//   - the 1.00o5 readout (fpga_read_sample_data, no dummy strobe): the first byte is word start, then start + i - 1;
//   - 0x06 (two bytes, mcu_state) and 0x14 (trigger address, two bytes).
// Sweep: t_low 12 .. 150 ns, t_s 0 .. 40 ns, random gaps. +expect_mix=1 (the 0x1538 reference run): reads with
// t_s >= the slowest pad delay must be right, and some reads with a shorter t_s must be mixed (the bench mechanism);
// +expect_mix=0 (0x1539): every read must be right at every t_s.
`timescale 1ns / 1ps

module tb;

  reg i_xtal = 0;
  always #10 i_xtal = ~i_xtal;

  reg       mcu_clk  = 1;
  reg       mcu_rws  = 1;
  reg       mcu_dcs  = 0;
  reg [7:0] mcu_dout = 0;

  wire [7:0] bus;
  assign bus = mcu_rws ? mcu_dout : 8'bz;

  // the MCU's view of the bus: FPGA-driven bits arrive after their pad delay
  real pd [0:7];
  reg [7:0] seen = 0;
  genvar g;
  generate
    for (g = 0; g < 8; g = g + 1) begin : pads
      always @(bus[g]) seen[g] <= #(pd[g]) bus[g];
    end
  endgenerate

  wire pad;
  assign (weak0, weak1) pad = 1'b0;
  wire e1A, e1B, e2A, e2B, o1, r11, r12, r13, ac1, o2, r21, r22, r23, ac2, pwm;

  fnirsi_1013D dut (
    .i_xtal(i_xtal), .i_mcu_clk(mcu_clk), .i_mcu_rws(mcu_rws), .i_mcu_dcs(mcu_dcs),
    .i_adc1A_d(8'd128), .i_adc1B_d(8'd128), .i_adc2A_d(8'd128), .i_adc2B_d(8'd128),
    .io_mcu_data(bus), .o_1khz_calib(pad),
    .o_adc1_encA(e1A), .o_adc1_encB(e1B), .o_adc2_encA(e2A), .o_adc2_encB(e2B),
    .o_offset_1(o1), .o_relay1_1(r11), .o_relay1_2(r12), .o_relay1_3(r13), .o_ac_dc_1(ac1),
    .o_offset_2(o2), .o_relay2_1(r21), .o_relay2_2(r22), .o_relay2_3(r23), .o_ac_dc_2(ac2),
    .o_pwm_display(pwm));

  integer errors = 0, checks = 0, seed, expect_mix;
  integer mixed_short = 0, reads_short = 0, bad_long = 0, reads_long = 0;
  real pd_max;

  task check(input cond, input [8*120-1:0] what);
    begin
      checks = checks + 1;
      if (!cond) begin
        errors = errors + 1;
        $display("FAIL  t=%0t  %0s", $time, what);
      end
    end
  endtask

  //---------------------------------------------------------------------------------
  // MCU bus: setup, clock low t_low, clock high, sample t_s after the rising edge, rest

  real t_low = 40.0, t_s = 20.0, t_gap = 30.0;
  reg [7:0] rd_d;

  task strobe(input sample);
    begin
      #(5.0 + 0.01 * ($random(seed) & 255)) mcu_clk = 0;
      #(t_low) mcu_clk = 1;
      if (sample) begin #(t_s) rd_d = seen; end
      #(t_gap + 0.01 * ($random(seed) & 255));
    end
  endtask

  task wr_cmd(input [7:0] c);  begin mcu_rws = 1; mcu_dcs = 1; mcu_dout = c; strobe(0); mcu_dcs = 0; end endtask
  task wr_byte(input [7:0] d); begin mcu_rws = 1; mcu_dcs = 0; mcu_dout = d; strobe(0); end endtask
  task rd_byte;                begin mcu_rws = 0; mcu_dcs = 0; strobe(1); end endtask
  task dummy;                  begin mcu_rws = 0; mcu_dcs = 0; strobe(0); end endtask
  task wr2(input [7:0] c, input [15:0] v); begin wr_cmd(c); wr_byte(v[15:8]); wr_byte(v[7:0]); end endtask

  //---------------------------------------------------------------------------------
  // Patterns: lane 0 alternates 0x7F / 0x80, lane 1 a ramp through all 256 codes (every boundary: 0x0F/0x10, 0x7F/0x80,
  // 0x8F/0x90, 0xFF/0x00, ...), lane 2 0x00 / 0xFF, lane 3 random (any pair of codes)

  integer i, n, k, start, rep, lane, nd, ok;
  reg [7:0] rnd8, ramp8;
  reg [7:0] exp_b, got;

  task fill;
    begin
      for (i = 0; i < 8192; i = i + 1) begin
        rnd8  = $random(seed);
        ramp8 = i % 256;
        dut.samples.mem[i] = { rnd8, (i & 1) ? 8'hFF : 8'h00, ramp8, (i & 1) ? 8'h80 : 8'h7F };
      end
    end
  endtask

  function [7:0] lane_of(input [31:0] w, input integer l);
    lane_of = w[8 * l +: 8];
  endfunction

  // one read of n bytes of lane l from word start, with or without the dummy strobe; counts mismatches
  integer bad;
  task read_lane(input integer l, input integer st, input integer cnt, input integer with_dummy);
    begin
      wr2(8'h1F, st);
      wr_cmd(8'h20 + l);
      mcu_rws = 0; mcu_dcs = 0;
      if (with_dummy) dummy;
      bad = 0;
      for (k = 0; k < cnt; k = k + 1) begin
        rd_byte;
        if (with_dummy) exp_b = lane_of(dut.samples.mem[(st + k) & 8191], l);
        else            exp_b = lane_of(dut.samples.mem[(st + ((k == 0) ? 0 : k - 1)) & 8191], l);
        if (rd_d !== exp_b) begin
          bad = bad + 1;
          if (bad <= 2 && (expect_mix == 0 || t_s >= pd_max))
            $display("  mismatch: t_low %0.1f t_s %0.1f lane %0d dummy %0d word %0d: read %h expected %h",
                     t_low, t_s, l, with_dummy, k, rd_d, exp_b);
        end
      end
      mcu_rws = 1;
    end
  endtask

  real tl [0:4];
  real ts [0:13];
  integer a, b;

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    if (!$value$plusargs("expect_mix=%d", expect_mix)) expect_mix = 0;
    // TD 0x1538: io_mcu_data clock pad to data pad 9.41 .. 10.85 ns (bits 1, 7, 6, 0, 2, 3, 5, 4), +0.1 ns
    pd[0] = 9.77; pd[1] = 9.51; pd[2] = 9.87; pd[3] = 10.17; pd[4] = 10.95; pd[5] = 10.24; pd[6] = 9.71; pd[7] = 9.67;
    pd_max = 10.95;
    tl[0] = 12.0; tl[1] = 20.0; tl[2] = 40.0; tl[3] = 97.0; tl[4] = 150.0;
    ts[0] = 0.0; ts[1] = 2.0; ts[2] = 5.0; ts[3] = 8.0; ts[4] = 9.0; ts[5] = 9.6; ts[6] = 9.8; ts[7] = 10.0;
    ts[8] = 10.5; ts[9] = 11.0; ts[10] = 12.0; ts[11] = 15.0; ts[12] = 25.0; ts[13] = 40.0;
    $display("tb_mcuread: seed %0d, expect_mix %0d", seed, expect_mix);
    #300;
    fill;
    wr_cmd(8'h50); wr_byte(8'h00);

    for (a = 0; a < 5; a = a + 1)
      for (b = 0; b < 14; b = b + 1) begin
        t_low = tl[a]; t_s = ts[b];
        t_gap = 20.0 + (($random(seed) & 32'h7FFFFFFF) % 100);
        // version word: two bytes through mcu_state
        wr_cmd(8'h06); rd_byte; got = rd_d; rd_byte;
        nd = ((got === 8'h15) && (rd_d === 8'h39 || rd_d === 8'h38)) ? 0 : 1;
        // every lane, with and without the dummy strobe, 48 words from a random start
        for (rep = 0; rep < 2; rep = rep + 1)
          for (lane = 0; lane < 4; lane = lane + 1) begin
            start = ($random(seed) & 32'h7FFFFFFF) % 8192;
            read_lane(lane, start, 48, 1 - rep);
            nd = nd + bad;
          end
        // the whole ramp, up and through the wrap: 300 consecutive words of lane 1, both read sequences
        for (rep = 0; rep < 2; rep = rep + 1) begin
          start = ($random(seed) & 32'h7FFFFFFF) % 8192;
          read_lane(1, start, 300, 1 - rep);
          nd = nd + bad;
        end
        if (t_s >= pd_max) begin
          reads_long = reads_long + 1;
          if (nd) bad_long = bad_long + 1;
          check(nd == 0, "a read sampled after the slowest pad delay returns the stored bytes");
        end else begin
          reads_short = reads_short + 1;
          if (nd) mixed_short = mixed_short + 1;
          if (!expect_mix) check(nd == 0, "0x1539: a read sampled at any time after the rising edge returns the stored bytes");
        end
        $display("  t_low %6.1f ns  t_s %5.1f ns: %0d wrong bytes", t_low, t_s, nd);
      end

    if (expect_mix)
      check(mixed_short > 0, "reference: reads sampled sooner than the pad delay after the rising edge are mixed");
    $display("sweep points sampled before the slowest pad delay: %0d, with wrong bytes %0d; after it: %0d, with wrong bytes %0d",
             reads_short, mixed_short, reads_long, bad_long);
    $display("RESULT: %0d checks, %0d failed -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #500_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
