// Peak detect testbench (fpga/al3/src/zaklad.v, 0x153A, command 0x56 bit 1). Run with run.sh (ONLY=peak) or vvp.
//
// The ADC model of tb_trigpos.v (each core samples the analog input vin on its encode edge, 4 encode cycles of
// pipeline, 3.3 ns tPD) is driven with a random analog signal that changes every 2 to 7 ns, plus short spikes.
// A monitor logs the pins at every rising write clock edge (the decimator's capture, cores B then A in time) and every
// word written to the sample memory.
//  1. min / max: with K >= 1 and 0x56 bit 1, every 16-bit word is { max, min } of exactly the 2P samples the K = 1
//     boxcar sums for the same output: pairs (w - L - P + 1) .. (w - L) for the word written on write clock edge w, L
//     the boxcar's own latency in this monitor's edge numbering, measured first from a K = 1 capture with peak detect
//     off (its words are the plain sums of the same windows). Checked for every word of each capture and both
//     channels, at N = 2, 8, 20 and 200, K = 1..3, with the FIR enabled too (peak detect turns it off: 0x29 bit 5 = 0),
//     and through the bus (READ_RAW with the dummy strobe, 0x20 / 0x21 = CH1 min / max).
//  2. alias: 6 ns spikes on a flat signal, one every few microseconds. With the filter off (K = 0, 5 MSa/s: the cores
//     sample every 200 ns) most spikes fall between samples; with peak detect at the same rate every spike is in its
//     window's max.
//  3. trigger: rising edge at level 150 << 8 fires on the first window whose max reaches 150, falling at 128 << 8 on the
//     first window whose min drops below 128.
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
  wire pad;
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
  // analog input and the AD9288 model (CH2 = 255 - CH1)

  reg [7:0] vin = 40;
  reg [7:0] pA [0:7];
  reg [7:0] pB [0:7];
  integer ia, ib;
  initial for (ia = 0; ia < 8; ia = ia + 1) begin pA[ia] = 40; pB[ia] = 40; end
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

  // signal modes: 0 random (changes every 2..7 ns), 1 flat with 6 ns spikes (spike_hi up, else down)
  integer mode = 0;
  reg     spike_hi = 1;
  real    spike_every = 3000.0;
  integer n_spikes = 0;
  reg [7:0] base = 40, peakv = 250;
  always begin
    if (mode == 0) begin
      #(2.0 + (($random(seed) & 32'h7FFFFFFF) % 5000) * 0.001);
      vin = $random(seed);
    end else begin
      vin = base;
      #(spike_every * (0.5 + (($random(seed) & 32'h7FFFFFFF) % 1000) * 0.001));
      vin = peakv; n_spikes = n_spikes + 1;
      #6.0;
      vin = base;
    end
  end

  //---------------------------------------------------------------------------------
  // monitor: pins at each rising write clock edge (edge number), memory writes (edge number, word)

  localparam NL = 40000;
  reg  [7:0] lp1a [0:NL - 1];
  reg  [7:0] lp1b [0:NL - 1];
  reg  [7:0] lp2a [0:NL - 1];
  reg  [7:0] lp2b [0:NL - 1];
  integer    nedge = 0;
  integer    wedge [0:8191];                              // edge number of the write of memory word i
  reg [31:0] wword [0:8191];
  integer    nwr = 0;
  reg        mon = 0;
  reg        r_we, r_adv;
  integer    r_addr;
  always @(posedge dut.sample_write_clock) begin
    r_we = dut.sample_write_enable; r_adv = dut.swc_adv; r_addr = dut.sample_write_address;
    if (mon && nedge < NL) begin
      lp1a[nedge] = adc1A; lp1b[nedge] = adc1B; lp2a[nedge] = adc2A; lp2b[nedge] = adc2B;
      if (r_we && r_adv && !dut.cap_rst && nwr < 8192) begin
        wedge[r_addr] = nedge; wword[r_addr] = dut.sample_dia;
        if (r_addr + 1 > nwr) nwr = r_addr + 1;
      end
      nedge = nedge + 1;
    end
  end

  //---------------------------------------------------------------------------------
  // MCU bus

  real stb = 61.0;
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
  task rd1(input [7:0] c); begin wr_cmd(c); rd_byte; end endtask

  //---------------------------------------------------------------------------------
  // configuration and capture (pc_dec_apply, pc_hal_arm)

  integer done, polls, ta, i, j, k, w, p, lat, best, okc, cnt;
  task setup(input integer kk, input integer d, input [7:0] f56);
    begin
      wr4(8'h0D, d);
      wr1(8'h53, kk);
      wr1(8'h56, f56);
      wr1(8'h54, 8'd0);
      #((kk + 10) * ((d / 2) + 1) * 10 + 1000);
    end
  endtask

  task capture(input integer pre, input integer tot, input integer normal, input integer edge_dir, input [15:0] lvl);
    begin
      wr1(8'h15, 8'h00);
      wr1(8'h16, edge_dir);
      wr1(8'h17, 8'd128);
      wr2(8'h55, lvl);
      wr1(8'h0F, 8'h00);
      wr1(8'h1A, normal);
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05); polls = 0; rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      wr2(8'h0B, pre);
      wr2(8'h0C, tot);
      wr4(8'h0E, normal ? 32'h00FFFFFF : tot + 10);
      nedge = 0; nwr = 0; mon = 1;
      wr1(8'h01, 8'h00);
      polls = 0; done = 0;
      while (!done && polls < 400000) begin rd1(8'h0A); done = rd_d[0]; polls = polls + 1; end
      mon = 0;
      wr1(8'h0F, 8'h01);
      rd2(8'h14);
      ta = rd16 & 8191;
      check(done == 1, "capture done");
    end
  endtask

  // expected { max, min } of channel ch for the word written on edge we, window of p pairs ending lat edges before
  reg [7:0] emx, emn, v1, v2;
  task window(input integer ch, input integer we, input integer pp, input integer lt, output integer valid);
    integer e;
    begin
      emx = 0; emn = 255; valid = (we - lt - pp + 1 >= 0);
      for (e = we - lt - pp + 1; e <= we - lt && valid; e = e + 1) begin
        v1 = ch ? lp2b[e] : lp1b[e]; v2 = ch ? lp2a[e] : lp1a[e];
        if (v1 > emx) emx = v1; if (v2 > emx) emx = v2;
        if (v1 < emn) emn = v1; if (v2 < emn) emn = v2;
      end
    end
  endtask

  // the K = 1 boxcar (shift 0): every word the sum of its window; returns the latency in lat_box
  integer esum, lat_box;
  task window_sum(input integer ch, input integer we, input integer pp, input integer lt, output integer valid);
    integer e;
    begin
      esum = 0; valid = (we - lt - pp + 1 >= 0);
      for (e = we - lt - pp + 1; e <= we - lt && valid; e = e + 1)
        esum = esum + (ch ? lp2b[e] + lp2a[e] : lp1b[e] + lp1a[e]);
    end
  endtask
  task check_box(input integer pp);
    begin
      lat_box = -1;
      for (lat = 0; lat < 40 && lat_box < 0; lat = lat + 1) begin
        okc = 1; cnt = 0;
        for (i = 0; i < nwr && okc; i = i + 1) begin
          window_sum(0, wedge[i], pp, lat, valid);
          if (valid) begin
            cnt = cnt + 1;
            if (wword[i][15:0] !== esum) okc = 0;
            window_sum(1, wedge[i], pp, lat, valid);
            if (wword[i][31:16] !== esum) okc = 0;
          end
        end
        if (okc && cnt > 0) lat_box = lat;
      end
      $display("  K = 1 boxcar (peak off), N = %0d: %0d words are the sums of their windows at latency %0d", 2 * pp, cnt, lat_box);
      check(lat_box >= 0, "boxcar reference: every word is the sum of one window");
    end
  endtask

  // every written word against the windows: find the latency that explains all, require the boxcar's
  integer valid, nchk;
  task check_words(input integer pp, input [8*60-1:0] what);
    begin
      best = -1; nchk = 0;
      for (lat = 0; lat < 40 && best < 0; lat = lat + 1) begin
        okc = 1; cnt = 0;
        for (i = 0; i < nwr && okc; i = i + 1) begin
          window(0, wedge[i], pp, lat, valid);
          if (valid) begin
            cnt = cnt + 1;
            if (wword[i][15:0] !== {emx, emn}) okc = 0;
            window(1, wedge[i], pp, lat, valid);
            if (wword[i][31:16] !== {emx, emn}) okc = 0;
          end
        end
        if (okc && cnt > 0) begin best = lat; nchk = cnt; end
      end
      $display("  %0s: %0d words, latency %0d write clocks", what, nchk, best);
      check(best >= 0 && best == lat_box, what);
    end
  endtask

  // READ_RAW of CH1 (dummy strobe), compared with the memory
  reg [7:0] rlo, rhi;
  integer bad;
  task read_check(input integer start, input integer n);
    begin
      bad = 0;
      wr2(8'h1F, start & 8191);
      wr_cmd(8'h20); mcu_rws = 0; mcu_dcs = 0; mcu_strobe;
      for (i = 0; i < n; i = i + 1) begin
        mcu_strobe; rlo = bus;
        if (rlo !== dut.samples.mem[(start + i) & 8191][7:0]) bad = bad + 1;
      end
      mcu_rws = 1;
      wr2(8'h1F, start & 8191);
      wr_cmd(8'h21); mcu_rws = 0; mcu_dcs = 0; mcu_strobe;
      for (i = 0; i < n; i = i + 1) begin
        mcu_strobe; rhi = bus;
        if (rhi !== dut.samples.mem[(start + i) & 8191][15:8]) bad = bad + 1;
      end
      mcu_rws = 1;
      check(bad == 0, "READ_RAW 0x20 / 0x21 return the min / max bytes");
    end
  endtask

  integer seen, legacy_seen, peak_seen, sp_leg, sp_peak, kk, dd, q, idx;
  integer dlist [0:3];
  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    $display("tb_peak: seed %0d", seed);
    #300;
    wr1(8'h50, 8'h00);
    wr1(8'h57, 8'h00);
    rd2(8'h06);
    check(rd16 == 16'h153A, "cmd 0x06 reads 0x153A");

    // 1. min / max per window; first the boxcar's latency in this monitor's numbering
    mode = 0;
    setup(1, 19, 8'h00);
    capture(20, 300, 0, 0, 16'hFFFF);
    check_box(10);
    dlist[0] = 1; dlist[1] = 7; dlist[2] = 19; dlist[3] = 199;
    for (q = 0; q < 4; q = q + 1) begin
      dd = dlist[q];
      p = (dd / 2) + 1;
      kk = 1 + (q % 3);
      setup(kk, dd, (q >= 2) ? 8'h03 : 8'h02);              // with the FIR enabled at N = 20 and 200: it must stay off
      rd2(8'h29);
      check(rd16[15] == 1'b1 && rd16[13] == 1'b0 && rd16[7] == 1'b1 && rd16[6] == 1'b1,
            "0x29: 16-bit, FIR not active, peak active and enabled");
      capture(20, (dd == 199) ? 60 : 300, 0, 0, 16'hFFFF);
      check_words(p, (q == 0) ? "N = 2, K = 1" : (q == 1) ? "N = 8, K = 2" : (q == 2) ? "N = 20, K = 3, FIR on" : "N = 200, K = 1, FIR on");
      read_check(0, (nwr > 64) ? 64 : nwr);
    end

    // peak off again: 0x29 byte 1 bits 7..6 = 0, the CIC output (no { max, min } words)
    setup(1, 19, 8'h00);
    rd2(8'h29);
    check(rd16[7:6] == 2'b00, "0x29 byte 1: peak bits clear with 0x56 bit 1 = 0");

    // 2. alias: 6 ns spikes on a flat signal at 5 MSa/s (divider 19 / N = 20), filter off and peak detect
    mode = 1; base = 40; peakv = 250; spike_every = 2000.0;
    setup(0, 19, 8'h00);
    n_spikes = 0;
    capture(20, 4000, 0, 0, 16'hFFFF);
    sp_leg = n_spikes; legacy_seen = 0;
    for (i = 1; i < nwr; i = i + 1)
      if ((wword[i][7:0] > 8'd150 || wword[i][15:8] > 8'd150) && !(wword[i - 1][7:0] > 8'd150 || wword[i - 1][15:8] > 8'd150))
        legacy_seen = legacy_seen + 1;
    setup(1, 19, 8'h02);
    n_spikes = 0;
    capture(20, 4000, 0, 0, 16'hFFFF);
    sp_peak = n_spikes; peak_seen = 0;
    for (i = 1; i < nwr; i = i + 1)
      if (wword[i][15:8] > 8'd150 && wword[i - 1][15:8] <= 8'd150) peak_seen = peak_seen + 1;
    $display("  alias: filter off saw %0d of about %0d spikes, peak detect %0d of about %0d", legacy_seen, sp_leg, peak_seen, sp_peak);
    check(peak_seen >= sp_peak - 2 && peak_seen <= sp_peak, "peak detect shows every spike (the capture window holds all but the edges)");
    check(legacy_seen * 3 < sp_leg, "filter off (sample skipping) misses most spikes");
    check_words(10, "spikes, N = 20");

    // 3. trigger on the window max (rising) and min (falling)
    base = 40; peakv = 210; spike_every = 4000.0;
    setup(1, 19, 8'h02);
    capture(100, 200, 1, 0, {8'd150, 8'd0});
    idx = (ta + 1) & 8191;
    check(dut.samples.mem[idx][15:8] >= 8'd150 && dut.samples.mem[(idx - 1) & 8191][15:8] < 8'd150,
          "peak, rising at 150: the trigger word is the first window whose max reaches 150");
    base = 210; peakv = 40;
    capture(100, 200, 1, 1, {8'd128, 8'd0});
    idx = (ta + 1) & 8191;
    check(dut.samples.mem[idx][7:0] < 8'd128 && dut.samples.mem[(idx - 1) & 8191][7:0] >= 8'd128,
          "peak, falling at 128: the trigger word is the first window whose min drops below 128");
    wr1(8'h56, 8'h00); wr1(8'h53, 8'h00);

    $display("RESULT: %0d checks, %0d failed -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #2_000_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
