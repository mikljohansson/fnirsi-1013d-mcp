// Encode phase testbench (fpga/al3/README.md "Changes" section 8): the four ADC encode pins against the write clock
// after the firmware's write sequences, with the write instants swept over the clk_200MHz phase.
// Run with fpga/al3/tb/run.sh (ONLY=encphase) or encphase.sh, which also builds the delay-annotated variants.
//
// The design under test may be delay-annotated by encphase.sh: the D input of each encode flop then reaches it
// through a transport delay (`DA1, `DA2 for the A flops of chip 1 / 2, `DB1, `DB2 for B), as a stand-in for the
// routed delay of the cross-clock path into the flop. iverilog has no setup check, so a delay longer than the
// path's window simply means the flop takes the value from before the launching edge.
//
// Every sequence copies the firmware (fpga_control.c, pc_hal_scope.c): a UI arm (fpga_set_sample_rate 0x0D, then
// pc_glue_decim_conversion: 0x53 / 0x56 when the cached mode differs, 0x0F, sequencer 0x57..0x5C, 0x01 = 1, the
// 0x05 poll, 0x0B, 0x0C, 0x0E, 0x01 = 0) and a host arm (SET_TIMEBASE raw 0x0D, pc_hal_arm: 0x17, 0x53 / 0x56,
// 0x0F, 0x1A, 0x57..0x5C, 0x01 = 1, 0x05, 0x0B, 0x0C, 0x0E, 0x01 = 0). The first strobe of each sequence starts at
// a phase swept in 0.1 ns steps over the 5 ns clk_200MHz period, with strobe periods of 41 to 298 ns.
//
// Checked on every rising clk_200MHz edge from 2 write clock periods after the last 0x0D / 0x53 write, with the
// write clock as it is after the preceding falling edge and the encode flops 1.25 ns after it:
//   pol[p]: encode pin p equals the write clock (B pins) or its complement (A pins); counted per pin.
// Pass criterion (+exp_ok=1, the default, for 0x1538): every check of every pin right, and read 0x33 bit 0 = 0
// after each capture. +exp_ok=0 only reports (for 0x1535 / 0x1537 and annotated variants).
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

  integer errors = 0, checks = 0, seed = 1, exp_ok = 1, has33 = 1;

  task check(input cond, input [8*120-1:0] what);
    begin
      checks = checks + 1;
      if (!cond) begin
        errors = errors + 1;
        if (errors <= 20) $display("FAIL  t=%0t  %0s", $time, what);
      end
    end
  endtask

  //---------------------------------------------------------------------------------
  // MCU bus (as tb_trigpos.v)

  real stb = 43.0;

  task mcu_strobe;
    begin
      #(0.3 * stb) mcu_clk = 0;
      #(0.4 * stb) mcu_clk = 1;
      #(0.3 * stb);
    end
  endtask

  task wr_cmd(input [7:0] c);  begin mcu_rws = 1; mcu_dcs = 1; mcu_dout = c; mcu_strobe; mcu_dcs = 0; end endtask
  task wr_byte(input [7:0] d); begin mcu_rws = 1; mcu_dcs = 0; mcu_dout = d; mcu_strobe; end endtask
  reg [7:0] rd_d;
  task rd_byte; begin mcu_rws = 0; mcu_dcs = 0; mcu_strobe; rd_d = bus; mcu_rws = 1; end endtask
  task wr1(input [7:0] c, input [7:0] d);  begin wr_cmd(c); wr_byte(d); end endtask
  task wr2(input [7:0] c, input [15:0] v); begin wr_cmd(c); wr_byte(v[15:8]); wr_byte(v[7:0]); end endtask
  task wr4(input [7:0] c, input [31:0] v);
    begin wr_cmd(c); wr_byte(v[31:24]); wr_byte(v[23:16]); wr_byte(v[15:8]); wr_byte(v[7:0]); end
  endtask

  //---------------------------------------------------------------------------------
  // Monitor: on every rising clk_200MHz edge (2.5 ns after the falling edge that may toggle the write clock,
  // 1.25 ns after the encode flops' edge)

  reg     mon_on = 0;
  integer nchk = 0;
  integer good [0:3];                                   // per pin: e1B, e1A, e2B, e2A right
  integer k;
  initial for (k = 0; k < 4; k = k + 1) good[k] = 0;

  always @(posedge dut.clk_200MHz)
    if (mon_on) begin
      nchk = nchk + 1;
      if (e1B === dut.sample_write_clock)  good[0] = good[0] + 1;
      if (e1A === ~dut.sample_write_clock) good[1] = good[1] + 1;
      if (e2B === dut.sample_write_clock)  good[2] = good[2] + 1;
      if (e2A === ~dut.sample_write_clock) good[3] = good[3] + 1;
    end

  //---------------------------------------------------------------------------------
  // Firmware sequences

  integer cur_k = 0, cur_d = 0, polls;

  task settle_window(input integer d);                  // 2 write clock periods (D + 1) x 10 ns, then monitor (0x153D: D = 0 always)
    begin
      #(20 * (d + 1) + 20);
      mon_on = 1;
    end
  endtask

  // 0x0D (if changed), 0x53 / 0x56 (if changed) as fpga_set_sample_rate + pc_dec_apply, then the arm and release
  task arm(input integer d, input integer kk, input host);
    begin
      mon_on = 0;
      if (host) begin
        wr4(8'h0D, d);                                  // SET_TIMEBASE raw
        wr1(8'h17, 8'd128);                             // pc_hal_arm: fpga_set_trigger_level
      end else
        wr4(8'h0D, d);                                  // fpga_set_sample_rate
      if (kk != cur_k) begin
        wr1(8'h53, kk[7:0]);
        wr1(8'h56, 8'h00);
        if (kk != 0) wr1(8'h54, 8'd0);
      end
      cur_k = kk; cur_d = d;
      wr1(8'h0F, 8'h00);
      wr1(8'h1A, host ? 8'h01 : 8'h00);
      wr1(8'h57, 8'h00); wr1(8'h58, 8'h00); wr2(8'h59, 16'd0); wr2(8'h5A, 16'd0); wr1(8'h5B, 8'h00);
      wr4(8'h5C, 32'd0);
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05);
      polls = 0;
      rd_byte;
      while (rd_d[0] == 0 && polls < 2000) begin rd_byte; polls = polls + 1; end
      wr2(8'h0B, 16'd20);
      wr2(8'h0C, 16'd60);
      wr4(8'h0E, 32'd100);
      wr1(8'h01, 8'h00);
      settle_window(0);                                 // 0x153D: the write clock runs at 100 MHz at every divider, so two
                                                        // write clocks are 20 ns at every rate (up to 0x153C (D + 1) x 20 ns with K = 0)
      #(10 * 40 * ((kk ? 0 : d) + 1));                  // 40 write clock periods with the monitor on
      mon_on = 0;
      if (has33) begin
        wr_cmd(8'h33);
        rd_byte;
        if (exp_ok) begin
          check(rd_d[0] == 0, "0x33 bit 0 (encode self-check) clear after the capture");
          check(rd_d[3:1] == 3'b011, "0x33 bits 3:2 = convention 1, bit 1 prediction right");
          check(rd_d[7:4] == 4'hF, "0x33 bits 7:4 all right");
        end
      end
    end
  endtask

  //---------------------------------------------------------------------------------

  integer ph, i, tr, n_pin_bad, last_good [0:3], seq, d1, d2, k1;
  integer divs [0:5];
  real    stbs [0:3];
  integer flips = 0, prev_pol = -1, pol;

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 1;
    if (!$value$plusargs("exp_ok=%d", exp_ok)) exp_ok = 1;
    if (!$value$plusargs("has33=%d", has33)) has33 = 1;
    divs[0] = 0; divs[1] = 1; divs[2] = 2; divs[3] = 9; divs[4] = 99; divs[5] = 3;
    stbs[0] = 41.0; stbs[1] = 97.0; stbs[2] = 174.0; stbs[3] = 298.0;
    #2000;
    tr = 0;
    // 50 phases x (UI arm at a random divider and K, then host arm at divider 0 / K = 0, as the bench)
    for (ph = 0; ph < 50; ph = ph + 1) begin
      stb = stbs[ph % 4];
      @(negedge dut.clk_200MHz);
      #(0.1 * ph);
      d1 = divs[$unsigned($random(seed)) % 6];
      k1 = ($unsigned($random(seed)) % 3 == 0) ? 2 : 0;
      for (k = 0; k < 4; k = k + 1) last_good[k] = good[k];
      nchk = 0;
      arm(d1, k1, 0);
      check(!exp_ok || (good[0] - last_good[0] == nchk && good[1] - last_good[1] == nchk &&
                        good[2] - last_good[2] == nchk && good[3] - last_good[3] == nchk), "UI arm: encodes follow the write clock");
      for (k = 0; k < 4; k = k + 1) last_good[k] = good[k];
      nchk = 0;
      @(negedge dut.clk_200MHz);
      #(0.1 * ((ph * 7) % 50));
      arm(0, 0, 1);
      pol = ((good[2] - last_good[2]) * 2 > nchk) ? 1 : 0;
      if (prev_pol >= 0 && pol != prev_pol) flips = flips + 1;
      prev_pol = pol;
      $display("phase %0d: UI D %0d K %0d -> host D 0: right/checks e1B %0d e1A %0d e2B %0d e2A %0d of %0d",
               ph, d1, k1, good[0] - last_good[0], good[1] - last_good[1], good[2] - last_good[2],
               good[3] - last_good[3], nchk);
      check(!exp_ok || (good[0] - last_good[0] == nchk && good[1] - last_good[1] == nchk &&
                        good[2] - last_good[2] == nchk && good[3] - last_good[3] == nchk), "host arm at D 0: encodes follow the write clock");
    end
    $display("tb_encphase: chip 2 B polarity changed %0d times over 50 host captures", flips);
    $display("RESULT: %0d checks, %0d errors -> %0s", checks, errors, (errors == 0) ? "PASS" : "FAIL");
    $finish;
  end

  initial begin #400000000; $display("RESULT: timeout -> FAIL"); $finish; end

endmodule
