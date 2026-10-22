// ADC capture window testbench (0x153A, fpga/al3/README.md "Changes" section 10): the design's capture stages against
// a behavioural AD9288 whose output buses are in transition (random garbage) from tV to tPD after the encode edge
// the data follows, with the encode pad delay, tV and tPD swept over the datasheet range.
// Run with fpga/al3/tb/run.sh (ONLY=adccap) or directly: vvp tb_adccap.vvp +seed=N [+quick=1].
//
// Model per core: samples are numbered 20 + n mod 100 (a "quiet" ramp: neighbours differ by 1 code); the data bus
// goes to garbage (200..255) tV after the output edge and to the next pipeline value (4 encodes old, the datasheet's
// pipeline) tPD after it. The output edge is the core's own encode (AD9288 data-align off) or, with +align=1 / the
// align variable, the chip's A encode for the B core (data-align on: B re-timed to clock A). The encode pins of the
// dut reach the model through a delay (epad) standing in for the FPGA's clock-to-pad delay (TD: the pads switch
// 6.7..7.9 ns after the write clock edge; the simulated flops switch 1.25 ns after it, so epad about 5.5..6.7).
// The model keeps the encode time of every sample value it produced, so a stored word can be checked for
//   - garbage (a value >= 200: the capture hit the transition),
//   - the word association: the A sample must be encoded 5 ns after the B sample of the same word (B first; 0x153D:
//     the encodes run at 100 MHz per core whatever the divider, so T = 10 ns at every rate; up to 0x153C T/2 of the
//     divided write clock),
//   - continuity: consecutive words hold samples D + 1 encodes apart (0x153D: with K = 0 the write path stores every
//     (D + 1)-th captured pair; up to 0x153C consecutive samples of the divided encode).
// The dut's outlier counters (read 0x36) are compared with the count the testbench derives from the full-rate stream
// (every write clock edge while the capture writes, 0x153D; up to 0x153C the stored words, which were every edge).
//
// Phase 1: every divider D in {0, 1, 2, 3, 4, 9, 19}, stages 2..5, both B references (0x63 bits 12/13) and both
//   data-align models, at the nominal pad delay: each core must be clean exactly where the datasheet window formula
//   says (README section 10) with T = 10 ns at every divider, the counters must agree with the stream, and with the
//   matching reference every clean capture must be B-first by 5 ns at every rate (0x153D: the divider sweep shows the
//   window does not depend on it; up to 0x153C the window moved with the divided encode).
// Phase 2: the pad delay swept from 2 to 12 ns in 0.5 ns steps at 200 MSa/s with the datasheet corner (tPD 6, tV 2)
//   and the typical values (4.5, 3.0), stages 3 and 4: clean exactly inside the predicted range, the counters flag
//   every dirty capture (a wrong stage fails loudly), B-first wherever clean.
`timescale 1ns / 1ps

module ad9288_core (input enc, input enc_out, output reg [7:0] d);
  // enc: the encode that samples (this core's pin); enc_out: the edge the outputs follow (the same pin, or the A pin
  // of the chip in data-align mode). Delays and pipeline depth are set from the testbench.
  real    tpd = 6.0, tv = 2.0;
  integer pl = 4;
  reg [7:0] pv [0:7];
  real      pt [0:7];
  real      tenc [0:255];                  // encode time of the last sample with this value (-1 = never)
  integer   n = 0, i;
  reg [7:0] nxt;
  initial begin
    for (i = 0; i < 8; i = i + 1) begin pv[i] = 0; pt[i] = 0.0; end
    for (i = 0; i < 256; i = i + 1) tenc[i] = -1.0;
    d = 0;
  end
  always @(posedge enc) begin
    for (i = 7; i > 0; i = i - 1) begin pv[i] = pv[i - 1]; pt[i] = pt[i - 1]; end
    pv[0] = 8'd20 + (n % 100);
    pt[0] = $realtime;
    tenc[pv[0]] = $realtime;
    n = n + 1;
  end
  always @(posedge enc_out) begin
    #0.001;                                // after the sampling block of the same edge (own-encode mode)
    nxt = pv[pl];
    fork
      begin #(tv)  d = 8'd200 + (($random & 32'h3F) % 56); end
      begin #(tpd) d = nxt; end
    join_none
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
  // ADC model: the encode pins through the pad delay, data-align selectable

  real    epad1 = 5.75, epad2 = 6.25;      // chip 1 / chip 2: pad switches at edge + 1.25 + epad
  integer align = 0;
  reg d1A = 0, d1B = 0, d2A = 0, d2B = 0;
  always @(e1A) d1A <= #(epad1) e1A;
  always @(e1B) d1B <= #(epad1) e1B;
  always @(e2A) d2A <= #(epad2) e2A;
  always @(e2B) d2B <= #(epad2) e2B;
  wire o1B = align ? d1A : d1B;
  wire o2B = align ? d2A : d2B;

  ad9288_core c1A (.enc(d1A), .enc_out(d1A), .d(adc1A));
  ad9288_core c1B (.enc(d1B), .enc_out(o1B), .d(adc1B));
  ad9288_core c2A (.enc(d2A), .enc_out(d2A), .d(adc2A));
  ad9288_core c2B (.enc(d2B), .enc_out(o2B), .d(adc2B));

  // debug trace of the chip 1 pins (+dump=1): encode edges and data changes in a 100 ns window
  always @(posedge d1A) if (dump && $time > 4400 && $time < 4500) $display("  encA rise %0t  e1A %b", $time, e1A);
  always @(posedge d1B) if (dump && $time > 4400 && $time < 4500) $display("  encB rise %0t", $time);
  always @(adc1A) if (dump && $time > 4400 && $time < 4500) $display("  adc1A -> %0d at %0t (n %0d)", adc1A, $time, c1A.n);
  always @(adc1B) if (dump && $time > 4400 && $time < 4500) $display("  adc1B -> %0d at %0t (n %0d)", adc1B, $time, c1B.n);

  task set_adc(input real tpd, input real tv);
    begin
      c1A.tpd = tpd; c1B.tpd = tpd; c2A.tpd = tpd; c2B.tpd = tpd;
      c1A.tv = tv;   c1B.tv = tv;   c2A.tv = tv;   c2B.tv = tv;
    end
  endtask

  //---------------------------------------------------------------------------------
  // MCU bus (as tb_encphase.v)

  integer errors = 0, checks = 0, seed = 1, quick = 0, dump = 0;
  task check(input cond, input [8*400-1:0] what);
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
  integer  nw = 0, ne = 0;                 // stored words / write clock edges since the release
  integer  cur_div = 0;                    // the divider of the capture (0x153D: the stride of the stored pairs)
  reg [7:0] pv1A = 0, pv1B = 0, pv2A = 0, pv2B = 0;   // the previous stored word's samples
  reg [7:0] ev1A = 0, ev1B = 0, ev2A = 0, ev2B = 0;   // the previous edge's samples (the counters' reference)
  integer  g1A, g1B, g2A, g2B;             // garbage words per core
  integer  w1A, w1B, w2A, w2B;             // ramp wraps (119 -> 20) per core: the one legitimate jump of the quiet input
  integer  x1A, x1B, x2A, x2B;             // expected outlier counts per core
  integer  ab1_bad, ab2_bad, ct1_bad, ct2_bad, ab1_n, ab2_n;
  real     dt1_min, dt1_max, dt2_min, dt2_max, dt;
  reg [7:0] v1A, v1B, v2A, v2B;
  integer  thr = 8;

  function far(input [7:0] x, input [7:0] y);
    far = ((x > y) ? (x - y) : (y - x)) > thr;
  endfunction

  task clear_stats;
    begin
      nw = 0; ne = 0; g1A = 0; g1B = 0; g2A = 0; g2B = 0; x1A = 0; x1B = 0; x2A = 0; x2B = 0; w1A = 0; w1B = 0; w2A = 0; w2B = 0;
      ab1_bad = 0; ab2_bad = 0; ct1_bad = 0; ct2_bad = 0; ab1_n = 0; ab2_n = 0;
      dt1_min = 1e9; dt1_max = -1e9; dt2_min = 1e9; dt2_max = -1e9;
    end
  endtask

  // the dut's counters clear while 0x01 = 1 and count from the release (skipping two words): the statistics start there too
  always @(negedge dut.sample_system_reset) if (capturing) clear_stats;

  // 0x153D: the dut's counters compare the captured pair of every write clock edge with the previous edge's while the
  // capture writes (from the third edge after the release), stored or not; the garbage, word-association and
  // continuity statistics are over the stored words (every (D + 1)-th edge with K = 0)
  always @(posedge dut.sample_write_clock)
    if (capturing && dut.sample_write_enable) begin
      v1A = dut.samples.dia[7:0];  v1B = dut.samples.dia[15:8];
      v2A = dut.samples.dia[23:16]; v2B = dut.samples.dia[31:24];
      if (ne >= 2) begin                     // the dut's counters start at the third edge after the release
        if (v1A == 8'd20 && ev1A == 8'd119) w1A = w1A + 1;
        if (v1B == 8'd20 && ev1B == 8'd119) w1B = w1B + 1;
        if (v2A == 8'd20 && ev2A == 8'd119) w2A = w2A + 1;
        if (v2B == 8'd20 && ev2B == 8'd119) w2B = w2B + 1;
        if (far(v1A, ev1A)) x1A = x1A + 1;
        if (far(v1B, ev1B)) x1B = x1B + 1;
        if (far(v2A, ev2A)) x2A = x2A + 1;
        if (far(v2B, ev2B)) x2B = x2B + 1;
      end
      ev1A = v1A; ev1B = v1B; ev2A = v2A; ev2B = v2B;
      ne = ne + 1;
      if (dut.swc_adv) begin
        if (v1A >= 8'd200) g1A = g1A + 1;
        if (v1B >= 8'd200) g1B = g1B + 1;
        if (v2A >= 8'd200) g2A = g2A + 1;
        if (v2B >= 8'd200) g2B = g2B + 1;
        if (v1A < 8'd200 && v1B < 8'd200) begin
          dt = c1A.tenc[v1A] - c1B.tenc[v1B];
          if (dt < dt1_min) dt1_min = dt;
          if (dt > dt1_max) dt1_max = dt;
          ab1_n = ab1_n + 1;
          if (nw >= 1 && pv1A < 8'd200 && v1A != 8'd20 + ((pv1A - 8'd20 + cur_div + 1) % 100)) ct1_bad = ct1_bad + 1;
        end
        if (v2A < 8'd200 && v2B < 8'd200) begin
          dt = c2A.tenc[v2A] - c2B.tenc[v2B];
          if (dt < dt2_min) dt2_min = dt;
          if (dt > dt2_max) dt2_max = dt;
          ab2_n = ab2_n + 1;
          if (nw >= 1 && pv2A < 8'd200 && v2A != 8'd20 + ((pv2A - 8'd20 + cur_div + 1) % 100)) ct2_bad = ct2_bad + 1;
        end
        if (dump && (nw < 12 || (dump > 1 && $time > 303000 && $time < 307700))) $display("  w%0d t=%0t 1A %0d 1B %0d 2A %0d 2B %0d tA %0.2f tB %0.2f", nw, $time, v1A, v1B, v2A, v2B, c1A.tenc[v1A], c1B.tenc[v1B]);
        pv1A = v1A; pv1B = v1B; pv2A = v2A; pv2B = v2B;
        nw = nw + 1;
      end
    end

  //---------------------------------------------------------------------------------
  // One capture: the firmware's arm with the stage word, auto mode time-out (no trigger), then read 0x36

  integer polls;
  reg [15:0] cs_rd;
  integer    oc1A, oc1B, oc2A, oc2B;
  reg [7:0]  b0, b1;

  task capture(input [31:0] div, input [15:0] cs);
    begin
      cur_div = div;
      wr2(8'h63, cs);
      wr4(8'h0D, div);
      wr1(8'h17, 8'hFF);                                // level never crossed by the ramp
      wr1(8'h15, 8'h00);
      wr1(8'h16, 8'h00);
      wr1(8'h0F, 8'h00);
      wr1(8'h1A, 8'h00);                                // auto mode: the time-out ends the capture
      clear_stats;
      capturing = 1;
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05); polls = 0; rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      wr2(8'h0B, 16'd20);
      wr2(8'h0C, 16'd60);
      wr4(8'h0E, 32'd90);
      wr1(8'h01, 8'h00);
      polls = 0; rd_d = 0;
      while (rd_d[0] !== 1'b1 && polls < 400000) begin rd1(8'h0A); polls = polls + 1; end
      capturing = 0;
      wr1(8'h0F, 8'h01);
      wr_cmd(8'h36);
      rd_byte; cs_rd[15:8] = rd_d; rd_byte; cs_rd[7:0] = rd_d;
      rd_byte; b0 = rd_d; rd_byte; oc1A = {b0, rd_d};
      rd_byte; b0 = rd_d; rd_byte; oc1B = {b0, rd_d};
      rd_byte; b0 = rd_d; rd_byte; oc2A = {b0, rd_d};
      rd_byte; b0 = rd_d; rd_byte; oc2B = {b0, rd_d};
    end
  endtask

  //---------------------------------------------------------------------------------
  // The datasheet window formula (README section 10): e = the pad delay of the edge the data follows, after the
  // capture edge of the core (mod T); the pins are stable over [e + tpd - T, e + tv] (plus multiples of T); stage k
  // samples at -5 - 2.5 k. Returns the margin (negative = inside the transition).

  function real margin(input real e, input real tpd, input real tv, input real T, input integer k);
    real t, ws, we, m, best;
    integer n;
    begin
      t = -5.0 - 2.5 * k;
      best = -1e9;
      for (n = -8; n <= 2; n = n + 1) begin
        ws = e + tpd + n * T;
        we = e + T + tv + n * T;
        m = ((t - ws) < (we - t)) ? (t - ws) : (we - t);
        if (m > best) best = m;
      end
      margin = best;
    end
  endfunction

  // e relative to the capture edge for the B core: data edge R (align 0) or F (align 1), capture edge R (ref 0) or F
  // (ref 1); F = R - T/2
  function real e_eff(input real e, input real T, input integer al, input integer rf);
    begin
      e_eff = e + ((al == rf) ? 0.0 : (al ? -T / 2.0 : T / 2.0));
    end
  endfunction

  //---------------------------------------------------------------------------------

  integer di, st, rf, al, ci, words_exp;
  integer divs [0:6];
  real    T, mA1, mB1, mA2, mB2, tpd_r, tv_r, ep, e1, e2;
  real    tpds [0:1], tvs [0:1];
  reg [15:0] cs;
  wire [15:0] po = dut.CAP_SEL_PO;
  reg     cleanA1, cleanB1, cleanA2, cleanB2;
  reg [8*300-1:0] tag;

  task evaluate(input real T, input integer al, input integer rf, input integer stA, input integer stB);
    begin
      e1 = 1.25 + epad1; e2 = 1.25 + epad2;
      mA1 = margin(e1, tpd_r, tv_r, T, stA);
      mA2 = margin(e2, tpd_r, tv_r, T, stA);
      mB1 = margin(e_eff(e1, T, al, rf), tpd_r, tv_r, T, stB);
      mB2 = margin(e_eff(e2, T, al, rf), tpd_r, tv_r, T, stB);
      cleanA1 = (mA1 > 0.2); cleanA2 = (mA2 > 0.2); cleanB1 = (mB1 > 0.2); cleanB2 = (mB2 > 0.2);
      $sformat(tag, "T %0.0f align %0d ref %0d stA %0d stB %0d tpd %0.1f tv %0.1f epad %0.2f: words %0d garbage %0d/%0d/%0d/%0d oc %0d/%0d/%0d/%0d exp %0d/%0d/%0d/%0d dt1 %0.2f..%0.2f dt2 %0.2f..%0.2f cont %0d/%0d margins %0.2f/%0.2f/%0.2f/%0.2f",
               T, al, rf, stA, stB, tpd_r, tv_r, epad1, nw, g1A, g1B, g2A, g2B, oc1A, oc1B, oc2A, oc2B, x1A, x1B, x2A, x2B,
               dt1_min, dt1_max, dt2_min, dt2_max, ct1_bad, ct2_bad, mA1, mB1, mA2, mB2);
      check(nw >= 60, {"capture too short: ", tag});
      // the dut's counters are exactly the full-rate stream's outliers (every write clock edge, 0x153D)
      check(oc1A == x1A && oc1B == x1B && oc2A == x2A && oc2B == x2B, {"0x36 counters differ from the full-rate stream: ", tag});
      // clean where the formula says clean, every word garbage where it says inside the transition
      if (mA1 > 0.2)       check(g1A == 0, {"CH1 A predicted clean but garbage stored: ", tag});
      else if (mA1 < -0.2) check(g1A == nw && oc1A > 0, {"CH1 A predicted in transition but not all words garbage / counter 0: ", tag});
      if (mB1 > 0.2)       check(g1B == 0, {"CH1 B predicted clean but garbage stored: ", tag});
      else if (mB1 < -0.2) check(g1B == nw && oc1B > 0, {"CH1 B predicted in transition but not all words garbage / counter 0: ", tag});
      if (mA2 > 0.2)       check(g2A == 0, {"CH2 A predicted clean but garbage stored: ", tag});
      else if (mA2 < -0.2) check(g2A == nw && oc2A > 0, {"CH2 A predicted in transition but not all words garbage / counter 0: ", tag});
      if (mB2 > 0.2)       check(g2B == 0, {"CH2 B predicted clean but garbage stored: ", tag});
      else if (mB2 < -0.2) check(g2B == nw && oc2B > 0, {"CH2 B predicted in transition but not all words garbage / counter 0: ", tag});
      // a clean core at a quiet input gives a zero counter, apart from the ramp's wrap
      if (cleanA1) check(oc1A == w1A, {"CH1 A clean but counter not the ramp wraps: ", tag});
      if (cleanB1) check(oc1B == w1B, {"CH1 B clean but counter not the ramp wraps: ", tag});
      if (cleanA2) check(oc2A == w2A, {"CH2 A clean but counter not the ramp wraps: ", tag});
      if (cleanB2) check(oc2B == w2B, {"CH2 B clean but counter not the ramp wraps: ", tag});
      // with the matching reference: B first by T/2 = 5 ns in every word, samples D + 1 encodes apart in consecutive words
      if (al == rf) begin
        if (cleanA1 && cleanB1) begin
          check(ab1_n > 0 && dt1_min > T / 2.0 - 1.0 && dt1_max < T / 2.0 + 1.0 && ct1_bad == 0,
                {"CH1 word association not B first by 5 ns or not D + 1 encodes apart: ", tag});
        end
        if (cleanA2 && cleanB2) begin
          check(ab2_n > 0 && dt2_min > T / 2.0 - 1.0 && dt2_max < T / 2.0 + 1.0 && ct2_bad == 0,
                {"CH2 word association not B first by 5 ns or not D + 1 encodes apart: ", tag});
        end
      end
    end
  endtask

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 1;
    if (!$value$plusargs("quick=%d", quick)) quick = 0;
    if (!$value$plusargs("align=%d", align)) align = 0;
    if (!$value$plusargs("dump=%d", dump)) dump = 0;
    divs[0] = 0; divs[1] = 1; divs[2] = 2; divs[3] = 3; divs[4] = 4; divs[5] = 9; divs[6] = 19;
    tpds[0] = 6.0; tvs[0] = 2.0;            // datasheet limits
    tpds[1] = 4.5; tvs[1] = 3.0;            // typical
    #300;
    wr1(8'h50, 8'h00);
    wr1(8'h57, 8'h00);
    wr1(8'h53, 8'h00);
    wr1(8'h64, 8'd8);
    $display("tb_adccap: seed %0d quick %0d", seed, quick);

    // Phase 1
    tpd_r = 6.0; tv_r = 2.0; set_adc(tpd_r, tv_r);
    for (al = 0; al < 2; al = al + 1)
      for (rf = 0; rf < 2; rf = rf + 1)
        for (di = 0; di < 7; di = di + 1)
          for (st = 2; st <= 5; st = st + 1) begin
            if (!(quick && (st == 2 || st == 5) && di > 1)) begin
              align = al;
              T = 10.0;                                   // 0x153D: the encode period at every divider (was 10 (D + 1))
              cs = {2'b00, rf[0], rf[0], st[2:0], st[2:0], st[2:0], st[2:0]};
              capture(divs[di], cs);
              evaluate(T, al, rf, st, st);
              check(cs_rd == cs, "0x36 bytes 0-1 read back the 0x63 value");
            end
          end
    $display("phase 1 done: %0d checks, %0d errors", checks, errors);

    // Phase 2: pad delay sweep at 200 MSa/s, own-encode reference
    align = 0;
    for (ci = 0; ci < 2; ci = ci + 1) begin
      tpd_r = tpds[ci]; tv_r = tvs[ci]; set_adc(tpd_r, tv_r);
      for (st = 3; st <= 4; st = st + 1)
        for (ep = 2.0; ep <= 12.01; ep = ep + (quick ? 1.0 : 0.5)) begin
          epad1 = ep; epad2 = ep + 0.25;
          T = 10.0;
          cs = {4'b0000, st[2:0], st[2:0], st[2:0], st[2:0]};
          capture(0, cs);
          evaluate(T, 0, 0, st, st);
        end
    end
    epad1 = 5.75; epad2 = 6.25;
    $display("phase 2 done: %0d checks, %0d errors", checks, errors);

    // the power-on stages: clean at every rate with the nominal pad delays, both corners
    for (ci = 0; ci < 2; ci = ci + 1) begin
      tpd_r = tpds[ci]; tv_r = tvs[ci]; set_adc(tpd_r, tv_r);
      for (di = 0; di < 7; di = di + 1) begin
        T = 10.0;                                         // 0x153D: the encode period at every divider
        capture(divs[di], po);
        evaluate(T, 0, 0, po[2:0], po[5:3]);
        check(g1A == 0 && g1B == 0 && g2A == 0 && g2B == 0 && oc1A == w1A && oc1B == w1B && oc2A == w2A && oc2B == w2B,
              {"power-on stages not clean: ", tag});
      end
    end

    $display("RESULT: %0d checks, %0d errors -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #400_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
