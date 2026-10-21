// Hardware counter testbench (fpga/al3/src/zaklad.v, 0x153C item C3: commands 0x61, 0x62, read 0x35 with 16 bytes).
// Run with run.sh (ONLY=counter) or vvp (+seed). About 130 ms of simulated time (thirteen 10 ms gates, roughly 40 min).
//
//  1. gate: the published results come exactly 2,000,000 clk_200MHz cycles apart (10 ms); the 100 ms and 1 s reloads
//     (not simulated in full) are checked as constants.
//  2. bookkeeping: E, T, W of every gate equal a reference model run on the counter's input stream (k_cs, sampled at
//     every clock): first and last rising edge in the gate, periods, clocks and high clocks between them. Min and max
//     equal a model of the source's captured pair (adc*_w at every write clock edge) over the same gate.
//  3. accuracy against the stimulus: EXT square 1234.5 ns / 400 ns high; CH1 square through the AD9288 model with
//     +-2 codes of noise at 200 MSa/s, at 10 MSa/s and with K = 1 (the decimator forcing the full-rate write clock);
//     frequency within 3e-6 (200 MSa/s, EXT) or 2e-5 (10 MSa/s), duty within 0.2 %; min / max within the noise of the
//     stimulus levels (60 / 200 on CH1, 55 / 195 on CH2).
//  4. source select: 0x61 bits 3..2 = 1 CH1 (whatever 0x15 says), 2 CH2, 3 EXT, 0 the trigger source of 0x15 (CH2 and
//     EXT); the status byte names the source; EXT publishes min 255 / max 0 (no sample).
//  5. Schmitt thresholds, noise off (codes exactly 60 and 200): high at code >= level (level 201 counts nothing, 200
//     counts), low below level - hysteresis (level 200: hysteresis 139 counts, 140 never goes low); a slow noisy
//     triangle counts its noise with hysteresis 0 and the true frequency with 12.
//  6. the MCU side: before the first gate 0x35 reads 0 everywhere except min 255 (power-on, nothing published); read
//     0x35 returns the snapshot taken at its command byte; byte 0 bit 7 ("new") is set for a result not read before,
//     clear when read again and set again after the next gate; reading 0x35 in the middle of a sample readout leaves
//     the readout exactly as reading 0x06 there does.
`timescale 1ns / 1ps

module tb;

  reg i_xtal = 0;
  always #10 i_xtal = ~i_xtal;

  reg       mcu_clk  = 1;
  reg       mcu_rws  = 1;
  reg       mcu_dcs  = 0;
  reg [7:0] mcu_dout = 0;
  reg [7:0] adc1A = 40, adc1B = 40, adc2A = 215, adc2B = 215;
  wire [7:0] bus;
  assign bus = mcu_rws ? mcu_dout : 8'bz;
  reg  ext_v = 0, ext_en = 0;
  wire pad;
  assign pad = ext_en ? ext_v : 1'bz;
  assign (weak0, weak1) pad = 1'b0;
  wire e1A, e1B, e2A, e2B, o1, r11, r12, r13, ac1, o2, r21, r22, r23, ac2, pwm;

  fnirsi_1013D dut (
    .i_xtal(i_xtal), .i_mcu_clk(mcu_clk), .i_mcu_rws(mcu_rws), .i_mcu_dcs(mcu_dcs),
    .i_adc1A_d(adc1A), .i_adc1B_d(adc1B), .i_adc2A_d(adc2A), .i_adc2B_d(adc2B),
    .io_mcu_data(bus), .o_1khz_calib(pad),
    .o_adc1_encA(e1A), .o_adc1_encB(e1B), .o_adc2_encA(e2A), .o_adc2_encB(e2B),
    .o_offset_1(o1), .o_relay1_1(r11), .o_relay1_2(r12), .o_relay1_3(r13), .o_ac_dc_1(ac1),
    .o_offset_2(o2), .o_relay2_1(r21), .o_relay2_2(r22), .o_relay2_3(r23), .o_ac_dc_2(ac2),
    .o_pwm_display(pwm));

  integer errors = 0, checks = 0, seed;
  task check(input cond, input [8*140-1:0] what);
    begin
      checks = checks + 1;
      if (!cond) begin errors = errors + 1; $display("FAIL  t=%0t  %0s", $time, what); end
    end
  endtask

  //---------------------------------------------------------------------------------
  // stimulus: EXT square, and the analog CH1 input (square or triangle, with noise) through the AD9288 model;
  // CH2 is the complement (255 - CH1)

  real    ext_p = 1234.5, ext_h = 400.0;
  integer amode = 0;                                     // 0 square, 1 triangle
  real    a_p = 777.7, a_h = 300.0;                      // square period / high time (ns); triangle period
  integer noise = 2;
  real    t0a = 0.0;
  reg [7:0] vin = 60;

  always begin
    #(ext_p - ext_h) ext_v = 1;
    #(ext_h) ext_v = 0;
  end

  real ph, x;
  integer nz;
  always begin
    #(2.0 + (($random(seed) & 32'h7FFFFFFF) % 2000) * 0.002);     // a new value every 2 .. 6 ns
    ph = ($realtime - t0a) / a_p;
    ph = ph - $floor(ph);
    if (amode == 0) x = (ph * a_p < a_h) ? 200.0 : 60.0;
    else x = (ph < 0.5) ? 60.0 + 280.0 * ph : 200.0 - 280.0 * (ph - 0.5);
    nz = (noise == 0) ? 0 : (($random(seed) & 32'h7FFFFFFF) % (2 * noise + 1)) - noise;
    x = x + nz;
    vin = (x < 0) ? 0 : (x > 255) ? 255 : $rtoi(x);
  end

  reg [7:0] pA [0:7];
  reg [7:0] pB [0:7];
  integer ia, ib;
  initial for (ia = 0; ia < 8; ia = ia + 1) begin pA[ia] = 60; pB[ia] = 60; end
  always @(posedge e1A) begin
    for (ia = 7; ia > 0; ia = ia - 1) pA[ia] = pA[ia - 1];
    pA[0] = vin;
    adc1A <= #3.3 pA[4];
    adc2A <= #3.3 8'd255 - pA[4];
  end
  always @(posedge e1B) begin
    for (ib = 7; ib > 0; ib = ib - 1) pB[ib] = pB[ib - 1];
    pB[0] = vin;
    adc1B <= #3.3 pB[4];
    adc2B <= #3.3 8'd255 - pB[4];
  end

  //---------------------------------------------------------------------------------
  // reference model on the counter's input stream, run at every clock; publish times. The min / max model takes the
  // pair the FPGA captured (adc*_w, the values before each write clock edge, as the counter's c_mn / c_mx do) of the
  // channel the test selected (exp_ch2), folded into the running gate on the same clocks the counter uses (swc_rise_q),
  // none for EXT (exp_ext).

  integer exp_ext = 0, exp_ch2 = 0;                      // the source the test selected
  integer wmn = 255, wmx = 0;                            // min / max of the pair at the last write clock edge
  reg [7:0] pe, pl;
  always @(posedge dut.sample_write_clock) begin
    pe = exp_ch2 ? dut.adc2B_w : dut.adc1B_w;
    pl = exp_ch2 ? dut.adc2A_w : dut.adc1A_w;
    wmn = (pe < pl) ? pe : pl;
    wmx = (pe > pl) ? pe : pl;
  end

  integer clk_n = 0;
  integer m_run = 0, m_t = 0, m_w = 0, m_e = 0, m_tl = 0, m_wl = 0, m_el = 0, mm_mn = 255, mm_mx = 0;
  integer x_t = 0, x_w = 0, x_e = 0, x_mn = 255, x_mx = 0;   // the model's result for the gate that just ended
  integer pub_n = 0, pub_last = -1, pub_iv = 0;
  reg     s_prev = 0, s_now = 0, was_on = 0, g_end = 0, w_rise = 0;
  always @(posedge dut.clk_200MHz) begin
    // values before this edge
    s_now = dut.k_cs; s_prev = dut.k_csp; was_on = dut.k_on; g_end = dut.k_g[28]; w_rise = dut.swc_rise_q;
    if (!was_on) begin m_run = 0; m_tl = 0; m_wl = 0; m_el = 0; mm_mn = 255; mm_mx = 0; end
    else if (g_end) begin
      x_t = m_tl; x_w = m_wl; x_e = m_el; x_mn = mm_mn; x_mx = mm_mx;
      m_run = 0; m_tl = 0; m_wl = 0; m_el = 0; mm_mn = 255; mm_mx = 0;
      pub_iv = clk_n - pub_last; pub_last = clk_n; pub_n = pub_n + 1;
    end else begin
      if (w_rise && !exp_ext) begin
        if (wmn < mm_mn) mm_mn = wmn;
        if (wmx > mm_mx) mm_mx = wmx;
      end
      if (m_run) begin
        if (s_now && !s_prev) begin m_tl = m_t; m_wl = m_w; m_el = m_e + 1; m_e = m_e + 1; end
        m_t = m_t + 1; m_w = m_w + s_now;
      end else if (s_now && !s_prev) begin
        m_run = 1; m_t = 1; m_w = 1; m_e = 0;
      end
    end
    clk_n = clk_n + 1;
  end

  //---------------------------------------------------------------------------------
  // MCU bus

  real stb = 97.0;
  task mcu_strobe;
    begin
      #(0.3 * stb + 0.011 * ($random(seed) & 63)) mcu_clk = 0;
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
  reg [15:0] rd16;
  task rd2(input [7:0] c); begin wr_cmd(c); rd_byte; rd16[15:8] = rd_d; rd_byte; rd16[7:0] = rd_d; end endtask

  // read 0x35: status, sequence, E, T, W, min, max (16 bytes)
  reg [7:0]  r_st, r_seq, r_mn, r_mx;
  reg [31:0] r_e, r_t, r_w;
  integer    b;
  task rd_counter;
    begin
      wr_cmd(8'h35);
      rd_byte; r_st = rd_d;
      rd_byte; r_seq = rd_d;
      for (b = 0; b < 4; b = b + 1) begin rd_byte; r_e = { r_e[23:0], rd_d }; end
      for (b = 0; b < 4; b = b + 1) begin rd_byte; r_t = { r_t[23:0], rd_d }; end
      for (b = 0; b < 4; b = b + 1) begin rd_byte; r_w = { r_w[23:0], rd_d }; end
      rd_byte; r_mn = rd_d;
      rd_byte; r_mx = rd_d;
    end
  endtask

  // select the counter source for the test and the model (0x61 gate 10 ms); src 0 follows 0x15 (t15). The gate is
  // switched off first, so that the new gate starts after the settings (and after a rate change made before the
  // call: the words around a divider change pair samples of different ages, see README section 12) and the model's
  // source is the counter's from the first sample of the gate.
  task select(input [1:0] src, input [1:0] t15);
    begin
      wr1(8'h61, 8'h00);
      exp_ext = (src == 2'd3) || (src == 2'd0 && t15[1]);
      exp_ch2 = (src == 2'd2) || (src == 2'd0 && !t15[1] && t15[0]);
      wr1(8'h15, { 6'd0, t15 });
      wr1(8'h61, { 4'd0, src, 2'd1 });
    end
  endtask

  // wait for the next publish, read it, compare with the model and the stimulus; mn_lo .. mx_hi: the stimulus range of
  // min and max (mn_lo < 0: not checked)
  integer pn;
  real    f_true, f_meas, d_true, d_meas, ferr;
  task gate_result(input real fexp, input real dexp, input real ftol, input integer mn_lo, input integer mn_hi,
                   input integer mx_lo, input integer mx_hi, input [8*50-1:0] what);
    begin
      pn = pub_n;
      wait (pub_n == pn + 1);
      #200;
      rd_counter;
      f_meas = (r_t > 0) ? r_e / (r_t * 5.0e-9) : 0.0;
      d_meas = (r_t > 0) ? (r_w * 1.0) / r_t : 0.0;
      ferr = (fexp > 0) ? (f_meas - fexp) / fexp : 0.0;
      $display("  %0s: E %0d T %0d W %0d min %0d max %0d -> f %0.3f Hz (expected %0.3f, error %0.2e), duty %0.4f (%0.4f), status %h seq %0d, gate %0d clocks",
               what, r_e, r_t, r_w, r_mn, r_mx, f_meas, fexp, ferr, d_meas, dexp, r_st, r_seq, pub_iv);
      check(r_e == x_e && r_t == x_t && r_w == x_w, "E, T, W equal the reference model on the input stream");
      check(r_mn == x_mn && r_mx == x_mx, "min, max equal the reference model on the captured pair");
      check(r_seq == dut.k_seq && r_st[7] == 1'b1, "a new result: sequence number current, bit 7 set");
      check(r_st[6] == 1'b0 && r_st[3:2] == 2'b00 && r_st[1:0] == 2'd1, "status: zero bits zero, gate 10 ms");
      check(r_st[5] == exp_ext[0] && r_st[4] == exp_ch2[0], "status: source bits EXT / CH2 as selected");
      if (fexp > 0) begin
        check(ferr < ftol && ferr > -ftol, "frequency within tolerance of the stimulus");
        if (dexp >= 0.0) check(d_meas - dexp < 0.002 && d_meas - dexp > -0.002, "duty within 0.2 % of the stimulus");
      end
      if (mn_lo >= 0)
        check(r_mn >= mn_lo && r_mn <= mn_hi && r_mx >= mx_lo && r_mx <= mx_hi, "min / max within the stimulus range");
    end
  endtask

  // a READ_RAW pass of 40 bytes with a read inserted after byte 16 (cmd 0x35 or 0x06), then the command reissued
  reg [7:0] pass35 [0:39];
  reg [7:0] pass06 [0:39];
  integer   ii;
  task raw_pass(input [7:0] ins, input integer which);
    begin
      wr2(8'h1F, 13'd100);
      wr_cmd(8'h20); mcu_rws = 0; mcu_dcs = 0; mcu_strobe;
      for (ii = 0; ii < 16; ii = ii + 1) begin mcu_strobe; if (which) pass35[ii] = bus; else pass06[ii] = bus; end
      mcu_rws = 1;
      if (ins == 8'h35) rd_counter; else begin rd2(8'h06); end
      wr_cmd(8'h20); mcu_rws = 0; mcu_dcs = 0; mcu_strobe;
      for (ii = 16; ii < 40; ii = ii + 1) begin mcu_strobe; if (which) pass35[ii] = bus; else pass06[ii] = bus; end
      mcu_rws = 1;
    end
  endtask

  integer e_hys0;
  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    $display("tb_counter: seed %0d", seed);
    #300;
    wr1(8'h50, 8'h00);                                   // generator off: EXT is the pad
    ext_en = 1;
    rd2(8'h06);
    check(rd16 == 16'h153B || rd16 == 16'h153C || rd16 == 16'h153D, "cmd 0x06 reads 0x153B .. 0x153D");
    check(dut.k_seq == 0 && dut.cnt_gate == 0 && dut.cnt_lvl == 0 && dut.cnt_hys == 0, "counter off at power-on, thresholds 0");
    // power-on read: nothing published, min reads 255, everything else 0
    rd_counter;
    check(r_st == 8'h00 && r_seq == 8'h00 && r_e == 0 && r_t == 0 && r_w == 0, "power-on 0x35: status, sequence, E, T, W zero");
    check(r_mn == 8'd255 && r_mx == 8'd0, "power-on 0x35: min 255, max 0 (no sample)");
    wr4(8'h0D, 32'd0);
    wr1(8'h53, 8'h00);
    // gate reload constants (100 ms and 1 s are not simulated in full)
    wr1(8'h61, 8'h02); #20; check(dut.k_gl == 29'd19999998, "gate 100 ms: 2e7 clocks");
    wr1(8'h61, 8'h03); #20; check(dut.k_gl == 29'd199999998, "gate 1 s: 2e8 clocks");
    wr1(8'h61, 8'h00);

    // EXT (src 3), 10 ms gate, two gates: interval, model, accuracy, the new flag, min / max 255 / 0
    select(2'd3, 2'd0);
    gate_result(1.0e9 / ext_p, ext_h / ext_p, 3.0e-6, 255, 255, 0, 0, "EXT 1234.5 ns");
    rd_counter;
    check(r_st[7] == 1'b0 && r_seq == dut.k_seq, "the same result read again: bit 7 clear");
    check(r_st[5:4] == 2'b10 && r_st[1:0] == 2'd1, "status: source EXT, gate 10 ms");
    check(r_mn == 8'd255 && r_mx == 8'd0, "EXT source: min 255, max 0");
    gate_result(1.0e9 / ext_p, ext_h / ext_p, 3.0e-6, 255, 255, 0, 0, "EXT 1234.5 ns, second gate");
    check(pub_iv == 2000000, "consecutive results exactly 2,000,000 clocks apart");
    // a sample readout with the counter read in the middle equals one with 0x06 read there
    raw_pass(8'h35, 1);
    raw_pass(8'h06, 0);
    b = 0;
    for (ii = 0; ii < 40; ii = ii + 1) if (pass35[ii] !== pass06[ii]) b = b + 1;
    check(b == 0, "reading 0x35 inside a readout leaves the sample readout unchanged");

    // CH1 square with noise, 200 MSa/s, level 130, hysteresis 8; src 1 = CH1 although 0x15 says CH2
    ext_en = 0;
    wr2(8'h62, {8'd130, 8'd8});
    amode = 0; noise = 2; t0a = $realtime;
    select(2'd1, 2'd1);
    gate_result(1.0e9 / a_p, a_h / a_p, 3.0e-6, 58, 62, 198, 202, "CH1 (src 1, 0x15 = CH2) square 777.7 ns, 200 MSa/s");
    check(r_st[5:4] == 2'b00, "status: source CH1");

    // the same at 10 MSa/s (divider 9). 0x153D: the encodes run at full rate at every divider, so the counter still sees
    // one sample per 5 ns and keeps the 200 MSa/s tolerance (up to 0x153C samples 50 ns apart, tolerance 2e-5)
    wr4(8'h0D, 32'd9);
    select(2'd1, 2'd0);
    gate_result(1.0e9 / a_p, a_h / a_p, 3.0e-6, 58, 62, 198, 202, "CH1 square 777.7 ns, 10 MSa/s (5 ns resolution)");
    wr4(8'h0D, 32'd0);

    // K = 1 (0x53 = 1, divider 1): the decimator forces the full-rate write clock, the counter keeps 5 ns resolution
    wr1(8'h53, 8'h01);
    wr4(8'h0D, 32'd1);
    select(2'd1, 2'd0);
    gate_result(1.0e9 / a_p, a_h / a_p, 3.0e-6, 58, 62, 198, 202, "CH1 square 777.7 ns, K = 1");
    wr1(8'h53, 8'h00);
    wr4(8'h0D, 32'd0);

    // CH2 (the complement) as the trigger source (0x15 = 1, src 0), then directly (src 2 with 0x15 = CH1)
    wr2(8'h62, {8'd125, 8'd8});
    select(2'd0, 2'd1);
    gate_result(1.0e9 / a_p, (a_p - a_h) / a_p, 3.0e-6, 53, 57, 193, 197, "CH2 (trigger source) square, 200 MSa/s");
    check(r_st[5:4] == 2'b01, "status: source CH2");
    select(2'd2, 2'd0);
    gate_result(1.0e9 / a_p, (a_p - a_h) / a_p, 3.0e-6, 53, 57, 193, 197, "CH2 (src 2, 0x15 = CH1) square, 200 MSa/s");
    check(r_st[5:4] == 2'b01, "status: source CH2");

    // EXT as the trigger source (0x15 = 2, src 0)
    ext_en = 1;
    select(2'd0, 2'd2);
    gate_result(1.0e9 / ext_p, ext_h / ext_p, 3.0e-6, 255, 255, 0, 0, "EXT (trigger source) 1234.5 ns");
    check(r_st[5:4] == 2'b10, "status: source EXT");
    ext_en = 0;

    // Schmitt thresholds, noise off: the codes are exactly 60 and 200
    noise = 0;
    wr2(8'h62, {8'd201, 8'd0});
    select(2'd1, 2'd0);
    gate_result(0.0, -1.0, 1.0, 60, 60, 200, 200, "level 201, hysteresis 0: never high");
    check(r_e == 0 && r_t == 0 && r_w == 0, "level 201: no edge counted (high needs code >= level)");
    wr2(8'h62, {8'd200, 8'd139});
    select(2'd1, 2'd0);
    gate_result(1.0e9 / a_p, a_h / a_p, 3.0e-6, 60, 60, 200, 200, "level 200, hysteresis 139: counts");
    wr2(8'h62, {8'd200, 8'd140});
    select(2'd1, 2'd0);
    gate_result(0.0, -1.0, 1.0, 60, 60, 200, 200, "level 200, hysteresis 140: never low");
    check(r_e == 0 && r_t == 0 && r_w == 0, "hysteresis 140: no edge counted (low needs code < level - hysteresis)");

    // slow noisy triangle, 20 us: hysteresis 0 counts false edges, 12 does not
    amode = 1; a_p = 20000.0; noise = 3; t0a = $realtime;
    wr2(8'h62, {8'd130, 8'd0});
    select(2'd1, 2'd0);
    gate_result(0.0, -1.0, 1.0, 57, 63, 197, 203, "triangle 20 us, hysteresis 0");
    e_hys0 = r_e;
    wr2(8'h62, {8'd130, 8'd12});
    select(2'd1, 2'd0);
    gate_result(5.0e4, -1.0, 1.0e-4, 57, 63, 197, 203, "triangle 20 us, hysteresis 12");   // high from 130 up, low below 118
    check(e_hys0 > 2 * r_e, "without hysteresis the noise adds edges");

    wr1(8'h61, 8'h00);
    $display("RESULT: %0d checks, %0d failed -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #400_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
