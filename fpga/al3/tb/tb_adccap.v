// ADC capture testbench (0x153A, reworked for 0x153F; fpga/al3/README.md "Changes" sections 10 and 22): the design's
// capture against a behavioural AD9288 pair in the data-align mode the maintainer's unit shows (bench 2026-11-06), with
// the release edge, the delay per core and the conversion pairing as parameters.
// Run with fpga/al3/tb/run.sh (ONLY=adccap) or directly: vvp tb_adccap.vvp +seed=N [+quick=1] [+dump=1].
//
// Model per core (module ad9288_core): samples are numbered 20 + n mod 100 (a "quiet" ramp: neighbours differ by 1
// code) on the core's own encode pin. The outputs follow the release edge: the buses go to garbage (200..255) tv after
// it and to the pipeline value pl samples back tpd after it. Three release rules (variable rel):
//   0  own encode, pl 4 / 4: the datasheet's Fig. 3 (normal operation, two clocks), what 0x153A..0x153E assumed;
//   1  the chip's A encode, pl 4 / 4: the datasheet's Fig. 4 (data align: B re-timed to clock A, B sample N - 1 with
//      A sample N);
//   2  the chip's B encode, plB 5 / plA 4: what the unit does. Both buses change after the write clock's rising edge R
//      (the B encode); the release at R_q carries the B sample taken at R_(q-5) and the A sample taken half a word
//      after it. The pair is the datasheet's Fig. 4 pair (B half a word before A), but it leaves on the other encode
//      edge. Only the pairing is measured; the absolute depth (5 / 4 against 4 / 3) is not, see README section 22.
// The delay is given as the bench gives it: D = the middle of the transition after the write clock's edge at the FPGA's
// capture registers, w = its width (garbage from D - w/2 to D + w/2), per core (task set_d). The encode pins reach the
// model through the pad delay epad (TD: the pads switch 6.97 / 7.34 ns after the write clock edge; the simulated pins
// 1.25 / 1.625 ns after it).
//
// A stored word is checked for
//   - garbage (a value >= 200: the capture hit the transition), against the prediction from D, w and the design's
//     capture instants,
//   - the pairing: s = (encode time of the word's A sample - that of its B sample - T/2) / T. s = 0 is the designed
//     pair (protocol 7.3: B[k] half a word before A[k]); +1 = core B one conversion late, as the bench found,
//   - continuity: consecutive words hold consecutive samples,
// and the dut's outlier counters (read 0x36) against the count derived from the stored stream.
//
// The testbench reads the version byte (0x06) and runs one of two programs.
//
// 0x153E and older, the model's acceptance: with rule 2 and D = 11.25 ns the unchanged RTL must give the two rows the
//   bench measured with fpga/al3/tools/core_order.py (stages CH1 A4 B3 and A3 B3, dividers 0..7, 9, 19, 99, 999), and
//   the D range that reproduces them is printed; rules 0 and 1 are printed for comparison (they give other rows). The
//   strict order check (s = 0 in every cell) must fail on that RTL: "STRICT ORDER: FAIL" is the expected line.
//
// 0x153F, the strict check: s = 0 wherever both cores of a chip are clean,
//   phase 1  at the nominal D, every divider 0..9, 19, 99, 999 and every stage 2..5 (a different stage per chip);
//   phase 2  at every divided rate over the rectangle D_A 9..14 x D_B 7..18 ns, both widths, every stage value (the
//            stage must not matter there: clean and paired everywhere);
//   phase 3  at divider 0 with both buses changing together (D_A = D_B over 7..18, and D_B within +-w of D_A over
//            9..14): every stage clean for both cores gives s = 0, whichever side of the transition it lies on;
//   phase 4  at divider 0 over the whole rectangle, for the record: where the two buses change more than w apart a
//            stage between the two transitions is clean and pairs neighbours of different releases. No capture scheme
//            can avoid that at a 10 ns period (the quiet-input training cannot see the conversion index); the cells
//            are printed, s must equal the model's prediction, and none may lie inside |D_A - D_B| <= w.
//   Then the garbage / counter checks of 0x153A at a stage inside the transition and the power-on value at every rate.
`timescale 1ns / 1ps
// ENC_A_NS / ENC_B_NS: the simulated A / B encode pins' delay after their write clock edge (1.25 ns: the encode flops
// on the falling clk_ADC edge; an image with the encode ODDRs gets its own from run.sh, README sections 19 and 21).
`ifndef ENC_A_NS
`define ENC_A_NS 1.25
`endif
`ifndef ENC_B_NS
`define ENC_B_NS 1.25
`endif

module ad9288_core (input enc, input enc_out, output reg [7:0] d);
  // enc: the encode that samples (this core's pin); enc_out: the edge the outputs follow. Delays and pipeline depth
  // are set from the testbench. pl counts the core's samples back at the release edge (a sample taken on that very
  // edge is 0).
  real    tpd = 6.0, tv = 2.0;
  integer pl = 4;
  reg [7:0] pv [0:7];
  real      tenc [0:255];                  // encode time of the last sample with this value (-1 = never)
  integer   n = 0, i;
  reg [7:0] nxt;
  initial begin
    for (i = 0; i < 8; i = i + 1) pv[i] = 0;
    for (i = 0; i < 256; i = i + 1) tenc[i] = -1.0;
    d = 0;
  end
  always @(posedge enc) begin
    for (i = 7; i > 0; i = i - 1) pv[i] = pv[i - 1];
    pv[0] = 8'd20 + (n % 100);
    tenc[pv[0]] = $realtime;
    n = n + 1;
  end
  always @(posedge enc_out) begin
    #0.001;                                // after the sampling block of the same edge
    nxt = pv[pl];
    // transport delays: tpd may exceed the encode period, and the value is the one of this edge
    d <= #(tv)  8'd200 + (($random & 32'h3F) % 56);
    d <= #(tpd) nxt;
  end
endmodule

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
  wire [7:0] adc1A, adc1B, adc2A, adc2B;

  fnirsi_1013D dut (
    .i_xtal(i_xtal), .i_mcu_clk(mcu_clk), .i_mcu_rws(mcu_rws), .i_mcu_dcs(mcu_dcs),
    .i_adc1A_d(adc1A), .i_adc1B_d(adc1B), .i_adc2A_d(adc2A), .i_adc2B_d(adc2B),
    .io_mcu_data(bus), .o_1khz_calib(pad),
    .o_adc1_encA(e1A), .o_adc1_encB(e1B), .o_adc2_encA(e2A), .o_adc2_encB(e2B),
    .o_offset_1(o1), .o_relay1_1(r11), .o_relay1_2(r12), .o_relay1_3(r13), .o_ac_dc_1(ac1),
    .o_offset_2(o2), .o_relay2_1(r21), .o_relay2_2(r22), .o_relay2_3(r23), .o_ac_dc_2(ac2),
    .o_pwm_display(pwm));

  //---------------------------------------------------------------------------------
  // ADC model: the encode pins through the pad delay, the release rule selectable

  real    epad1 = 5.75, epad2 = 6.25;      // chip 1 / chip 2: pad switches at edge + 1.25 (B) / 1.625 (A) + epad
  integer rel = 2;
  reg d1A = 0, d1B = 0, d2A = 0, d2B = 0;
  always @(e1A) d1A <= #(epad1) e1A;
  always @(e1B) d1B <= #(epad1) e1B;
  always @(e2A) d2A <= #(epad2) e2A;
  always @(e2B) d2B <= #(epad2) e2B;
  wire o1A = (rel == 2) ? d1B : d1A;
  wire o1B = (rel == 1) ? d1A : d1B;
  wire o2A = (rel == 2) ? d2B : d2A;
  wire o2B = (rel == 1) ? d2A : d2B;

  ad9288_core c1A (.enc(d1A), .enc_out(o1A), .d(adc1A));
  ad9288_core c1B (.enc(d1B), .enc_out(o1B), .d(adc1B));
  ad9288_core c2A (.enc(d2A), .enc_out(o2A), .d(adc2A));
  ad9288_core c2B (.enc(d2B), .enc_out(o2B), .d(adc2B));

  integer dump = 0;
  // debug trace of the chip 1 pins (+dump=1): encode edges and data changes in a 100 ns window
  always @(posedge d1A) if (dump && $time > 4400 && $time < 4500) $display("  encA rise %0t  e1A %b", $time, e1A);
  always @(posedge d1B) if (dump && $time > 4400 && $time < 4500) $display("  encB rise %0t", $time);
  always @(adc1A) if (dump && $time > 4400 && $time < 4500) $display("  adc1A -> %0d at %0t (n %0d)", adc1A, $time, c1A.n);
  always @(adc1B) if (dump && $time > 4400 && $time < 4500) $display("  adc1B -> %0d at %0t (n %0d)", adc1B, $time, c1B.n);

  // The release rule and the pipeline depths that go with it
  task set_rel(input integer r);
    begin
      rel = r;
      c1B.pl = (r == 2) ? 5 : 4; c2B.pl = c1B.pl;
      c1A.pl = 4;                c2A.pl = 4;
    end
  endtask

  // D and w per core, as the bench states them: the transition's middle after the write clock edge the release follows
  // (R for rules 1.. see e_rel) at the capture registers. dA1.. keep the values for the predictions.
  real dA1 = 11.25, dB1 = 11.25, dA2 = 11.25, dB2 = 11.25, wid = 1.5;

  // pad time of the release edge after the write clock edge it follows, per chip and core
  function real e_rel(input integer chip, input integer isA);
    real ep;
    begin
      ep = (chip == 1) ? epad1 : epad2;
      if (rel == 2)      e_rel = `ENC_B_NS + ep;
      else if (rel == 1) e_rel = `ENC_A_NS + ep;
      else               e_rel = (isA ? `ENC_A_NS : `ENC_B_NS) + ep;
    end
  endfunction

  task set_d(input real a1, input real b1, input real a2, input real b2, input real w);
    begin
      dA1 = a1; dB1 = b1; dA2 = a2; dB2 = b2; wid = w;
      if (a1 - w / 2.0 <= e_rel(1, 1) || b1 - w / 2.0 <= e_rel(1, 0) || a2 - w / 2.0 <= e_rel(2, 1) || b2 - w / 2.0 <= e_rel(2, 0)) begin
        $display("RESULT: set_d: a transition would start before its release edge reaches the pad (lower epad) -> FAIL");
        $finish;
      end
      c1A.tv = a1 - w / 2.0 - e_rel(1, 1); c1A.tpd = a1 + w / 2.0 - e_rel(1, 1);
      c1B.tv = b1 - w / 2.0 - e_rel(1, 0); c1B.tpd = b1 + w / 2.0 - e_rel(1, 0);
      c2A.tv = a2 - w / 2.0 - e_rel(2, 1); c2A.tpd = a2 + w / 2.0 - e_rel(2, 1);
      c2B.tv = b2 - w / 2.0 - e_rel(2, 0); c2B.tpd = b2 + w / 2.0 - e_rel(2, 0);
    end
  endtask

  //---------------------------------------------------------------------------------
  // MCU bus (as tb_encphase.v)

  integer errors = 0, checks = 0, seed = 1, quick = 0;
  task check(input cond, input [8*480-1:0] what);
    begin
      checks = checks + 1;
      if (!cond) begin
        errors = errors + 1;
        if (errors <= 25) $display("FAIL  t=%0t  %0s", $time, what);
      end
    end
  endtask

  real stb = 43.0;
  task mcu_strobe; begin #(0.3 * stb) mcu_clk = 0; #(0.4 * stb) mcu_clk = 1; #(0.3 * stb); end endtask
  task wr_cmd(input [7:0] c);  begin mcu_rws = 1; mcu_dcs = 1; mcu_dout = c; mcu_strobe; mcu_dcs = 0; end endtask
  task wr_byte(input [7:0] d); begin mcu_rws = 1; mcu_dcs = 0; mcu_dout = d; mcu_strobe; end endtask
  reg [7:0] rd_d;
  task rd_byte; begin mcu_rws = 0; mcu_dcs = 0; mcu_strobe; rd_d = bus; mcu_rws = 1; end endtask
  task wr1(input [7:0] c, input [7:0] d);  begin wr_cmd(c); wr_byte(d); end endtask
  task wr2(input [7:0] c, input [15:0] v); begin wr_cmd(c); wr_byte(v[15:8]); wr_byte(v[7:0]); end endtask
  task wr4(input [7:0] c, input [31:0] v);
    begin wr_cmd(c); wr_byte(v[31:24]); wr_byte(v[23:16]); wr_byte(v[15:8]); wr_byte(v[7:0]); end
  endtask
  task rd1(input [7:0] c); begin wr_cmd(c); rd_byte; end endtask

  //---------------------------------------------------------------------------------
  // Memory write monitor: every word written while a capture is armed, with the garbage flags, the A - B encode
  // time difference of the word (from the models' value -> encode time tables, looked up at the write) and the
  // per-core outlier count as the dut's counters define it (|sample - previous| > threshold, from the third word)

  reg      capturing = 0;
  integer  nw = 0;
  reg [7:0] pv1A = 0, pv1B = 0, pv2A = 0, pv2B = 0;
  integer  g1A, g1B, g2A, g2B;             // garbage words per core
  integer  w1A, w1B, w2A, w2B;             // ramp wraps (119 -> 20) per core: the one legitimate jump of the quiet input
  integer  x1A, x1B, x2A, x2B;             // expected outlier counts per core
  integer  ct1_bad, ct2_bad, ab1_n, ab2_n;
  real     dt1_min, dt1_max, dt2_min, dt2_max, dt;
  reg [7:0] v1A, v1B, v2A, v2B;
  integer  thr = 8;

  function far(input [7:0] x, input [7:0] y);
    far = ((x > y) ? (x - y) : (y - x)) > thr;
  endfunction

  task clear_stats;
    begin
      nw = 0; g1A = 0; g1B = 0; g2A = 0; g2B = 0; x1A = 0; x1B = 0; x2A = 0; x2B = 0; w1A = 0; w1B = 0; w2A = 0; w2B = 0;
      ct1_bad = 0; ct2_bad = 0; ab1_n = 0; ab2_n = 0;
      dt1_min = 1e9; dt1_max = -1e9; dt2_min = 1e9; dt2_max = -1e9;
    end
  endtask

  // the dut's counters clear while 0x01 = 1 and count from the release (skipping two words): the statistics start there too
  always @(negedge dut.sample_system_reset) if (capturing) clear_stats;

  always @(posedge dut.sample_write_clock)
    if (capturing && dut.sample_write_enable && dut.swc_adv) begin
      v1A = dut.samples.dia[7:0];  v1B = dut.samples.dia[15:8];
      v2A = dut.samples.dia[23:16]; v2B = dut.samples.dia[31:24];
      if (v1A >= 8'd200) g1A = g1A + 1;
      if (v1B >= 8'd200) g1B = g1B + 1;
      if (v2A >= 8'd200) g2A = g2A + 1;
      if (v2B >= 8'd200) g2B = g2B + 1;
      if (nw >= 2) begin                     // the dut's counters start at the third word after the release
        if (v1A == 8'd20 && pv1A == 8'd119) w1A = w1A + 1;
        if (v1B == 8'd20 && pv1B == 8'd119) w1B = w1B + 1;
        if (v2A == 8'd20 && pv2A == 8'd119) w2A = w2A + 1;
        if (v2B == 8'd20 && pv2B == 8'd119) w2B = w2B + 1;
      end
      if (nw >= 2) begin
        if (far(v1A, pv1A)) x1A = x1A + 1;
        if (far(v1B, pv1B)) x1B = x1B + 1;
        if (far(v2A, pv2A)) x2A = x2A + 1;
        if (far(v2B, pv2B)) x2B = x2B + 1;
      end
      if (v1A < 8'd200 && v1B < 8'd200) begin
        dt = c1A.tenc[v1A] - c1B.tenc[v1B];
        if (dt < dt1_min) dt1_min = dt;
        if (dt > dt1_max) dt1_max = dt;
        ab1_n = ab1_n + 1;
        if (nw >= 1 && pv1A < 8'd200 && v1A != 8'd20 + ((pv1A - 8'd20 + 8'd1) % 100)) ct1_bad = ct1_bad + 1;
      end
      if (v2A < 8'd200 && v2B < 8'd200) begin
        dt = c2A.tenc[v2A] - c2B.tenc[v2B];
        if (dt < dt2_min) dt2_min = dt;
        if (dt > dt2_max) dt2_max = dt;
        ab2_n = ab2_n + 1;
        if (nw >= 1 && pv2A < 8'd200 && v2A != 8'd20 + ((pv2A - 8'd20 + 8'd1) % 100)) ct2_bad = ct2_bad + 1;
      end
      if (dump && nw < 12) $display("  w%0d t=%0t 1A %0d 1B %0d 2A %0d 2B %0d tA %0.2f tB %0.2f", nw, $time, v1A, v1B, v2A, v2B, c1A.tenc[v1A], c1B.tenc[v1B]);
      pv1A = v1A; pv1B = v1B; pv2A = v2A; pv2B = v2B;
      nw = nw + 1;
    end

  //---------------------------------------------------------------------------------
  // One capture: the firmware's arm with the stage word, auto mode time-out (no trigger), then read 0x36. Short
  // records at the slow rates (a word is 1 us at divider 99, 10 us at 999).

  integer polls, words_min;
  reg [15:0] cs_rd;
  integer    oc1A, oc1B, oc2A, oc2B;
  reg [7:0]  b0;

  task capture(input [31:0] div, input [15:0] cs);
    integer pre, tot, tmo;
    begin
      if (div >= 99) begin pre = 4; tot = 12; tmo = 18; end
      else           begin pre = 20; tot = 60; tmo = 90; end
      words_min = tot;
      wr2(8'h63, cs);
      wr4(8'h0D, div);
      // a slow rate: let the models' pipelines fill with samples taken at this rate before the record starts (the
      // faster rates do that during the arm's strobes)
      if (div >= 20) #(10.0 * (div + 1) * 10);
      wr1(8'h17, 8'hFF);                                // level never crossed by the ramp
      wr1(8'h15, 8'h00);
      wr1(8'h16, 8'h00);
      wr1(8'h1A, 8'h00);                                // auto mode: the time-out ends the capture
      clear_stats;
      capturing = 1;
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05); polls = 0; rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      wr2(8'h0B, pre[15:0]);
      wr2(8'h0C, tot[15:0]);
      wr4(8'h0E, tmo);
      wr1(8'h01, 8'h00);
      polls = 0; rd_d = 0;
      while (rd_d[0] !== 1'b1 && polls < 4000000) begin rd1(8'h0A); polls = polls + 1; end
      capturing = 0;
      wr_cmd(8'h36);
      rd_byte; cs_rd[15:8] = rd_d; rd_byte; cs_rd[7:0] = rd_d;
      rd_byte; b0 = rd_d; rd_byte; oc1A = {b0, rd_d};
      rd_byte; b0 = rd_d; rd_byte; oc1B = {b0, rd_d};
      rd_byte; b0 = rd_d; rd_byte; oc2A = {b0, rd_d};
      rd_byte; b0 = rd_d; rd_byte; oc2B = {b0, rd_d};
    end
  endtask

  //---------------------------------------------------------------------------------
  // Predictions. The pins of a core are in transition over [D - w/2, D + w/2) after every release edge (period T), so
  // a capture at t ns after a release edge's write clock edge is clean by margin(t) and holds the release number
  // rel_idx(t) = floor((t - D) / T) (relative to that edge). Rules 0 and 1 release on F for core A (rule 0) or for
  // both (rule 1): t is then taken relative to F (rshift).

  function real margin(input real t, input real d, input real w, input real T);
    real lo, hi, m, best;
    integer n;
    begin
      best = 1e9;
      for (n = -6; n <= 6; n = n + 1) begin
        lo = d - w / 2.0 + n * T;
        hi = d + w / 2.0 + n * T;
        if (t < lo) m = lo - t; else if (t >= hi) m = t - hi; else m = -(((t - lo) < (hi - t)) ? (t - lo) : (hi - t));
        if (m < best) best = m;
      end
      margin = best;
    end
  endfunction

  function integer rel_idx(input real t, input real d, input real T);
    begin
      rel_idx = $rtoi($floor((t - d) / T));
    end
  endfunction

  // The design's capture instants, ns after the write clock's rising edge R of a word (version ver, T, the stage
  // fields of the core's chip: stA = the A field, stB = the B field).
  //   0x153E and older: core A at F - 5 - 2.5 kA = R + T/2 - 5 - 2.5 kA, core B at R - 5 - 2.5 kB.
  //   0x153F: both cores of a chip at R - 2.5 k at full rate (k = the A field's stage, 2..5; other values alias by
  //           k mod 4), at R + 2.5 at every divided rate.
  reg [7:0] ver = 0;
  function real t_cap(input integer isA, input real T, input integer stA, input integer stB);
    integer k;
    begin
      if (ver >= 8'h3F) begin
        k = 2 + ((stA + 2) % 4);
        t_cap = (T > 10.5) ? 2.5 : -2.5 * k;
      end else
        t_cap = isA ? (T / 2.0 - 5.0 - 2.5 * stA) : (-5.0 - 2.5 * stB);
    end
  endfunction

  // shift from "after R" to "after the edge the core's release follows" for the rule in use
  function real rshift(input integer isA, input real T);
    begin
      rshift = (rel == 1 || (rel == 0 && isA)) ? T / 2.0 : 0.0;
    end
  endfunction

  // predicted s of a chip: the A sample's encode time - the B sample's - T/2, in words. Core A holds release mA (the
  // A sample pipeline plA back at it), core B release mB; with rule 2 a release carries the designed pair, with rule 1
  // too, with rule 0 each core releases alone (A's releases half a word after B's with the designed pair).
  function integer s_pred(input real T, input integer stA, input integer stB, input real dA, input real dB);
    integer mA, mB;
    begin
      mA = rel_idx(t_cap(1, T, stA, stB) - rshift(1, T), dA, T);
      mB = rel_idx(t_cap(0, T, stA, stB) - rshift(0, T), dB, T);
      s_pred = mA - mB;
    end
  endfunction

  //---------------------------------------------------------------------------------
  // Evaluation of one capture

  real    T, mA1, mB1, mA2, mB2;
  reg     cleanA1, cleanB1, cleanA2, cleanB2;
  integer s1, s2, sp1, sp2;                // measured and predicted s per chip (99 = not measurable)
  integer ord_cells = 0, ord_bad = 0;      // chips with both cores clean, and those with s != 0
  reg [8*400-1:0] tag;
  localparam real GUARD = 0.05;            // a capture closer than this to a transition's end is not predicted

  function integer s_of(input real dmin, input real dmax, input real T, input integer n);
    integer a, b;
    begin
      a = $rtoi($floor((dmin - T / 2.0) / T + 0.5));
      b = $rtoi($floor((dmax - T / 2.0) / T + 0.5));
      s_of = (n > 0 && a == b && dmax - dmin < 1.0) ? a : 99;
    end
  endfunction

  // strict = 1: s != 0 on a clean chip is a failure; 0: only counted (ord_bad)
  task evaluate(input [31:0] div, input [15:0] cs, input integer strict);
    integer a1, b1, a2, b2;
    begin
      T  = 10.0 * (div + 1);
      a1 = cs[2:0]; b1 = cs[5:3]; a2 = cs[8:6]; b2 = cs[11:9];
      mA1 = margin(t_cap(1, T, a1, b1) - rshift(1, T), dA1, wid, T);
      mB1 = margin(t_cap(0, T, a1, b1) - rshift(0, T), dB1, wid, T);
      mA2 = margin(t_cap(1, T, a2, b2) - rshift(1, T), dA2, wid, T);
      mB2 = margin(t_cap(0, T, a2, b2) - rshift(0, T), dB2, wid, T);
      cleanA1 = (mA1 > GUARD); cleanA2 = (mA2 > GUARD); cleanB1 = (mB1 > GUARD); cleanB2 = (mB2 > GUARD);
      s1 = s_of(dt1_min, dt1_max, T, ab1_n); s2 = s_of(dt2_min, dt2_max, T, ab2_n);
      sp1 = s_pred(T, a1, b1, dA1, dB1);     sp2 = s_pred(T, a2, b2, dA2, dB2);
      $sformat(tag, "div %0d cs %04x rel %0d D %0.2f/%0.2f/%0.2f/%0.2f w %0.1f: words %0d garbage %0d/%0d/%0d/%0d oc %0d/%0d/%0d/%0d exp %0d/%0d/%0d/%0d s %0d/%0d pred %0d/%0d cont %0d/%0d margins %0.2f/%0.2f/%0.2f/%0.2f",
               div, cs, rel, dA1, dB1, dA2, dB2, wid, nw, g1A, g1B, g2A, g2B, oc1A, oc1B, oc2A, oc2B, x1A, x1B, x2A, x2B,
               s1, s2, sp1, sp2, ct1_bad, ct2_bad, mA1, mB1, mA2, mB2);
      check(nw >= words_min, {"capture too short: ", tag});
      check(cs_rd == cs, {"0x36 bytes 0-1 do not read back the 0x63 value: ", tag});
      // the dut's counters are exactly the stream's outliers
      check(oc1A == x1A && oc1B == x1B && oc2A == x2A && oc2B == x2B, {"0x36 counters differ from the stored stream: ", tag});
      // clean where the model says clean, every word garbage where it says inside the transition
      if (mA1 > GUARD)       check(g1A == 0, {"CH1 A predicted clean but garbage stored: ", tag});
      else if (mA1 < -GUARD) check(g1A == nw && oc1A > 0, {"CH1 A predicted in transition but not all words garbage / counter 0: ", tag});
      if (mB1 > GUARD)       check(g1B == 0, {"CH1 B predicted clean but garbage stored: ", tag});
      else if (mB1 < -GUARD) check(g1B == nw && oc1B > 0, {"CH1 B predicted in transition but not all words garbage / counter 0: ", tag});
      if (mA2 > GUARD)       check(g2A == 0, {"CH2 A predicted clean but garbage stored: ", tag});
      else if (mA2 < -GUARD) check(g2A == nw && oc2A > 0, {"CH2 A predicted in transition but not all words garbage / counter 0: ", tag});
      if (mB2 > GUARD)       check(g2B == 0, {"CH2 B predicted clean but garbage stored: ", tag});
      else if (mB2 < -GUARD) check(g2B == nw && oc2B > 0, {"CH2 B predicted in transition but not all words garbage / counter 0: ", tag});
      // a clean core at a quiet input gives a zero counter, apart from the ramp's wrap
      if (cleanA1) check(oc1A == w1A, {"CH1 A clean but counter not the ramp wraps: ", tag});
      if (cleanB1) check(oc1B == w1B, {"CH1 B clean but counter not the ramp wraps: ", tag});
      if (cleanA2) check(oc2A == w2A, {"CH2 A clean but counter not the ramp wraps: ", tag});
      if (cleanB2) check(oc2B == w2B, {"CH2 B clean but counter not the ramp wraps: ", tag});
      // both cores clean: one pairing through the record, the model's, consecutive samples in consecutive words
      if (cleanA1 && cleanB1) begin
        check(s1 != 99 && s1 == sp1 && ct1_bad == 0, {"CH1 pairing not the model's or not consecutive: ", tag});
        ord_cells = ord_cells + 1;
        if (s1 != 0) ord_bad = ord_bad + 1;
        if (strict) check(s1 == 0, {"CH1 word is not B[k] half a word before A[k]: ", tag});
      end
      if (cleanA2 && cleanB2) begin
        check(s2 != 99 && s2 == sp2 && ct2_bad == 0, {"CH2 pairing not the model's or not consecutive: ", tag});
        ord_cells = ord_cells + 1;
        if (s2 != 0) ord_bad = ord_bad + 1;
        if (strict) check(s2 == 0, {"CH2 word is not B[k] half a word before A[k]: ", tag});
      end
    end
  endtask

  //---------------------------------------------------------------------------------
  // The dividers of the bench table (0..7, 9, 19, 99, 999) and the two the brief adds (8); DIVN of them in a quick run

  integer divs [0:12];
  integer ndiv;
  initial begin
    divs[0] = 0; divs[1] = 1; divs[2] = 2; divs[3] = 3; divs[4] = 4; divs[5] = 5; divs[6] = 6; divs[7] = 7;
    divs[8] = 8; divs[9] = 9; divs[10] = 19; divs[11] = 99; divs[12] = 999;
  end

  // the bench rows (docs/hardware-status.md 2026-11-06 17:22 and 17:59; divider 99 from the 2026-11-04 records, +1
  // at A3 with B3 or B4): s per divider for stages CH1 A4 B3 and A3 B3. Divider 8 was not measured (-9).
  integer row_a4 [0:12];
  integer row_a3 [0:12];
  initial begin
    row_a4[0] = 0; row_a4[1] = 1; row_a4[2] = 0; row_a4[3] = 0; row_a4[4] = 0; row_a4[5] = 1; row_a4[6] = 1;
    row_a4[7] = 1; row_a4[8] = -9; row_a4[9] = 1; row_a4[10] = 1; row_a4[11] = -9; row_a4[12] = 1;
    row_a3[0] = 1; row_a3[1] = 1; row_a3[2] = 0; row_a3[3] = 0; row_a3[4] = 1; row_a3[5] = 1; row_a3[6] = 1;
    row_a3[7] = 1; row_a3[8] = -9; row_a3[9] = 1; row_a3[10] = 1; row_a3[11] = 1; row_a3[12] = 1;
  end

  integer di, st, k, ia, ib, wi, match, rows_ok, n_lo, n_hi;
  real    da, db, dl, d_lo, d_hi, step;
  reg [15:0] cs;
  wire [15:0] po = dut.CAP_SEL_PO;
  reg [8*200-1:0] line4, line3;
  real    ws [0:1];

  // one row pair of the 0x153E-type design: chip 1 at A4 B3, chip 2 at A3 B3 (0x06DC), every divider; rows_ok = both
  // rows equal the bench's in every measured cell (silent = no check, for the sweeps and the other rules)
  task rows_153e(input integer do_check, input integer last);
    begin
      rows_ok = 1; line4 = ""; line3 = "";
      for (di = 0; di <= last; di = di + 1) begin
        capture(divs[di], 16'h06DC);
        evaluate(divs[di], 16'h06DC, 0);
        $sformat(line4, "%0s %3d", line4, s1);
        $sformat(line3, "%0s %3d", line3, s2);
        if (row_a4[di] != -9 && s1 != row_a4[di]) rows_ok = 0;
        if (row_a3[di] != -9 && s2 != row_a3[di]) rows_ok = 0;
        if (do_check && row_a4[di] != -9)
          check(s1 == row_a4[di], {"model acceptance, A4 B3: s differs from the bench row: ", tag});
        if (do_check && row_a3[di] != -9)
          check(s2 == row_a3[di], {"model acceptance, A3 B3: s differs from the bench row: ", tag});
      end
    end
  endtask

  integer strict_bad_153e;

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 1;
    if (!$value$plusargs("quick=%d", quick)) quick = 0;
    if (!$value$plusargs("dump=%d", dump)) dump = 0;
    ws[0] = 1.5; ws[1] = 4.0;               // the unit's estimate; the datasheet's tPD max - tV min
    #300;
    wr1(8'h50, 8'h00);
    wr1(8'h57, 8'h00);
    wr1(8'h53, 8'h00);
    wr1(8'h64, 8'd8);
    wr_cmd(8'h06); rd_byte; rd_byte; ver = rd_d;
    $display("tb_adccap: version byte 0x%02x, seed %0d quick %0d", ver, seed, quick);
    ndiv = quick ? 11 : 13;

    if (ver < 8'h3F) begin
      //-----------------------------------------------------------------------------
      // 0x153E and older: the model's acceptance
      set_rel(2); set_d(11.25, 11.25, 11.25, 11.25, 1.5);
      rows_153e(1, ndiv - 1);
      $display("dividers                    0   1   2   3   4   5   6   7   8   9  19  99 999");
      $display("bench  A4 B3                0   1   0   0   0   1   1   1   .   1   1   .   1");
      $display("model  A4 B3 (rule 2)    %0s", line4);
      $display("bench  A3 B3                1   1   0   0   1   1   1   1   .   1   1   1   1");
      $display("model  A3 B3 (rule 2)    %0s", line3);
      strict_bad_153e = ord_bad;
      $display("acceptance: %0d checks, %0d errors; strict order: %0d of %0d clean cells are not s = 0", checks, errors, ord_bad, ord_cells);

      // the D range that gives the bench rows, w 0.2 (a sharp edge), dividers 0..7, 9, 19: core A, then core B. The
      // pad delay only moves the sampling instants; a short one lets D go down to the bench's lower bounds.
      epad1 = 3.0; epad2 = 3.0;
      step = quick ? 0.5 : 0.25;
      n_lo = 0; d_lo = 0; d_hi = 0;
      for (da = 9.37; da < 13.4; da = da + step) begin
        set_d(da, 11.25, da, 11.25, 0.2);
        rows_153e(0, 10);
        if (rows_ok) begin if (n_lo == 0) d_lo = da; d_hi = da; n_lo = n_lo + 1; end
      end
      $display("core A: the rows are the bench's for D_A = %0.2f .. %0.2f ns (D_B 11.25, w 0.2; bench: 10 .. 12.5)", d_lo, d_hi);
      check(n_lo > 0 && d_lo > 10.1 && d_lo < 10.11 + step && d_hi > 12.39 - step && d_hi < 12.4, "core A's D range is not the bench's (10, 12.5]");
      step = quick ? 1.0 : 0.5;
      n_lo = 0; d_lo = 0; d_hi = 0;
      for (db = 6.87; db < 18.4; db = db + step) begin
        set_d(11.25, db, 11.25, db, 0.2);
        rows_153e(0, 10);
        if (rows_ok) begin if (n_lo == 0) d_lo = db; d_hi = db; n_lo = n_lo + 1; end
      end
      $display("core B: the rows are the bench's for D_B = %0.2f .. %0.2f ns (D_A 11.25, w 0.2; bench: 7.5 .. 17.5)", d_lo, d_hi);
      check(n_lo > 0 && d_lo > 7.6 && d_lo < 7.61 + step && d_hi > 17.39 - step && d_hi < 17.4, "core B's D range is not the bench's (7.5, 17.5)");
      epad1 = 5.75; epad2 = 6.25;

      // the datasheet's rules at the same delay after their own release edges, for the record: neither gives the rows
      set_rel(1); set_d(11.25, 11.25, 11.25, 11.25, 1.5);
      rows_153e(0, 10);
      $display("model  A4 B3 (rule 1, the datasheet's Fig. 4: release on the A encode) %0s", line4);
      $display("model  A3 B3 (rule 1)                                                  %0s", line3);
      check(!rows_ok, "the datasheet's data-align figure reproduces the bench rows (it must not)");
      set_rel(0); set_d(11.25, 11.25, 11.25, 11.25, 1.5);
      rows_153e(0, 10);
      $display("model  A4 B3 (rule 0, the datasheet's Fig. 3: each core on its own encode) %0s", line4);
      $display("model  A3 B3 (rule 0)                                                      %0s", line3);
      check(!rows_ok, "the datasheet's normal operation reproduces the bench rows (it must not)");
      $display("STRICT ORDER: %0s (%0d cells; expected to fail on 0x%02x)", strict_bad_153e ? "FAIL" : "PASS", strict_bad_153e, ver);
      check(strict_bad_153e > 0, "the strict order check passes on a design older than 0x153F");
    end else begin
      //-----------------------------------------------------------------------------
      // 0x153F: the strict order check
      set_rel(2);

      // Phase 1: nominal D, every divider, every stage (chip 1 at st, chip 2 at the stage two further on)
      set_d(11.25, 11.25, 11.25, 11.25, 1.5);
      for (di = 0; di < ndiv; di = di + 1)
        for (st = 2; st <= 5; st = st + 1)
          if (!(divs[di] >= 99 && st > 3)) begin
            k = 2 + ((st - 2 + 2) % 4);
            cs = {4'b0000, k[2:0], k[2:0], st[2:0], st[2:0]};
            capture(divs[di], cs);
            evaluate(divs[di], cs, 1);
            check(cleanA1 && cleanB1 && cleanA2 && cleanB2, {"nominal D: a core is not clean: ", tag});
          end
      $display("phase 1 done: %0d checks, %0d errors, %0d clean cells", checks, errors, ord_cells);

      epad1 = 1.0; epad2 = 1.0;             // the sweeps: D - w/2 down to 3.3 (the pad delay only moves the sampling instants)

      // Phase 2: divided rates over the D rectangle, both widths; the stage walks 2..5 and must not matter. Chip 1
      // takes (D_A, D_B), chip 2 the mirrored corner (D_A' = 23 - D_A, D_B' = 25 - D_B), so one pass covers the grid
      // twice with different stages. The slow rates take the four corners and the centre only.
      st = 2;
      for (wi = 0; wi < 2; wi = wi + 1)
        for (di = 1; di < ndiv; di = di + 1)
          for (da = 9.0; da < 14.01; da = da + ((quick || divs[di] >= 99) ? 5.0 : 1.0))
            for (db = 7.0; db < 18.01; db = db + ((quick || divs[di] >= 99) ? 11.0 : 1.0)) begin
              set_d(da, db, 23.0 - da, 25.0 - db, ws[wi]);
              k = 2 + ((st - 2 + 1) % 4);
              cs = {4'b0000, k[2:0], k[2:0], st[2:0], st[2:0]};
              capture(divs[di], cs);
              evaluate(divs[di], cs, 1);
              check(cleanA1 && cleanB1 && cleanA2 && cleanB2 && g1A == 0 && g1B == 0 && g2A == 0 && g2B == 0,
                    {"divided rate: a core is not clean inside the D rectangle: ", tag});
              check(mA1 > 2.4 && mB1 > 2.4 && mA2 > 2.4 && mB2 > 2.4, {"divided rate: less than 2.4 ns to a transition: ", tag});
              st = (st == 5) ? 2 : st + 1;
            end
      $display("phase 2 done: %0d checks, %0d errors, %0d clean cells", checks, errors, ord_cells);

      // Phase 3: divider 0, the buses changing together. (a) D_A = D_B over 7..18; (b) D_B = D_A + dl, |dl| < w, D_A
      // over 9..14. Every stage; s = 0 wherever both cores are clean, and at least two stages are.
      for (wi = 0; wi < 2; wi = wi + 1) begin
        for (da = 7.1; da < 18.01; da = da + (quick ? 1.3 : 0.4))
          for (st = 2; st <= 5; st = st + 1) begin
            set_d(da, da, da + 0.3, da + 0.3, ws[wi]);
            cs = {4'b0000, st[2:0], st[2:0], st[2:0], st[2:0]};
            capture(0, cs);
            evaluate(0, cs, 1);
          end
        for (da = 9.1; da < 14.01; da = da + (quick ? 2.3 : 0.7))
          for (dl = -ws[wi] + 0.2; dl < ws[wi]; dl = dl + (ws[wi] - 0.2) / (quick ? 1.0 : 2.0))
            for (st = 2; st <= 5; st = st + 1) begin
              set_d(da, da + dl, da + 0.4, da + 0.4 - dl, ws[wi]);
              cs = {4'b0000, st[2:0], st[2:0], st[2:0], st[2:0]};
              capture(0, cs);
              evaluate(0, cs, 1);
            end
      end
      $display("phase 3 done: %0d checks, %0d errors, %0d clean cells, %0d not s = 0", checks, errors, ord_cells, ord_bad);
      check(ord_bad == 0, "a clean cell is not s = 0 in phases 1 to 3");

      // Phase 4: divider 0 over the whole rectangle (w 1.5), for the record. A stage that lies between the two
      // transitions is clean and pairs different releases: only where they are more than w apart.
      $display("phase 4: divider 0, w 1.5: per D_A row and D_B column the stages (2..5) that are clean on both cores but not s = 0 ('.' = none)");
      n_hi = 0;
      for (da = 9.1; da < 14.2; da = da + (quick ? 2.5 : 1.0)) begin
        line4 = "";
        for (db = 7.1; db < 18.2; db = db + (quick ? 2.75 : 1.0)) begin
          line3 = "";
          for (st = 2; st <= 5; st = st + 1) begin
            set_d(da, db, da, db, 1.5);
            cs = {4'b0000, st[2:0], st[2:0], st[2:0], st[2:0]};
            n_lo = ord_bad;
            capture(0, cs);
            evaluate(0, cs, 0);
            if (cleanA1 && cleanB1 && s1 != 0) begin
              $sformat(line3, "%0s%0d", line3, st);
              n_hi = n_hi + 1;
              check((da - db > 1.5) || (db - da > 1.5), {"divider 0: a mispaired clean stage with the transitions within w: ", tag});
            end
          end
          if (line3 == "") line3 = ".";
          $sformat(line4, "%0s %4s", line4, line3);
        end
        $display("  D_A %5.1f: %0s", da, line4);
      end
      $display("phase 4 done: %0d checks, %0d errors; %0d mispaired clean cells, all with |D_A - D_B| > w", checks, errors, n_hi);

      epad1 = 5.75; epad2 = 6.25;

      // A stage inside the transition: every word garbage, the counters say so (the training's signal)
      set_d(11.25, 11.25, 11.25, 11.25, 4.0);          // garbage 9.25 .. 13.25: stages 4 (R + 0 = 10) and 3 (12.5)
      for (st = 3; st <= 4; st = st + 1) begin
        cs = {4'b0000, st[2:0], st[2:0], st[2:0], st[2:0]};
        capture(0, cs);
        evaluate(0, cs, 1);
        check(g1A == nw && g1B == nw && g2A == nw && g2B == nw && oc1A > 0 && oc1B > 0 && oc2A > 0 && oc2B > 0,
              {"a stage inside the transition is not all garbage with counters: ", tag});
      end

      // the B fields and bits 15:12 change nothing: any value there gives the capture of the A fields
      set_d(11.25, 11.25, 11.25, 11.25, 1.5);
      for (di = 0; di < 4; di = di + 1) begin
        cs = {di[1:0], ~di[1:0], di[2:0] + 3'd2, 3'd3, 3'd5 - di[2:0], 3'd2};
        capture(di, cs);
        evaluate(di, cs, 1);
        check(cleanA1 && cleanB1 && cleanA2 && cleanB2 && s1 == 0 && s2 == 0, {"B fields / bits 15:12 change the capture: ", tag});
      end

      // the power-on value: clean and paired at every rate at the nominal D
      for (di = 0; di < ndiv; di = di + 1) begin
        capture(divs[di], po);
        evaluate(divs[di], po, 1);
        check(g1A == 0 && g1B == 0 && g2A == 0 && g2B == 0 && oc1A == w1A && oc1B == w1B && oc2A == w2A && oc2B == w2B
              && s1 == 0 && s2 == 0, {"power-on stages not clean and paired: ", tag});
      end
      $display("STRICT ORDER: %0s (%0d clean cells outside phase 4)", errors ? "FAIL" : "PASS", ord_cells);
    end

    $display("RESULT: %0d checks, %0d errors -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #4_000_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
