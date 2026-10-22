// Full-rate encodes with sample skipping (fpga/al3/src/zaklad.v, 0x153D, the owner's decision of 2026-10-21 17:59).
// Run with fpga/al3/tb/run.sh (ONLY=skip) or vvp (+seed). One instance, an AD9288 model on the dut's own encode pins.
// Up to 0x153C the divider 0x0D gated the encodes with K = 0 (Atlan4's sample skipping at 100 / (D + 1) MHz); the
// lockstep equivalence benches (tb_ext_trigger, tb_decim, tb_fir, tb_seg) compared those captures bit for bit with the
// 0x1532 .. 0x1535 references. Now the write clock and the encodes run at 100 MHz per core at every setting and the
// write path stores one captured pair per D + 1 write clock edges; the references cannot model that, so the K = 0
// random phases of those benches run at D = 0 (where the lockstep still holds) and this bench checks the D > 0 case
// against a model of the dut's own full-rate pair stream (adc*_w at every write clock edge, as tb_peak.v logs it):
//   1. geometry: for D in {1, 2, 3, 9, 19, 199, 399} (K = 0, random data): every stored word is the captured pair of
//      its edge, consecutive words are exactly D + 1 edges (10 (D + 1) ns) apart, the first word of a record comes
//      within D + 1 edges of the release, the write clock never stops toggling (the edge count grows by 100 per us),
//      the auto time-out and 0x14 = W - total + pretrigger count stored words, and the 0x05 ready wait is six write
//      clocks (60 ns) at every rate instead of six words.
//   2. trigger on the stored words: a CH1 step with the capture in normal mode at every D: the record's crossing lies at
//      index pretrigger + 1 (0x14 convention), as at D = 0, and every record sample before the edge reads LO.
//   3. EXT at D = 49 (500 ns words): an EXT edge mid-word gives 0x14 = the last word written before the edge (the EXT
//      apply waits for the next stored edge, as it waits for the filter's output strobe with K >= 1).
//   4. the hardware counter with the filter off at D = 399 (the bench's miscount on 0x153C: 1 kHz read 1000 / 3005 /
//      2764 / 1141 Hz): a 100 kHz square on CH1 over a 10 ms gate gives E 999 .. 1001, T = E x 2000 clocks, duty 50 %,
//      min / max the square's codes: the counter sees the full-rate stream whatever the divider.
//   5. a divider change while idle (9 -> 399 -> 2) is in effect for the next capture (the skip counter reloads within one
//      cycle of the old or the new value), and K >= 1 is untouched (one filtered capture at D = 19 as a smoke check:
//      words = the K = 1 boxcar sums of their windows).
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
  // AD9288 model (as tb_peak.v): sample vin on the encode rising edge, out 4 encodes later, 3.3 ns after the edge

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

  // vin modes: 0 random (changes every 2..7 ns), 1 held at `level` (the step cases set it), 2 square of half period sq_half
  integer  mode = 0;
  reg [7:0] level = 40;
  real     sq_half = 5000.0;
  localparam [7:0] LO = 8'd40, HI = 8'd210;
  always begin
    if (mode == 0) begin
      #(2.0 + (($random(seed) & 32'h7FFFFFFF) % 5000) * 0.001);
      vin = $random(seed);
    end else if (mode == 1) begin
      vin = level; #10;
    end else begin
      vin = HI; #(sq_half); vin = LO; #(sq_half);
    end
  end

  //---------------------------------------------------------------------------------
  // monitor (as tb_peak.v): the captured pairs adc*_w at every rising write clock edge, the memory writes per edge

  localparam NL = 400000;
  reg  [7:0] lp1a [0:NL - 1];
  reg  [7:0] lp1b [0:NL - 1];
  reg  [7:0] lp2a [0:NL - 1];
  reg  [7:0] lp2b [0:NL - 1];
  real       ltime  [0:NL - 1];                             // time of the edge
  integer    nedge = 0;
  integer    wedge [0:8191];
  reg [31:0] wword [0:8191];
  integer    nwr = 0;
  reg        mon = 0;
  reg        r_we, r_adv;
  integer    r_addr;
  always @(posedge dut.sample_write_clock) begin
    r_we = dut.sample_write_enable; r_adv = dut.swc_adv; r_addr = dut.sample_write_address;
    if (mon && nedge < NL) begin
      lp1a[nedge] = dut.adc1A_w; lp1b[nedge] = dut.adc1B_w; lp2a[nedge] = dut.adc2A_w; lp2b[nedge] = dut.adc2B_w;
      ltime[nedge] = $realtime;
      if (r_we && r_adv && !dut.cap_rst && r_addr < 8192) begin
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
  // the firmware's arm (pc_hal_arm), the capture and its bookkeeping

  integer done, polls, ta, i, j, k, w, okc, cur_d, rel_edge, ready_polls, gaps, gapbad, valbad, xcr;
  reg [7:0] a6 [0:7];                       // phase 6 reads
  reg [7:0] q6 [0:2];
  reg [7:0] r6 [0:2];
  reg [7:0] b6 [0:1];
  real    t_rel, t_done;

  task setup(input integer kk, input integer d);
    begin
      wr4(8'h0D, d);
      wr1(8'h53, kk);
      wr1(8'h56, 8'h00);
      wr1(8'h54, 8'd0);
      cur_d = d;
      #((kk + 10) * ((d / 2) + 1) * 10 + 1000);
    end
  endtask

  task capture(input [1:0] src, input integer pre, input integer tot, input integer normal, input integer edge_dir,
               input [31:0] tmo, input [7:0] lvl);
    begin
      wr1(8'h15, {6'd0, src});
      wr1(8'h16, edge_dir);
      wr1(8'h17, lvl);
      wr2(8'h55, {lvl, 8'h00});
      wr1(8'h1A, normal);
      wr2(8'h0B, pre);
      wr2(8'h0C, tot);
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05); polls = 0; rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      ready_polls = polls;
      wr2(8'h0B, pre);
      wr2(8'h0C, tot);
      wr1(8'h5E, 8'h00);
      wr4(8'h0E, tmo);
      for (i = 0; i < 8192; i = i + 1) wedge[i] = -1;
      nedge = 0; nwr = 0; mon = 1;
      wr1(8'h01, 8'h00);
      rel_edge = nedge; t_rel = $realtime;
      polls = 0; done = 0;
      while (!done && polls < 400000) begin rd1(8'h0A); done = rd_d[0]; polls = polls + 1; end
      t_done = $realtime;
      mon = 0;
      rd2(8'h14);
      ta = rd16 & 8191;
      check(done == 1, "capture done");
    end
  endtask

  // the stored words against the pair stream: every word = the pair of its edge, consecutive words D + 1 edges apart
  task check_geometry(input integer d, input [8*40-1:0] name);
    begin
      gaps = 0; gapbad = 0; valbad = 0;
      for (i = 0; i < nwr; i = i + 1)
        if (wedge[i] >= 0) begin
          if (wword[i] !== { lp2b[wedge[i]], lp2a[wedge[i]], lp1b[wedge[i]], lp1a[wedge[i]] }) valbad = valbad + 1;
          if (i > 0 && wedge[i - 1] >= 0) begin
            gaps = gaps + 1;
            if (wedge[i] - wedge[i - 1] != d + 1) gapbad = gapbad + 1;
          end
        end
      $display("  %0s D %0d: %0d words, %0d gaps of other than %0d edges, %0d words not the pair of their edge; first word %0d edges after the release; %0d edges in %0.1f us (%0.2f per 10 ns); ready after %0d polls",
               name, d, nwr, gapbad, d + 1, valbad, wedge[0] - rel_edge, nedge, (t_done - t_rel) / 1000.0,
               nedge * 10.0 / (t_done - t_rel), ready_polls);
      check(nwr > 0 && valbad == 0, "every stored word is the captured pair of its write clock edge");
      check(gaps > 0 && gapbad == 0, "consecutive words are exactly D + 1 write clock edges apart");
      check(wedge[0] >= 0 && wedge[0] - rel_edge <= d + 1, "the first word comes within D + 1 edges of the release");
      check(nedge * 10.0 > 0.95 * (t_done - t_rel) && nedge * 10.0 < 1.05 * (t_done - t_rel) + 1000.0,
            "the write clock toggles every 10 ns at this rate too");
    end
  endtask

  integer dlist [0:6];
  integer nq, lat_box, esum, cnt, valid;
  task window_sum(input integer ch, input integer we, input integer pp, input integer lt, output integer valid);
    integer e;
    begin
      esum = 0; valid = (we - lt - pp + 1 >= 0) && (we < NL);
      for (e = we - lt - pp + 1; e <= we - lt && valid; e = e + 1)
        esum = esum + (ch ? lp2b[e] + lp2a[e] : lp1b[e] + lp1a[e]);
    end
  endtask

  reg [7:0]  cnt_b [0:15];
  integer    ke, kt, kw;
  reg [12:0] wa_at_edge;

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    $display("tb_skip: seed %0d", seed);
    #300;
    rd2(8'h06);
    check(rd16 == 16'h153D, "cmd 0x06 reads 0x153D");
    wr4(8'h50, 32'd0);

    //-------------------------------------------------------------------------------
    // 1. geometry at every D (K = 0, random data, auto mode with the time-out = total words)
    dlist[0] = 1; dlist[1] = 2; dlist[2] = 3; dlist[3] = 9; dlist[4] = 19; dlist[5] = 199; dlist[6] = 399;
    mode = 0;
    for (nq = 0; nq < 7; nq = nq + 1) begin
      setup(0, dlist[nq]);
      capture(2'd0, 10, 40, 0, 0, 32'd40, 8'hFF);          // level 255: never crossed, the time-out ends the capture
      check_geometry(dlist[nq], "random data, auto time-out");
      check(ta == ((dut.w_last - 13'd40 + 13'd10) & 13'h1FFF), "auto time-out: 0x14 = W - total + pretrigger (stored words)");
      check(nwr >= 40 && nwr <= 44, "the time-out stops the write at 0x0E + 2 stored words (40 .. 44 words)");
      check(ready_polls < 40, "0x05 ready within 40 polls (six write clocks of 10 ns, not six words)");
    end

    //-------------------------------------------------------------------------------
    // 2. the trigger on the stored words: a CH1 step at every D, normal mode, pre 10 total 40
    for (nq = 0; nq < 7; nq = nq + 1) begin
      setup(0, dlist[nq]);
      mode = 1; level = LO;
      fork
        capture(2'd0, 10, 40, 1, 0, 32'h00FFFFFF, 8'd128);
        begin
          wait (dut.sample_system_reset === 1'b1); wait (dut.sample_system_reset === 1'b0);
          #(20 * 10 * (dlist[nq] + 1) + 300);             // 20 words of LO after the release, then the step
          level = HI;
        end
      join
      check_geometry(dlist[nq], "CH1 step, normal mode");
      xcr = -1; okc = 1;
      for (j = 1; j < 40 && xcr < 0; j = j + 1)
        if (dut.samples.mem[(ta - 10 + j - 1) & 8191][7:0] < 128 && dut.samples.mem[(ta - 10 + j) & 8191][7:0] >= 128)
          xcr = j;
      for (j = 0; j < 10; j = j + 1) if (dut.samples.mem[(ta - 10 + j) & 8191][7:0] >= 128) okc = 0;
      $display("  step D %0d: crossing at record index %0d (expect 11), 0x14 %0d", dlist[nq], xcr, ta);
      check(xcr == 11 && okc, "the step's crossing at record index pretrigger + 1 on the stored words, LO before it");
      mode = 0;
    end

    //-------------------------------------------------------------------------------
    // 3. EXT at D = 49: the edge mid-word, 0x14 = the last word written before it
    setup(0, 49);
    mode = 1; level = LO;
    ext_en = 1; ext_v = 0;
    fork
      capture(2'd2, 100, 200, 1, 0, 32'h00FFFFFF, 8'd128);
      begin
        wait (dut.sample_system_reset === 1'b1); wait (dut.sample_system_reset === 1'b0);
        wait (dut.sampling_half_way === 1'b1);
        #3000;
        @(posedge dut.sample_write_clock);
        #250;
        ext_v = 1;
        wa_at_edge = dut.sample_write_address;
      end
    join
    $display("  EXT D 49: edge at write address %0d, 0x14 %0d", wa_at_edge, ta);
    check(ta == ((wa_at_edge - 13'd1) & 13'h1FFF), "EXT at D = 49: 0x14 = the last stored word before the edge");
    check_geometry(49, "EXT, normal mode");
    ext_v = 0; ext_en = 0;
    mode = 0;

    //-------------------------------------------------------------------------------
    // 4. the counter with the filter off at D = 399: a 100 kHz square, gate 10 ms (the 0x153C bench miscount)
    setup(0, 399);
    mode = 2; sq_half = 5000.0;
    wr_cmd(8'h62); wr_byte(8'd128); wr_byte(8'd6);
    wr1(8'h61, 8'h05);                                    // gate 10 ms, source CH1
    #10600000;
    wr_cmd(8'h35);
    for (i = 0; i < 16; i = i + 1) begin rd_byte; cnt_b[i] = rd_d; end
    ke = {cnt_b[2], cnt_b[3], cnt_b[4], cnt_b[5]};
    kt = {cnt_b[6], cnt_b[7], cnt_b[8], cnt_b[9]};
    kw = {cnt_b[10], cnt_b[11], cnt_b[12], cnt_b[13]};
    $display("  counter D 399 K 0: flags %h E %0d T %0d W %0d min %0d max %0d", cnt_b[0], ke, kt, kw, cnt_b[14], cnt_b[15]);
    check(cnt_b[0][7] == 1'b1, "counter: a result published");
    check(ke >= 999 && ke <= 1001, "counter at D = 399, filter off: E = 999 .. 1001 periods of 100 kHz in 10 ms");
    check(ke > 0 && kt == ke * 2000, "counter at D = 399: T = E x 2000 clocks (the full-rate stream, 5 ns resolution)");
    check(ke > 0 && kw * 2 >= kt - 2 * ke && kw * 2 <= kt + 2 * ke, "counter at D = 399: duty 50 % within one sample per period");
    check(cnt_b[14] == LO && cnt_b[15] == HI, "counter at D = 399: min / max = the square's codes");
    wr1(8'h61, 8'h00);
    mode = 0;

    //-------------------------------------------------------------------------------
    // 5. divider changes while idle, then K = 1 untouched
    setup(0, 9);
    wr4(8'h0D, 32'd399);
    #100;
    wr4(8'h0D, 32'd2);
    cur_d = 2;
    #100;
    capture(2'd0, 10, 40, 0, 0, 32'd40, 8'hFF);
    check_geometry(2, "after 9 -> 399 -> 2 while idle");
    setup(1, 19);                                         // K = 1 boxcar, N = 20: the filter path as before
    capture(2'd0, 10, 40, 0, 0, 32'd40, 8'hFF);
    lat_box = -1;
    for (k = 0; k < 40 && lat_box < 0; k = k + 1) begin
      okc = 1; cnt = 0;
      for (i = 0; i < nwr && okc; i = i + 1) begin
        window_sum(0, wedge[i], 10, k, valid);
        if (valid) begin
          cnt = cnt + 1;
          if (wword[i][15:0] !== esum) okc = 0;
          window_sum(1, wedge[i], 10, k, valid);
          if (wword[i][31:16] !== esum) okc = 0;
        end
      end
      if (okc && cnt > 0) lat_box = k;
    end
    $display("  K = 1 N = 20 at D = 19: %0d words are the boxcar sums of their windows at latency %0d", cnt, lat_box);
    check(lat_box >= 0, "K = 1 unchanged: every word the K = 1 boxcar sum of its window");
    gapbad = 0;
    for (i = 1; i < nwr; i = i + 1) if (wedge[i] >= 0 && wedge[i - 1] >= 0 && wedge[i] - wedge[i - 1] != 10) gapbad = gapbad + 1;
    check(nwr > 0 && gapbad == 0, "K = 1 at D = 19: one word per P = 10 write clocks as before");

    // 6. (fpga-d3c register audit) what the firmware can read between the 0x05 ready and the first stored word at the
    // slowest K = 0 rate it uses (D = 199999, 1 kSa/s). Up to 0x153C the 0x05 wait was six stored words at every
    // rate, so registers loaded only on a stored write saw one inside the reset window; now the wait is 60 ns and the
    // first word comes up to D + 1 edges (2 ms) after the release. After a D = 1 auto record that left them non-zero,
    // the release is placed 1.5 ms before the skip counter's next stored edge, and 0x14, 0x0A, 0x18, 0x38 (W, count,
    // occupancy) and 0x34 (the ADC qualifier) are read before that word: they must read their reset values (0x34 the
    // value it reads inside the 0x01 = 1 window); after the first word 0x38 must read W = 1, count 1.
    setup(0, 1);
    capture(2'd0, 10, 40, 0, 0, 32'd40, 8'hFF);
    wr_cmd(8'h38); for (k = 0; k < 8; k = k + 1) begin rd_byte; a6[k] = rd_d; end
    $display("  audit: the D = 1 record left 0x14 = %0d, W = %0d, count %0d", ta, {a6[0][4:0], a6[1]}, {a6[2], a6[3], a6[4], a6[5]});
    check(ta != 0 && {a6[0][4:0], a6[1]} != 0, "audit: the previous record left 0x14 and W non-zero");
    wr4(8'h0D, 32'd199999); cur_d = 199999;
    wr1(8'h15, 8'd0); wr1(8'h16, 8'd0); wr1(8'h17, 8'hFF); wr2(8'h55, 16'hFF00); wr1(8'h1A, 8'd0);
    wr2(8'h0B, 16'd10); wr2(8'h0C, 16'd40);
    wr1(8'h01, 8'h01);
    wr_cmd(8'h05); polls = 0; rd_byte;
    while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
    wr2(8'h0B, 16'd10); wr2(8'h0C, 16'd40); wr1(8'h5E, 8'h00); wr4(8'h0E, 32'd40);
    wr_cmd(8'h34); for (k = 0; k < 3; k = k + 1) begin rd_byte; q6[k] = rd_d; end
    wait (dut.skip_cnt == 150000);
    for (i = 0; i < 8192; i = i + 1) wedge[i] = -1;
    nedge = 0; nwr = 0; mon = 1;
    wr1(8'h01, 8'h00);
    rel_edge = nedge; t_rel = $realtime;
    rd2(8'h14); ta = rd16 & 8191;
    rd1(8'h0A); b6[0] = rd_d;
    rd1(8'h18); b6[1] = rd_d;
    wr_cmd(8'h38); for (k = 0; k < 8; k = k + 1) begin rd_byte; a6[k] = rd_d; end
    wr_cmd(8'h34); for (k = 0; k < 3; k = k + 1) begin rd_byte; r6[k] = rd_d; end
    $display("  audit at D = 199999, %0.0f ns after the release, %0d words stored: 0x14 %0d, 0x0A %0d, 0x18 %0d, W %0d, count %0d, occ %0d, 0x34 %h%h%h (in the window %h%h%h)",
             $realtime - t_rel, nwr, ta, b6[0][0], b6[1][0], {a6[0][4:0], a6[1]}, {a6[2], a6[3], a6[4], a6[5]}, {a6[6][5:0], a6[7]},
             r6[0], r6[1], r6[2], q6[0], q6[1], q6[2]);
    check(nwr == 0, "audit: the reads came before the first stored word");
    check(ta == 0 && b6[0][0] == 0 && b6[1][0] == 0, "audit: 0x14 = 0, 0x0A = 0, 0x18 = 0 before the first stored word at D = 199999");
    check({a6[0][4:0], a6[1]} == 0 && {a6[2], a6[3], a6[4], a6[5]} == 0 && {a6[6][5:0], a6[7]} == 0,
          "audit: 0x38 W = 0, count 0, occupancy 0 before the first stored word at D = 199999");
    check(r6[0] == q6[0] && r6[1] == q6[1] && r6[2] == q6[2], "audit: 0x34 reads its reset-window value before the first stored word");
    wait (nwr > 0);
    #100;
    wr_cmd(8'h38); for (k = 0; k < 8; k = k + 1) begin rd_byte; a6[k] = rd_d; end
    $display("  audit: first word %0.3f ms after the release; then W %0d, count %0d", ($realtime - t_rel) / 1.0e6, {a6[0][4:0], a6[1]}, {a6[2], a6[3], a6[4], a6[5]});
    check({a6[0][4:0], a6[1]} == 1 && {a6[2], a6[3], a6[4], a6[5]} == 1, "audit: after the first stored word 0x38 reads W = 1, count 1");
    mon = 0;
    wr1(8'h01, 8'h01); wr4(8'h0D, 32'd0); #200; wr1(8'h01, 8'h00); #200;

    $display("RESULT: %0d checks, %0d failed -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #60_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
