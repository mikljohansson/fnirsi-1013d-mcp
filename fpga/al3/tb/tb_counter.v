// Hardware counter testbench (fpga/al3/src/zaklad.v, 0x153A: commands 0x61, 0x62, read 0x35). Run with run.sh
// (ONLY=counter) or vvp. About 60 ms of simulated time.
//
//  1. gate: the published results come exactly 2,000,000 clk_200MHz cycles apart (10 ms); the 100 ms and 1 s reloads
//     (not simulated in full) are checked as constants.
//  2. bookkeeping: E, T, W of every gate equal a reference model run on the counter's input stream (k_cs, sampled at
//     every clock): first and last rising edge in the gate, periods, clocks and high clocks between them.
//  3. accuracy against the stimulus: EXT square 1234.5 ns / 400 ns high; CH1 square through the AD9288 model with
//     +-2 codes of noise at 200 MSa/s and at 10 MSa/s; frequency within 3e-6 (200 MSa/s, EXT) or 2e-5 (10 MSa/s), duty
//     within 0.2 %.
//  4. hysteresis: a slow noisy triangle on CH1 counts many false edges with hysteresis 0 and the true frequency with 12.
//  5. the MCU side: read 0x35 returns the snapshot taken at its command byte; bit 7 of byte 0 is set for a result not
//     read before and clear when read again; reading 0x35 in the middle of a sample readout leaves the readout exactly
//     as reading 0x06 there does.
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
  // stimulus: EXT square, and the analog CH1 input (square or triangle, with noise) through the AD9288 model

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
    #(1.0 + (($random(seed) & 32'h7FFFFFFF) % 2000) * 0.001);
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
  // reference model on the counter's input stream, run at every clock; publish times

  integer clk_n = 0;
  integer m_run = 0, m_t = 0, m_w = 0, m_e = 0, m_tl = 0, m_wl = 0, m_el = 0;
  integer x_t = 0, x_w = 0, x_e = 0;                     // the model's result for the gate that just ended
  integer pub_n = 0, pub_last = -1, pub_iv = 0;
  reg     s_prev = 0, s_now = 0, was_on = 0, g_end = 0;
  reg [7:0] seq_prev = 0;
  always @(posedge dut.clk_200MHz) begin
    // values before this edge
    s_now = dut.k_cs; s_prev = dut.k_csp; was_on = dut.k_on; g_end = dut.k_g[28];
    seq_prev = dut.k_seq;
    if (!was_on) begin m_run = 0; m_tl = 0; m_wl = 0; m_el = 0; end
    else if (g_end) begin
      x_t = m_tl; x_w = m_wl; x_e = m_el;
      m_run = 0; m_tl = 0; m_wl = 0; m_el = 0;
      pub_iv = clk_n - pub_last; pub_last = clk_n; pub_n = pub_n + 1;
    end else if (m_run) begin
      if (s_now && !s_prev) begin m_tl = m_t; m_wl = m_w; m_el = m_e + 1; m_e = m_e + 1; end
      m_t = m_t + 1; m_w = m_w + s_now;
    end else if (s_now && !s_prev) begin
      m_run = 1; m_t = 1; m_w = 1; m_e = 0;
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

  // read 0x35: status, sequence, E, T, W
  reg [7:0]  r_st, r_seq;
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
    end
  endtask

  // wait for the next publish, read it, compare with the model and the stimulus
  integer pn;
  real    f_true, f_meas, d_true, d_meas, ferr;
  task gate_result(input real fexp, input real dexp, input real ftol, input [8*50-1:0] what);
    begin
      pn = pub_n;
      wait (pub_n == pn + 1);
      #200;
      rd_counter;
      f_meas = (r_t > 0) ? r_e / (r_t * 5.0e-9) : 0.0;
      d_meas = (r_t > 0) ? (r_w * 1.0) / r_t : 0.0;
      ferr = (fexp > 0) ? (f_meas - fexp) / fexp : 0.0;
      $display("  %0s: E %0d T %0d W %0d -> f %0.3f Hz (expected %0.3f, error %0.2e), duty %0.4f (%0.4f), status %h seq %0d, gate %0d clocks",
               what, r_e, r_t, r_w, f_meas, fexp, ferr, d_meas, dexp, r_st, r_seq, pub_iv);
      check(r_e == x_e && r_t == x_t && r_w == x_w, "E, T, W equal the reference model on the input stream");
      check(r_seq == dut.k_seq && r_st[7] == 1'b1, "a new result: sequence number current, bit 7 set");
      if (fexp > 0) begin
        check(ferr < ftol && ferr > -ftol, "frequency within tolerance of the stimulus");
        if (dexp >= 0.0) check(d_meas - dexp < 0.002 && d_meas - dexp > -0.002, "duty within 0.2 % of the stimulus");
      end
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
    check(rd16 == 16'h153A, "cmd 0x06 reads 0x153A");
    check(dut.k_seq == 0 && dut.cnt_gate == 0, "counter off at power-on");
    wr4(8'h0D, 32'd0);
    wr1(8'h53, 8'h00);
    // gate reload constants (100 ms and 1 s are not simulated in full)
    wr1(8'h61, 8'h02); #20; check(dut.k_gl == 29'd19999998, "gate 100 ms: 2e7 clocks");
    wr1(8'h61, 8'h03); #20; check(dut.k_gl == 29'd199999998, "gate 1 s: 2e8 clocks");
    wr1(8'h61, 8'h00);

    // EXT, 10 ms gate, two gates: interval, model, accuracy, the new flag
    wr1(8'h61, {4'd0, 2'd3, 2'd1});
    gate_result(1.0e9 / ext_p, ext_h / ext_p, 3.0e-6, "EXT 1234.5 ns");
    rd_counter;
    check(r_st[7] == 1'b0 && r_seq == dut.k_seq, "the same result read again: bit 7 clear");
    check(r_st[5:4] == 2'b10 && r_st[1:0] == 2'd1, "status: source EXT, gate 10 ms");
    gate_result(1.0e9 / ext_p, ext_h / ext_p, 3.0e-6, "EXT 1234.5 ns, second gate");
    check(pub_iv == 2000000, "consecutive results exactly 2,000,000 clocks apart");
    // a sample readout with the counter read in the middle equals one with 0x06 read there
    raw_pass(8'h35, 1);
    raw_pass(8'h06, 0);
    b = 0;
    for (ii = 0; ii < 40; ii = ii + 1) if (pass35[ii] !== pass06[ii]) b = b + 1;
    check(b == 0, "reading 0x35 inside a readout leaves the sample readout unchanged");

    // CH1 square with noise, 200 MSa/s, level 130, hysteresis 8
    ext_en = 0;
    wr2(8'h62, {8'd130, 8'd8});
    wr1(8'h61, {4'd0, 2'd1, 2'd1});
    amode = 0; noise = 2; t0a = $realtime;
    gate_result(1.0e9 / a_p, a_h / a_p, 3.0e-6, "CH1 square 777.7 ns, 200 MSa/s");
    check(r_st[5:4] == 2'b00, "status: source CH1");

    // the same at 10 MSa/s (divider 9): samples 50 ns apart
    wr4(8'h0D, 32'd9);
    wr1(8'h61, 8'h00); wr1(8'h61, {4'd0, 2'd1, 2'd1});
    gate_result(1.0e9 / a_p, a_h / a_p, 2.0e-5, "CH1 square 777.7 ns, 10 MSa/s");
    wr4(8'h0D, 32'd0);

    // CH2 (the complement, the trigger source by 0x15 = 1, counter source 0 = follow the trigger)
    wr1(8'h15, 8'h01);
    wr2(8'h62, {8'd125, 8'd8});
    wr1(8'h61, 8'h00); wr1(8'h61, {4'd0, 2'd0, 2'd1});
    gate_result(1.0e9 / a_p, (a_p - a_h) / a_p, 3.0e-6, "CH2 (trigger source) square, 200 MSa/s");
    check(r_st[5:4] == 2'b01, "status: source CH2");
    wr1(8'h15, 8'h00);

    // slow noisy triangle, 20 us: hysteresis 0 counts false edges, 12 does not
    amode = 1; a_p = 20000.0; noise = 3; t0a = $realtime;
    wr2(8'h62, {8'd130, 8'd0});
    wr1(8'h61, 8'h00); wr1(8'h61, {4'd0, 2'd1, 2'd1});
    gate_result(0.0, -1.0, 1.0, "triangle 20 us, hysteresis 0");
    e_hys0 = r_e;
    wr2(8'h62, {8'd130, 8'd12});
    wr1(8'h61, 8'h00); wr1(8'h61, {4'd0, 2'd1, 2'd1});
    gate_result(5.0e4, -1.0, 1.0e-4, "triangle 20 us, hysteresis 12");   // duty: high from 130 up, low below 118
    check(e_hys0 > 2 * r_e, "without hysteresis the noise adds edges");

    wr1(8'h61, 8'h00);
    $display("RESULT: %0d checks, %0d failed -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #200_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
