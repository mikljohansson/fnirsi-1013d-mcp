// Streaming logger testbench (fpga/al3/src/zaklad.v, 0x153D item D1: commands 0x69, 0x6A, read 0x38; contract
// docs/plans/fpga-0x153c.md section 6). Run with run.sh (ONLY=stream) or vvp (+seed, +chunks).
//
// The testbench mirrors every word the capture logic writes into the sample memory (address, data, write index, time)
// and drives the FPGA with the logger firmware's byte sequences (fw-d4): the arm with 0x69 = 1, 0x57 = 0, 0x1A, 0x0B =
// 0x0C = 0, 0x01 = 1, the 0x05 wait, 0x0E = 0, 0x01 = 0, then 0x38, 0x6A = 0, 0x69 = 3; the drain loop 0x38, then per
// lane 0x1F / command / dummy strobe / n strobes as READ_RAW (split at the ring end), then 0x6A = the next word to read.
// Checks:
//   0. before any 0x69 write: normal captures as tb_timeout checks them (auto time-out record, triggered record, a
//      1.3-ring time-out) are the 0x153C ones: equal to the mirror, contiguous, the newest total words; the lap flag
//      stays 0 although the ring wrapped (0x69 = 0 at power-on = 0x153C).
//   1. start and steady streaming at several rates and K settings (legacy D 0 / 199 / 399 / 1999, K 1 D 1, K 3 + FIR
//      D 799 / 1599 / 31): after the arm the write never stops (sample_write_enable stays 1, 0x0A reads 0, 0x18 reads 0
//      although the auto time-out 0x0E = 0 has long elapsed, or the trigger level is crossed in normal mode) and the
//      sustained rate measured from two 0x38 snapshots equals the word rate of the configuration.
//   2. read 0x38: the words-written counter equals the number of memory writes since word 0 (the mirror, the DUT's
//      12.5 ns counting latency applied), W == counter mod 8192 (word 0 at ring word 0), reserved bits 0, the unread count
//      equals the counter minus the words the reader consumed (saturating at 8192).
//   3. the reader keeping up: chunks read with the firmware's sequences at the MCU's strobe timing (300 ns) at the
//      contract's three rate tiers (1 ch 8-bit 500 kS/s = D 399, 1 ch 16-bit 250 kS/s = D 799 K 3 FIR, 2 ch 16-bit
//      125 kS/s = D 1599 K 3 FIR) and at D 199 (1 MS/s legacy) with a fast bus: every byte equals the mirror, every word
//      is exactly the next word of the session (write index), across the ring end.
//   4. the reader falling behind: the lap flag is 0 after word R + 8191 (unread 8192 = full) and 1 after word R + 8192
//      (the write that overwrote the oldest unread word), at 100 Mwords/s and at 5 Mwords/s; the resync (0x6A = W from
//      0x38, 0x69 = 3) clears it and the next chunk is contiguous from there; an empty ring (0x6A = W) takes the next
//      write without a lap (unread 0 -> 1).
//   5. stop: 0x69 = 0 in auto mode stops the write at the next edges (0x0A = 1, 0x18 = 0, W and the counter frozen, 0x14
//      = W, the tail drains contiguous to the last word); in normal mode the capture runs on and triggers (0x18 = 1).
//   6. restart: a new arm counts from 0 at ring word 0; the lap flag survives 0x01 = 1 until 0x69 bit 1 clears it.
//   7. after the streaming: the normal captures of 0 again (0x69 = 0 hands the capture back).
`timescale 1ns / 1ps

module tb;

  reg i_xtal = 0;
  always #10 i_xtal = ~i_xtal;

  reg       mcu_clk  = 1;
  reg       mcu_rws  = 1;
  reg       mcu_dcs  = 0;
  reg [7:0] mcu_dout = 0;

  reg [7:0] adc1A = 8'd60, adc1B = 8'd60, adc2A = 8'd0, adc2B = 8'd0;

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

  //---------------------------------------------------------------------------------
  // Bookkeeping

  integer errors = 0, checks = 0;
  integer seed;
  integer chunks = 2;                                   // chunks per keep-up tier (+chunks)

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
  // Mirror of the sample memory's write port: data, write index and time of every write; wpass = the write index of
  // word 0 of the capture (the first write at an edge that is not a reset edge after reset edges, as the DUT counts)

  reg [31:0] mir  [0:8191];
  integer    widx [0:8191];
  integer    wcount = 0;                                // writes since the start of the simulation
  integer    wpass = 0;                                 // write index of word 0 of the current capture
  real       wt [0:7];                                  // times of the last 8 writes
  integer    wti = 0;
  reg        rst_seen = 0;
  integer    ii;
  initial for (ii = 0; ii < 8; ii = ii + 1) wt[ii] = -1.0e9;

  always @(posedge dut.sample_write_clock) begin
    if (dut.sample_system_reset) rst_seen <= 1;
    if (dut.sample_write_enable && dut.swc_adv) begin
      mir[dut.sample_write_address_phys]  <= dut.sample_dia;
      widx[dut.sample_write_address_phys] <= wcount;
      wt[wti] <= $realtime;
      wti     <= (wti + 1) & 7;
      wcount  <= wcount + 1;
      if (!dut.sample_system_reset && rst_seen) begin wpass <= wcount; rst_seen <= 0; end
    end
  end

  // the DUT's 0x38 snapshot: the expected counter and W at the copy instant (a write at edge E is counted from
  // E + 12.5 ns, the copy at clock c reads the value before c: writes with E <= c - 17.5 are in)
  integer x_cnt = 0, x_w = 0, late, jj;
  real    x_t = 0.0;
  always @(posedge dut.clk_200MHz)
    if (dut.l_cp) begin
      late = 0;
      for (jj = 0; jj < 8; jj = jj + 1) if (wt[jj] > $realtime - 17.5 + 0.01) late = late + 1;
      x_cnt = wcount - late - wpass;
      if (x_cnt < 0) x_cnt = 0;
      x_w   = x_cnt % 8192;
      x_t   = $realtime;
    end

  // the write never stops while streaming: flag any fall of sample_write_enable after the release
  integer swe_falls = 0;
  reg     watch_swe = 0;
  always @(negedge dut.sample_write_enable) if (watch_swe) swe_falls = swe_falls + 1;

  //---------------------------------------------------------------------------------
  // Stimulus: a triangle on CH1 (60 .. 200, a step every 64 clk_200MHz cycles: 90 us period, crosses 128 twice per
  // period), a sample counter on CH2 (every encode edge), so that consecutive words differ at every rate

  integer tri_on = 1;
  integer tri_cnt = 0, tri_v = 0, tri_dir = 1;
  always @(posedge dut.clk_200MHz) begin
    tri_cnt <= tri_cnt + 1;
    if (tri_on && (tri_cnt & 63) == 0) begin
      if (tri_dir) begin if (tri_v >= 140) tri_dir <= 0; else tri_v <= tri_v + 1; end
      else         begin if (tri_v <= 0)   tri_dir <= 1; else tri_v <= tri_v - 1; end
      adc1A <= 60 + tri_v;  adc1B <= 60 + tri_v;
    end
  end
  always @(posedge e2A) adc2A <= adc2A + 8'd1;
  always @(posedge e2B) adc2B <= adc2B + 8'd3;

  //---------------------------------------------------------------------------------
  // MCU bus (fpga_control.c: data set up, clock low then high); stb = 300 ns is the MCU's strobe, 60 ns a fast bus

  real stb = 60.0;

  task mcu_strobe;
    begin
      #(0.3 * stb + 0.011 * ($random(seed) & 63)) mcu_clk = 0;
      #(0.4 * stb) mcu_clk = 1;
      #(0.3 * stb);
    end
  endtask

  task wr_cmd(input [7:0] c);
    begin mcu_rws = 1; mcu_dcs = 1; mcu_dout = c; mcu_strobe; mcu_dcs = 0; end
  endtask

  task wr_byte(input [7:0] d);
    begin mcu_rws = 1; mcu_dcs = 0; mcu_dout = d; mcu_strobe; end
  endtask

  reg [7:0] rd_d;
  task rd_byte;
    begin mcu_rws = 0; mcu_dcs = 0; mcu_strobe; rd_d = bus; mcu_rws = 1; end
  endtask

  task wr1(input [7:0] c, input [7:0] d);  begin wr_cmd(c); wr_byte(d); end endtask
  task wr2(input [7:0] c, input [15:0] v); begin wr_cmd(c); wr_byte(v[15:8]); wr_byte(v[7:0]); end endtask
  task wr4(input [7:0] c, input [31:0] v);
    begin wr_cmd(c); wr_byte(v[31:24]); wr_byte(v[23:16]); wr_byte(v[15:8]); wr_byte(v[7:0]); end
  endtask

  reg [15:0] rd16;
  task rd2(input [7:0] c);
    begin wr_cmd(c); rd_byte; rd16[15:8] = rd_d; rd_byte; rd16[7:0] = rd_d; end
  endtask
  task rd1(input [7:0] c); begin wr_cmd(c); rd_byte; end endtask

  // read 0x38: { stream, lap, 0, W[12:8] }, W[7:0], counter (4), unread (2)
  reg        r_str, r_lap;
  reg [7:0]  r_b0, r_b6;
  reg [31:0] r_cnt32;
  integer    r_w, r_cnt, r_occ, b;
  task rd_log;
    begin
      wr_cmd(8'h38);
      rd_byte; r_b0 = rd_d; r_str = rd_d[7]; r_lap = rd_d[6]; r_w = rd_d[4:0];
      rd_byte; r_w = (r_w << 8) | rd_d;
      for (b = 0; b < 4; b = b + 1) begin rd_byte; r_cnt32 = { r_cnt32[23:0], rd_d }; end
      r_cnt = r_cnt32;
      rd_byte; r_b6 = rd_d; r_occ = rd_d[5:0];
      rd_byte; r_occ = (r_occ << 8) | rd_d;
      check(r_b0[5] == 1'b0 && r_b6[7:6] == 2'b00, "0x38 reserved bits read 0");
    end
  endtask

  // the reader's bookkeeping: words consumed in this session (the next word to read is rd_cnt mod 8192)
  integer rd_cnt = 0;
  integer x_occ;

  // read 0x38 and check it against the mirror: counter, W, stream bit, unread (when the reader is not lapped)
  task check_log(input exp_str, input chk_occ, input [8*40-1:0] label);
    begin
      rd_log;
      x_occ = x_cnt - rd_cnt;
      if (x_occ > 8192) x_occ = 8192;
      if (r_cnt != x_cnt || r_w != x_w || r_str != exp_str || (chk_occ && r_occ != x_occ))
        $display("  0x38 %0s: str %0d lap %0d W %0d cnt %0d occ %0d; expected str %0d W %0d cnt %0d occ %0d",
                 label, r_str, r_lap, r_w, r_cnt, r_occ, exp_str, x_w, x_cnt, x_occ);
      check(r_cnt == x_cnt, "0x38 counter = words written since word 0 (mirror)");
      check(r_w == x_w, "0x38 W = counter mod 8192 (the address after the last word written)");
      check(r_str == exp_str, "0x38 stream bit as written");
      if (chk_occ) check(r_occ == x_occ, "0x38 unread = counter - consumed (saturating at 8192)");
    end
  endtask

  // fpga_read_lane: 0x1F = start, command, read mode, one dummy strobe, then n strobes; every byte compared with the
  // mirror's lane at the strobe, the write index of every word (lane 0) with the session's expected index
  integer nmis = 0, nidx = 0;
  integer ri, ra;
  task read_lane_chk(input [7:0] cmd, input integer lane, input integer start, input integer n, input integer first_idx);
    begin
      wr2(8'h1F, start & 8191);
      wr_cmd(cmd);
      mcu_rws = 0; mcu_dcs = 0;
      mcu_strobe;
      for (ri = 0; ri < n; ri = ri + 1) begin
        mcu_strobe;
        ra = (start + ri) & 8191;
        if (bus !== mir[ra][8 * lane +: 8]) nmis = nmis + 1;
        if (lane == 0 && widx[ra] !== first_idx + ri) nidx = nidx + 1;
      end
      mcu_rws = 1;
    end
  endtask

  // pc_raw_core: at most ring_words - start words per 0x1F, then again from ring word 0
  integer rn1;
  task read_lane_fw(input [7:0] cmd, input integer lane, input integer start, input integer n, input integer first_idx);
    begin
      rn1 = 8192 - (start & 8191);
      if (rn1 > n) rn1 = n;
      read_lane_chk(cmd, lane, start, rn1, first_idx);
      if (n > rn1) read_lane_chk(cmd, lane, 0, n - rn1, first_idx + rn1);
    end
  endtask

  // the drain of n words from the reader position: the lanes of the format, then 0x6A = the next word to read
  integer li;
  task read_chunk(input integer n, input integer nlanes, input [8*40-1:0] label);
    begin
      nmis = 0; nidx = 0;
      for (li = 0; li < nlanes; li = li + 1)
        read_lane_fw(8'h20 + li, li, rd_cnt & 8191, n, wpass + rd_cnt);
      if (nmis || nidx)
        $display("  chunk %0s: %0d words from %0d: %0d byte mismatches, %0d wrong write indices", label, n, rd_cnt & 8191, nmis, nidx);
      check(nmis == 0, "chunk bytes equal the mirror (the words written)");
      check(nidx == 0, "chunk words are exactly the next words of the session (contiguous, across the ring end)");
      rd_cnt = rd_cnt + n;
      wr2(8'h6A, rd_cnt & 8191);
    end
  endtask

  //---------------------------------------------------------------------------------
  // Filter helpers (pc_decim.c), as tb_timeout.v

  function [5:0] shift_for(input [1:0] k, input [31:0] m);
    reg [127:0] g;
    integer bb, i;
    begin
      g = 255;
      for (i = 0; i < k; i = i + 1) g = g * m;
      bb = 0;
      for (i = 0; i < 128; i = i + 1) if (g[i]) bb = i + 1;
      shift_for = (bb > 16) ? bb - 16 : 0;
    end
  endfunction

  function [5:0] fw_shift(input [1:0] k, input fir, input [31:0] d);
    begin
      fw_shift = (k == 0) ? 6'd0 : (fir ? shift_for(k, (d / 2) + 1) + 1 : shift_for(k, 2 * ((d / 2) + 1)));
    end
  endfunction

  function [15:0] out16(input [7:0] v, input [1:0] k, input fir, input [31:0] d, input [5:0] sh);
    reg [127:0] g;
    integer i;
    begin
      g = v;
      for (i = 0; i < k; i = i + 1) g = g * (fir ? ((d / 2) + 1) : 2 * ((d / 2) + 1));
      g = g >> sh;
      out16 = (g > 65535) ? 16'hFFFF : g[15:0];
    end
  endfunction

  //---------------------------------------------------------------------------------
  // The firmware's sequences

  integer cur_k = 0, cur_fir = 0, cur_d = 0, cur_sh = 0;
  integer word_ns;                                      // output word period

  task configure(input [31:0] d, input [1:0] k, input fir, input [15:0] level16);
    integer sh;
    begin
      if (d != cur_d) wr4(8'h0D, d);
      sh = fw_shift(k, fir, d);
      if (k != cur_k) wr1(8'h53, {6'd0, k});
      if (fir != cur_fir) wr1(8'h56, {7'd0, fir});
      if (k != 0) begin
        wr1(8'h54, sh[5:0]);
        wr2(8'h55, level16);
        if (fir) #((k + 10) * (2 * ((d / 2) + 1)) * 5 + 1000);
        else     #((k + 2) * (2 * ((d / 2) + 1)) * 5);
      end
      cur_d = d; cur_k = k; cur_fir = fir; cur_sh = sh;
      word_ns = (k != 0) ? 10 * ((d / 2) + 1) : 10 * (d + 1);
    end
  endtask

  // pc_hal_arm / fpga_arm_reset (a record): 0x17, 0x0F = 0, 0x1A, 0x0B / 0x0C, 0x01 = 1, the 0x05 wait, 0x0B / 0x0C,
  // 0x0E, 0x01 = 0
  integer polls;
  task arm(input integer pre_w, input integer tot_w, input normal, input [31:0] t_out, input [7:0] level8);
    begin
      wr1(8'h17, level8);
      wr1(8'h0F, 8'h00);
      wr1(8'h1A, normal ? 8'h01 : 8'h00);
      wr2(8'h0B, pre_w); wr2(8'h0C, tot_w);
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05);
      polls = 0;
      rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      wr2(8'h0B, pre_w);
      wr2(8'h0C, tot_w);
      wr4(8'h0E, t_out);
      wr1(8'h01, 8'h00);
    end
  endtask

  // LOG_START (fw-d4): 0x69 = 1, 0x57 = 0, 0x1A, then the reset with 0x0B = 0x0C = 0 and 0x0E = 0 in the window, then
  // 0x38, 0x6A = 0 (the next word to read), 0x69 = 3 (clear the lap flag, stream stays on) when clr
  task arm_stream(input auto_mode, input clr, input [7:0] level8);
    begin
      wr1(8'h69, 8'h01);
      wr1(8'h57, 8'h00);
      wr1(8'h17, level8);
      wr1(8'h1A, auto_mode ? 8'h00 : 8'h01);
      wr2(8'h0B, 0); wr2(8'h0C, 0);
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05);
      polls = 0;
      rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      wr2(8'h0B, 0); wr2(8'h0C, 0);
      wr4(8'h0E, 0);
      wr1(8'h01, 8'h00);
      rd_cnt = 0;
      rd_log;
      wr2(8'h6A, 0);
      if (clr) wr1(8'h69, 8'h03);
      swe_falls = 0;
      watch_swe = 1;
    end
  endtask

  // the record read as the firmware reads it (tb_timeout.v finish_and_read, shortened): equal to the mirror, contiguous
  // in write order, the newest total words (untriggered) or the post-trigger region complete (triggered)
  reg [31:0] y [0:8191];
  reg [7:0]  rb [0:8191];
  integer    done, trig, ta, start, i, nmir, splice, tail_gap, wstop;

  task read_lane_raw(input [7:0] cmd, input integer st, input integer n);
    begin
      wr2(8'h1F, st & 8191);
      wr_cmd(cmd);
      mcu_rws = 0; mcu_dcs = 0;
      mcu_strobe;
      for (ri = 0; ri < n; ri = ri + 1) begin mcu_strobe; rb[ri] = bus; end
      mcu_rws = 1;
    end
  endtask

  task finish_and_read(input integer pre_w, input integer tot_w, input [8*48-1:0] label);
    begin
      polls = 0; done = 0; trig = 0;
      while (!done && polls < 2000000) begin
        rd1(8'h0A); done = rd_d[0];
        rd1(8'h18); trig = rd_d[0];
        polls = polls + 1;
      end
      wr1(8'h0F, 8'h01);
      rd2(8'h14);
      ta = rd16 & 8191;
      start = (ta - pre_w) & 8191;
      wstop = wcount;
      read_lane_raw(8'h20, start, tot_w); for (i = 0; i < tot_w; i = i + 1) y[i][7:0]   = rb[i];
      read_lane_raw(8'h21, start, tot_w); for (i = 0; i < tot_w; i = i + 1) y[i][15:8]  = rb[i];
      read_lane_raw(8'h22, start, tot_w); for (i = 0; i < tot_w; i = i + 1) y[i][23:16] = rb[i];
      read_lane_raw(8'h23, start, tot_w); for (i = 0; i < tot_w; i = i + 1) y[i][31:24] = rb[i];
      nmir = 0; splice = -1;
      for (i = 0; i < tot_w; i = i + 1) begin
        if (y[i] !== mir[(start + i) & 8191]) nmir = nmir + 1;
        if (i > 0 && splice < 0 && widx[(start + i) & 8191] !== widx[(start + i - 1) & 8191] + 1) splice = i;
      end
      tail_gap = wstop - 1 - widx[(start + tot_w - 1) & 8191];
      $display("%0s  K %0d fir %0d D %0d pre %0d total %0d  done %0d trig %0d ta %0d start %0d  W %0d  mismatches %0d  splice at %0d  tail %0d  lap %0d",
               label, cur_k, cur_fir, cur_d, pre_w, tot_w, done, trig, ta, start, wstop & 8191, nmir, splice, tail_gap, dut.lap);
      check(done == 1, "record: capture completed");
      check(nmir == 0, "record equals the words written (readout)");
      check(splice < 0, "record is contiguous in write order (no splice)");
      if (trig) check(tail_gap >= 0 && tail_gap <= 3, "triggered record holds its whole post-trigger region");
      else      check(tail_gap == 0, "untriggered record ends with the last word written (the newest total words)");
    end
  endtask

  //---------------------------------------------------------------------------------
  // Streaming checks

  integer c1, c2, dn, exp_dn;
  real    t1, t2;

  // the sustained rate: two 0x38 snapshots a window apart; the word count between them equals the window / word period
  task measure_rate(input real window_ns, input [8*40-1:0] label);
    begin
      rd_log; c1 = r_cnt; t1 = x_t;
      #(window_ns);
      rd_log; c2 = r_cnt; t2 = x_t;
      dn = c2 - c1;
      exp_dn = $rtoi((t2 - t1) / word_ns + 0.5);
      $display("RATE %0s: %0d words in %0.1f us = %0.1f kwords/s (word %0d ns, expected %0d words)",
               label, dn, (t2 - t1) / 1000.0, dn * 1.0e6 / (t2 - t1), word_ns, exp_dn);
      check(dn >= exp_dn - 1 && dn <= exp_dn + 1, "sustained rate: the words between two snapshots = window / word period");
    end
  endtask

  // the write never stops and nothing triggers: polls of 0x0A / 0x18 over a window, sample_write_enable watched
  integer k, bad_oa, bad_18;
  task never_stops(input integer npolls, input real gap_ns, input [8*40-1:0] label);
    begin
      bad_oa = 0; bad_18 = 0;
      for (k = 0; k < npolls; k = k + 1) begin
        rd1(8'h0A); if (rd_d[0] !== 1'b0) bad_oa = bad_oa + 1;
        rd1(8'h18); if (rd_d[0] !== 1'b0) bad_18 = bad_18 + 1;
        #(gap_ns);
      end
      if (bad_oa || bad_18 || swe_falls)
        $display("  %0s: 0x0A set %0d times, 0x18 set %0d times, sample_write_enable fell %0d times", label, bad_oa, bad_18, swe_falls);
      check(bad_oa == 0, "streaming: 0x0A reads 0 (the write never stops)");
      check(bad_18 == 0, "streaming: 0x18 reads 0 (no trigger)");
      check(swe_falls == 0, "streaming: sample_write_enable never falls");
      check(dut.sample_write_enable === 1'b1, "streaming: the write is on");
    end
  endtask

  // the reader keeping up: chunks of n words at the given lanes
  integer c;
  task keep_up(input integer n, input integer nlanes, input [8*40-1:0] label);
    begin
      for (c = 0; c < chunks; c = c + 1) begin
        // wait until n words are unread (the firmware waits on 0x38 as well)
        rd_log;
        while (x_cnt - rd_cnt < n) begin #(word_ns * 16.0); rd_log; end
        check_log(1, 1, label);
        read_chunk(n, nlanes, label);
      end
      check_log(1, 1, label);
      $display("KEEPUP %0s: %0d chunks of %0d words, %0d lanes, strobe %0.0f ns, consumed %0d of %0d written, unread %0d",
               label, chunks, n, nlanes, stb, rd_cnt, x_cnt, r_occ);
    end
  endtask

  // the lap: the reader stays at rd_cnt; the lap flag is 0 after word rd_cnt + 8191 and 1 after word rd_cnt + 8192
  integer lap_before, occ_before, lap_after;
  task lap_case(input [8*40-1:0] label);
    begin
      wait (wcount >= wpass + rd_cnt + 8192);          // word rd_cnt + 8191 written: 8192 unread, full
      #17;                                             // counted (E + 12.5), the next write not yet (E + 22.5 at the fastest)
      lap_before = dut.lap; occ_before = dut.occ;
      wait (wcount >= wpass + rd_cnt + 8193);          // word rd_cnt + 8192 written: the oldest unread word overwritten
      #17;
      lap_after = dut.lap;
      $display("LAP %0s: after word R + 8191 lap %0d occ %0d; after word R + 8192 lap %0d occ %0d", label, lap_before, occ_before, lap_after, dut.occ);
      check(lap_before == 0 && occ_before == 8192, "lap: 0 with 8192 unread words (full, nothing lost)");
      check(lap_after == 1, "lap: set by the write that overwrites the oldest unread word");
      rd_log;
      check(r_lap == 1 && r_occ == 8192 && r_str == 1, "0x38 after the lap: lap 1, unread 8192, streaming");
      // resync as the firmware does: the reader position = W of a fresh snapshot, consumed = its counter, clear the lap
      rd_log;
      wr2(8'h6A, r_w);
      rd_cnt = r_cnt;
      wr1(8'h69, 8'h03);
      check_log(1, 1, "resync");
      check(r_lap == 0, "0x38 after 0x69 bit 1: lap cleared, stream on");
    end
  endtask

  //---------------------------------------------------------------------------------
  // Cases

  integer lvl16, cross_i;
  integer c_a, c_b, w_a, w_b;

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    if (!$value$plusargs("chunks=%d", chunks)) chunks = 2;
    #2000;
    wr1(8'h01, 8'h00);
    wr1(8'h15, 8'h00);
    wr1(8'h16, 8'h00);
    wr1(8'h57, 8'h00);
    wr1(8'h53, 8'h00);
    wr1(8'h56, 8'h00);
    wr1(8'h54, 8'h00);
    wr4(8'h0D, 0);
    rd2(8'h06);
    $display("0x06 = %04h", rd16);
    check(rd16 == 16'h153B || rd16 == 16'h153C || rd16 == 16'h153D, "0x06 reads 0x153B .. 0x153D");

    //------------------------------------------------------------------------------
    $display("---- 0: normal captures before any 0x69 write (0x153C behaviour): auto time-out, triggered, 1.3-ring time-out");
    configure(31, 3, 1, 16'hFFFF);
    arm(200, 2048, 0, 2048, 8'd255);
    finish_and_read(200, 2048, "0 auto pre 200   ");
    check(trig == 0, "0: untriggered");
    tri_on = 0;
    adc1A = 60; adc1B = 60;
    lvl16 = out16(8'd128, 3, 1, 31, fw_shift(3, 1, 31));
    configure(31, 3, 1, lvl16[15:0]);
    arm(200, 2048, 0, 2048, 8'd128);
    #(1024 * word_ns * 1.0);
    adc1A = 200; adc1B = 200;
    finish_and_read(200, 2048, "0 auto triggered ");
    check(trig == 1, "0: 0x18 reads triggered");
    cross_i = -1;
    for (i = 1; i < 2048; i = i + 1)
      if (cross_i < 0 && y[i - 1][15:0] < lvl16 && y[i][15:0] >= lvl16) cross_i = i;
    check(cross_i >= 199 && cross_i <= 203, "0: the crossing lies at the pretrigger (-1 .. +3 words)");
    adc1A = 60; adc1B = 60;
    tri_on = 1;
    configure(31, 3, 1, 16'hFFFF);
    #(40 * word_ns * 1.0);
    arm(200, 2048, 0, 8192 + 1000 + 200 - 2, 8'd255);  // the ring wraps 1.3 times before the stop
    finish_and_read(200, 2048, "0 long time-out  ");
    check(dut.lap === 1'b0, "0: the lap flag stays 0 in a normal capture that wrapped the ring (0x69 = 0)");
    rd_log;
    check(r_str == 0 && r_lap == 0, "0: 0x38 reads stream 0, lap 0 after a normal capture");
    check(r_w == ((wstop - wpass) & 8191) || r_w == ((wstop - 1 - wpass) & 8191), "0: 0x38 W = the address after the last word written");

    //------------------------------------------------------------------------------
    $display("---- 1: streaming at 200 MSa/s legacy (D 0, 100 Mwords/s): never stops, snapshot, lap at full rate");
    configure(0, 0, 0, 16'h8000);
    arm_stream(1, 1, 8'd128);                          // auto mode with 0x0E = 0: a normal capture would stop at word 2
    #5000;
    check_log(1, 1, "D0 start");
    never_stops(20, 500.0, "D0");
    measure_rate(20000.0, "D0 legacy");
    lap_case("D0 legacy 100 Mwords/s");
    never_stops(5, 200.0, "D0 after lap");
    // a chunk at the fast bus is slower than the writer here (2 lanes x 60 ns per word against 10 ns): no keep-up test

    $display("---- 1b: K 1 D 1 (N 2, 100 Mwords/s 16-bit): never stops, snapshot, lap");
    wr1(8'h69, 8'h00);
    configure(1, 1, 0, 16'd256);                       // level 256 on the 16-bit sum of two codes 60 .. 200
    arm_stream(0, 1, 8'd128);                          // normal mode: the level is crossed all the time, no trigger
    #5000;
    check_log(1, 1, "K1 start");
    never_stops(20, 500.0, "K1 D1");
    measure_rate(20000.0, "K1 D1 16-bit");
    lap_case("K1 D1 100 Mwords/s");

    //------------------------------------------------------------------------------
    $display("---- 2: D 199 legacy (1 MS/s, 500 kwords/s), fast bus: keep-up with 1536-word chunks across the ring end, then the lap");
    wr1(8'h69, 8'h00);
    configure(199, 0, 0, 16'h8000);
    arm_stream(1, 1, 8'd128);
    never_stops(10, 2000.0, "D199");
    measure_rate(200000.0, "D199 legacy");
    keep_up(1536, 2, "D199 legacy 2 lanes");

    $display("---- 2b: D 19 legacy (5 Mwords/s): the lap at a divided rate, the resync, a chunk after it");
    wr1(8'h69, 8'h00);
    configure(19, 0, 0, 16'h8000);
    arm_stream(1, 1, 8'd128);
    never_stops(10, 2000.0, "D19");
    lap_case("D19 5 Mwords/s");
    keep_up(1024, 2, "D19 after resync");

    //------------------------------------------------------------------------------
    $display("---- 3: the contract's rate tiers at the MCU's strobe (300 ns): 1 ch 8-bit 500 kS/s, 1 ch 16-bit 250 kS/s, 2 ch 16-bit 125 kS/s");
    stb = 300.0;
    wr1(8'h69, 8'h00);
    configure(399, 0, 0, 16'h8000);                    // legacy D 399: 4 us per word, 2 samples per word per channel
    arm_stream(1, 1, 8'd128);
    never_stops(5, 4000.0, "D399");
    measure_rate(400000.0, "T1 1 ch 8-bit 500 kS/s (D 399 legacy)");
    keep_up(1024, 2, "T1 D399 legacy 2 lanes");

    wr1(8'h69, 8'h00);
    configure(799, 3, 1, 16'h8000);                    // K 3 + FIR, N 800: 4 us per 16-bit word
    arm_stream(1, 1, 8'd128);
    never_stops(5, 4000.0, "D799");
    measure_rate(400000.0, "T2 1 ch 16-bit 250 kS/s (D 799 K3 FIR)");
    keep_up(512, 2, "T2 D799 K3 FIR 2 lanes");

    wr1(8'h69, 8'h00);
    configure(1599, 3, 1, 16'h8000);                   // K 3 + FIR, N 1600: 8 us per word, both channels
    arm_stream(1, 1, 8'd128);
    never_stops(5, 8000.0, "D1599");
    measure_rate(800000.0, "T3 2 ch 16-bit 125 kS/s (D 1599 K3 FIR)");
    keep_up(256, 4, "T3 D1599 K3 FIR 4 lanes");
    stb = 60.0;

    //------------------------------------------------------------------------------
    $display("---- 4: the empty ring at a slow rate (D 1999 legacy, 20 us per word): 0x6A = W, the next write is no lap");
    wr1(8'h69, 8'h00);
    configure(1999, 0, 0, 16'h8000);
    arm_stream(1, 1, 8'd128);
    #(5 * word_ns * 1.0);
    rd_log;
    wr2(8'h6A, r_w);
    rd_cnt = r_cnt;
    check_log(1, 1, "empty");
    check(r_occ <= 1, "empty ring: unread 0 (or 1 if a word arrived meanwhile)");
    wait (wcount >= wpass + rd_cnt + 1);
    #30;
    check(dut.lap === 1'b0, "empty ring: the write on the reader position is no lap (unread 0 -> 1)");
    check_log(1, 1, "after the write on R");
    check(r_lap == 0 && r_occ >= 1, "0x38 after that write: lap 0, unread >= 1");
    read_chunk(x_cnt - rd_cnt, 2, "D1999 tail");

    //------------------------------------------------------------------------------
    $display("---- 5: stop. Auto mode: 0x69 = 0 stops the write, W / counter freeze, 0x14 = W, the tail drains; normal mode: runs on and triggers");
    wr1(8'h69, 8'h00);
    configure(199, 0, 0, 16'h8000);
    arm_stream(1, 1, 8'd128);
    #(300 * word_ns * 1.0);
    check_log(1, 1, "before stop");
    watch_swe = 0;
    wr1(8'h69, 8'h00);
    #(4 * word_ns * 1.0);
    check(dut.sample_write_enable === 1'b0, "stop (auto): the write stopped within 4 write clocks");
    rd1(8'h0A); check(rd_d[0] === 1'b1, "stop (auto): 0x0A reads 1");
    rd1(8'h18); check(rd_d[0] === 1'b0, "stop (auto): 0x18 reads 0");
    check_log(0, 1, "after stop");
    c_a = r_cnt; w_a = r_w;
    #(50 * word_ns * 1.0);
    check_log(0, 1, "after stop + 50 words");
    check(r_cnt == c_a && r_w == w_a, "stop (auto): W and the counter are frozen");
    rd2(8'h14);
    check((rd16 & 8191) == w_a, "stop (auto): 0x14 = W (0x0B = 0x0C = 0: the 0x153B time-out address)");
    read_chunk(c_a - rd_cnt, 2, "tail after stop");
    check(rd_cnt == c_a, "stop (auto): the tail drained to the last word written");
    check((wcount - wpass) == c_a, "stop (auto): no word was written after the counter froze");

    wr1(8'h69, 8'h00);
    configure(199, 0, 0, 16'h8000);
    arm_stream(0, 1, 8'd128);                          // normal mode, level 128 crossed by the triangle every 45 us
    never_stops(10, 20000.0, "D199 normal mode");      // 200 us: several crossings, none triggers
    watch_swe = 0;
    wr1(8'h69, 8'h00);
    #100000;                                           // a crossing comes within 45 us
    rd1(8'h18); check(rd_d[0] === 1'b1, "stop (normal mode): the capture triggers once 0x69 = 0");
    rd1(8'h0A); check(rd_d[0] === 1'b1, "stop (normal mode): and stops (post-trigger words 0)");
    check_log(0, 0, "normal mode stopped");

    //------------------------------------------------------------------------------
    $display("---- 6: restart: a new arm counts from 0 at ring word 0; the lap flag survives 0x01 = 1 until 0x69 bit 1");
    configure(0, 0, 0, 16'h8000);
    arm_stream(1, 1, 8'd128);
    lap_case("restart prelude");
    wait (wcount >= wpass + rd_cnt + 8193);            // lap again, left set
    #30;
    check(dut.lap === 1'b1, "restart: lap set again");
    watch_swe = 0;
    wr1(8'h69, 8'h00);
    configure(199, 0, 0, 16'h8000);
    arm_stream(1, 0, 8'd128);                          // no clear
    #(20 * word_ns * 1.0);
    check_log(1, 1, "restart");
    check(r_cnt >= 15 && r_cnt <= 30, "restart: the counter restarted at 0 (about 20 words after the release)");
    check(r_lap == 1, "restart: the lap flag survives 0x01 = 1 (only 0x69 bit 1 clears it)");
    wr1(8'h69, 8'h03);
    check_log(1, 1, "restart cleared");
    check(r_lap == 0, "restart: 0x69 bit 1 clears the lap flag with the stream on");
    read_chunk(x_cnt - rd_cnt, 2, "restart chunk");
    check_log(1, 1, "restart after chunk");

    //------------------------------------------------------------------------------
    $display("---- 7: normal captures after the streaming (0x69 = 0)");
    watch_swe = 0;
    wr1(8'h69, 8'h00);
    configure(31, 3, 1, 16'hFFFF);
    arm(200, 2048, 0, 2048, 8'd255);
    finish_and_read(200, 2048, "7 auto pre 200   ");
    check(trig == 0, "7: untriggered");
    tri_on = 0;
    adc1A = 60; adc1B = 60;
    configure(31, 3, 1, lvl16[15:0]);
    arm(200, 2048, 1, 2048, 8'd128);
    #(1024 * word_ns * 1.0);
    adc1A = 200; adc1B = 200;
    finish_and_read(200, 2048, "7 normal trig    ");
    check(trig == 1, "7: 0x18 reads triggered");
    cross_i = -1;
    for (i = 1; i < 2048; i = i + 1)
      if (cross_i < 0 && y[i - 1][15:0] < lvl16 && y[i][15:0] >= lvl16) cross_i = i;
    check(cross_i >= 199 && cross_i <= 203, "7: the crossing lies at the pretrigger (-1 .. +3 words)");
    adc1A = 60; adc1B = 60;
    tri_on = 1;

    $display("RESULT: %0d checks, %0d errors -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #400000000;
    $display("TIMEOUT");
    $display("RESULT: %0d checks, %0d errors -> FAIL", checks, errors + 1);
    $finish;
  end

endmodule
