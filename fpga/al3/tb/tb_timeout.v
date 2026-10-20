// Records that end on the auto time-out or on the firmware's forced stop (fpga/al3/src/zaklad.v, 0x153B; fpga-gen11).
// Run with fpga/al3/tb/run.sh (ONLY=timeout) or directly with vvp (+seed, +total, +runs).
//
// Bench (docs/hardware-status.md 2026-10-20 12:32, backups/bode/cells_1235.npz): a record whose capture the firmware
// ended with 0x1A = 0 after its auto deadline (the firmware's auto mode since fw 0.2.41: arm normal, force at the
// deadline) is spliced from two ring passes in about a third of the cases: the words after a random position are one
// ring (8192 words) older than the words before it. On 0x1532..0x153A the time-out stop latches trigger_address =
// pretrigger, which the firmware turns into record start 0, while the write pointer has gone round the ring for
// milliseconds and stopped at a random word W: the ring words 0 .. W - 1 are from the last pass, W .. total - 1 from the
// pass before. The same convention cuts a triggered record in FPGA auto mode when the time-out (0x0E = total, the Bode
// engine) comes before the post-trigger words are complete.
//
// This testbench mirrors every word the capture logic writes into the sample memory (address, data and the write
// order, taken from the DUT's write port) and, after each capture, reads the record exactly as the firmware does
// (0x14, start = trigger_address - pretrigger words, fpga_read_start with its dummy strobe, 16-bit or legacy lanes) and
// checks that (1) the words read equal the mirror, (2) the record is contiguous in write order and (3) it ends with
// the last word written (the newest total words). Cases, each with the firmware's exact byte sequences:
//   F1  the firmware's auto mode (0x1A = 1 at the arm, 0x0E = total, 0x1A = 0 after 1.1 .. 2.6 ring passes) with
//       CIC3+FIR at divider 31 (N 32), pretrigger 0, then pretrigger 200; `runs` forces per case at write positions
//       spread over the ring
//   F2  the same in the legacy 8-bit layout at divider 9 (10 MSa/s), pretrigger 100 words
//   A1  FPGA auto mode with 0x0E = total (bh_arm, 1.00o5 auto): no trigger; pretrigger 0 and 200
//   A2  FPGA auto, 0x0E = total, a CH1 step that crosses the trigger level at about word total / 2: the record must
//       hold the whole post-trigger region (0x153A cut it at the time-out), 0x18 = 1, the crossing at the pretrigger
//   A3  FPGA auto, 0x0E = total, the CH1 step 500 words after the stop: 0x18 must stay 0 (0x153A reads 1)
//   A4  FPGA auto with a time-out of about 1.3 rings whose stop lands at ring word 1000 + pretrigger (0x153A: start 0,
//       spliced at 1000), no trigger
//   B   (fpga-gen13) the host capture of the bench of 2026-10-20 19:52 on 0x153B, byte for byte: fw 0.2.102's host ARM at
//       the UI's 1 MSa/s index (divider 199, CIC3+FIR, N 200, 16-bit words), pretrigger 3000, total 6000, 0x0E = total,
//       normal arm; B1 untriggered (level 255 = 0xFFFF), the firmware's force 0x1A = 0 after 1.37 rings so that W lands at
//       ring word about 3000 (inside the record's ring words: the 0x153A splice position); B2 triggered: a CH1 step across
//       level 128 after 3500 words, no force. The record is read as READ_RAW reads it (pc_raw_core / fpga_read_lane: the
//       read restarts at ring word 0 with a new 0x1F, command and dummy strobe when it reaches the ring end). +bench=0 skips
//       it (about 20 ms of simulated time).
// PASS on 0x153B: every record equals the mirror, is contiguous and ends at the stop; A2 and B2 triggered at the crossing,
// A3 untriggered. Against the 0x153A source (DUT= the a2b6929 file) the F1 / F2 forces whose W lands inside the record,
// A2, A3, A4 and B1 fail: that is the bench fault reproduced.
`timescale 1ns / 1ps

module tb;

  reg i_xtal = 0;
  always #10 i_xtal = ~i_xtal;

  reg       mcu_clk  = 1;
  reg       mcu_rws  = 1;
  reg       mcu_dcs  = 0;
  reg [7:0] mcu_dout = 0;

  reg [7:0] adc1A = 8'd60, adc1B = 8'd60, adc2A = 8'd90, adc2B = 8'd90;

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
  integer total = 2048;                                 // record words per capture (+total)
  integer runs = 4;                                     // forces per F case (+runs)

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
  // Mirror of the sample memory's write port: data, and the write order of every address

  reg [31:0] mir  [0:8191];
  integer    widx [0:8191];
  integer    wcount = 0;                                // words written since the last mirror reset
  integer    wpass = 0;                                 // write index at the last arm (the capture's first word)

  always @(posedge dut.sample_write_clock)
    if (dut.sample_write_enable && dut.swc_adv) begin
      mir[dut.sample_write_address_phys]  <= dut.sample_dia;
      widx[dut.sample_write_address_phys] <= wcount;
      wcount <= wcount + 1;
    end

  //---------------------------------------------------------------------------------
  // Slow input: a triangle on both channels (CH1 60 .. 200, CH2 90 .. 230) advancing every 64 ADC samples, so that a
  // dump of a spliced record shows the jump; the trigger cases replace it with a step

  integer tri_on = 1;
  integer tri_cnt = 0, tri_v = 0, tri_dir = 1;
  always @(posedge dut.clk_200MHz) begin
    tri_cnt <= tri_cnt + 1;
    if (tri_on && (tri_cnt & 63) == 0) begin
      if (tri_dir) begin if (tri_v >= 140) tri_dir <= 0; else tri_v <= tri_v + 1; end
      else         begin if (tri_v <= 0)   tri_dir <= 1; else tri_v <= tri_v - 1; end
      adc1A <= 60 + tri_v;  adc1B <= 60 + tri_v;
      adc2A <= 90 + tri_v;  adc2B <= 90 + tri_v;
    end
  end

  //---------------------------------------------------------------------------------
  // MCU bus (fpga_control.c: data set up, clock low then high)

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

  // fpga_read_start (0x1F = start, command, read mode, one dummy strobe) then n strobes; the 13-bit pointer wraps
  reg [7:0] rb [0:8191];
  integer   ri;
  task read_lane(input [7:0] cmd, input integer start, input integer n);
    begin
      wr2(8'h1F, start & 8191);
      wr_cmd(cmd);
      mcu_rws = 0; mcu_dcs = 0;
      mcu_strobe;
      for (ri = 0; ri < n; ri = ri + 1) begin mcu_strobe; rb[ri] = bus; end
      mcu_rws = 1;
    end
  endtask

  // pc_raw_core / fpga_read_lane (fpga_bus.c): the firmware reads at most ring_words - start words per 0x1F, then
  // starts again at ring word 0 (a second 0x1F, command and dummy strobe) instead of relying on the 13-bit pointer wrap
  reg [7:0] rb2 [0:8191];
  integer   rj, rn1;
  task read_lane_fw(input [7:0] cmd, input integer start, input integer n);
    begin
      rn1 = 8192 - (start & 8191);
      if (rn1 > n) rn1 = n;
      read_lane(cmd, start, rn1);
      if (n > rn1) begin
        for (rj = 0; rj < rn1; rj = rj + 1) rb2[rj] = rb[rj];
        read_lane(cmd, 0, n - rn1);
        for (rj = 0; rj < n - rn1; rj = rj + 1) rb2[rn1 + rj] = rb[rj];
        for (rj = 0; rj < n; rj = rj + 1) rb[rj] = rb2[rj];
      end
    end
  endtask

  //---------------------------------------------------------------------------------
  // Filter helpers (pc_decim.c), as tb_firstrec.v

  function [5:0] shift_for(input [1:0] k, input [31:0] m);
    reg [127:0] g;
    integer b, i;
    begin
      g = 255;
      for (i = 0; i < k; i = i + 1) g = g * m;
      b = 0;
      for (i = 0; i < 128; i = i + 1) if (g[i]) b = i + 1;
      shift_for = (b > 16) ? b - 16 : 0;
    end
  endfunction

  function [5:0] fw_shift(input [1:0] k, input fir, input [31:0] d);
    begin
      fw_shift = (k == 0) ? 6'd0 : (fir ? shift_for(k, (d / 2) + 1) + 1 : shift_for(k, 2 * ((d / 2) + 1)));
    end
  endfunction

  // 16-bit value of a constant input v (the firmware's trigger level: round(v G / 2^S), here v G >> S)
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

  // pc_hal_timebase_write raw, then pc_dec_apply with the settle wait (fw-bus / fpga_decim_write)
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
      // output word period: legacy 10 (D + 1) ns; K >= 1 runs the write clock at 100 MHz and N = 2 (D / 2 + 1)
      word_ns = (k != 0) ? 10 * ((d / 2) + 1) : 10 * (d + 1);
    end
  endtask

  // pc_hal_arm / fpga_arm_reset (fw-bus order): 0x17, 0x0F = 0, 0x1A, 0x0B / 0x0C, 0x01 = 1, the 0x05 wait, 0x0B / 0x0C,
  // 0x0E, 0x01 = 0. normal = 1 is the firmware's auto mode (and the host's normal / single); the firmware writes 0x0E =
  // total samples in both modes (pc_hal_auto_timeout_default).
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
      wpass = wcount;                                   // the mirror index of this capture's first word
      wr1(8'h01, 8'h00);
    end
  endtask

  // pc_hal_acq_poll until done (bounded), pc_hal_acq_finish, the record read as the firmware reads it
  reg [31:0] y [0:8191];
  integer    done, trig, ta, start, i, nmir, splice, head_gap, tail_gap, wstop;

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
      wstop = wcount;                                   // the mirror index after the last word written
      // the four lanes as READ_RAW reads them (16-bit: 0x20 / 0x21 are the low / high byte of CH1's sample, 0x22 / 0x23 CH2's)
      read_lane_fw(8'h20, start, tot_w); for (i = 0; i < tot_w; i = i + 1) y[i][7:0]   = rb[i];
      read_lane_fw(8'h21, start, tot_w); for (i = 0; i < tot_w; i = i + 1) y[i][15:8]  = rb[i];
      read_lane_fw(8'h22, start, tot_w); for (i = 0; i < tot_w; i = i + 1) y[i][23:16] = rb[i];
      read_lane_fw(8'h23, start, tot_w); for (i = 0; i < tot_w; i = i + 1) y[i][31:24] = rb[i];
      nmir = 0; splice = -1;
      for (i = 0; i < tot_w; i = i + 1) begin
        if (y[i] !== mir[(start + i) & 8191]) nmir = nmir + 1;
        if (i > 0 && splice < 0 && widx[(start + i) & 8191] !== widx[(start + i - 1) & 8191] + 1) splice = i;
      end
      head_gap = widx[start & 8191] - wpass;                        // record start, words after the capture's first word
      tail_gap = wstop - 1 - widx[(start + tot_w - 1) & 8191];      // words written after the record's last word
      $display("%0s  K %0d fir %0d D %0d pre %0d total %0d  done %0d trig %0d ta %0d start %0d  written %0d (W %0d)  mirror mismatches %0d  splice at %0d  head %0d tail %0d  w0 %08h w[last] %08h",
               label, cur_k, cur_fir, cur_d, pre_w, tot_w, done, trig, ta, start, wstop - wpass, wstop & 8191, nmir, splice,
               head_gap, tail_gap, y[0], y[tot_w - 1]);
      check(done == 1, "capture completed");
      check(nmir == 0, "record equals the words written (readout)");
      check(splice < 0, "record is contiguous in write order (no splice)");
      // untriggered: the newest total words; triggered: the post-trigger region complete, the stop 0 .. 3 words after it
      if (trig) check(tail_gap >= 0 && tail_gap <= 3, "triggered record holds its whole post-trigger region (not cut)");
      else      check(tail_gap == 0, "untriggered record ends with the last word written (the newest total words)");
    end
  endtask

  //---------------------------------------------------------------------------------
  // Cases

  integer r, pre, wait_words, target, cross_i, lvl16;
  integer run_no = 0;
  integer bench = 1;                                    // +bench=0 skips case B
  real    t_force;

  // the firmware's auto mode: normal arm, then 0x1A = 0 after wait_words output words (>= 1.1 rings)
  task force_case(input [8*48-1:0] label, input integer pre_w, input integer tot_w, input integer wait_w);
    begin
      arm(pre_w, tot_w, 1, tot_w, 8'd255);
      #(wait_w * word_ns * 1.0);
      wr1(8'h1A, 8'h00);                                // pc_hal_force / pc_ui_auto_service
      finish_and_read(pre_w, tot_w, label);
    end
  endtask

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    if (!$value$plusargs("total=%d", total)) total = 2048;
    if (!$value$plusargs("runs=%d", runs)) runs = 4;
    if (!$value$plusargs("bench=%d", bench)) bench = 1;
    #2000;
    wr1(8'h01, 8'h00);
    wr1(8'h15, 8'h00);
    wr1(8'h16, 8'h00);
    wr1(8'h57, 8'h00);
    wr1(8'h53, 8'h00);
    wr1(8'h56, 8'h00);
    wr1(8'h54, 8'h00);
    wr4(8'h0D, 0);

    $display("---- F1: firmware auto mode (normal arm, 0x1A = 0 at the deadline), CIC3+FIR D 31, total %0d", total);
    configure(31, 3, 1, 16'hFFFF);
    for (pre = 0; pre <= 200; pre = pre + 200)
      for (r = 0; r < runs; r = r + 1) begin
        // forces whose stop lands at ring word total / 4, total / 2 + 7, 3 total / 4 (inside the record: the 0x153A
        // splice) and total + 500 (outside), in the second ring pass; W = wait + about 3 words of latency
        wait_words = 8192 + ((r & 3) == 3 ? total + 500 : (r & 3) * (total / 4) + total / 4 + ((r & 3) == 1 ? 7 : 0)) - 3;
        force_case(pre ? "F1 force pre 200" : "F1 force pre 0  ", pre, total, wait_words);
      end

    $display("---- F2: the same in the legacy layout at D 9, pretrigger 100 words");
    configure(9, 0, 0, 16'h8000);
    for (r = 0; r < runs; r = r + 1) begin
      wait_words = 8192 + ((r & 3) == 3 ? total / 2 + 500 : (r & 3) * (total / 8) + total / 8 + ((r & 3) == 1 ? 7 : 0)) - 3;
      force_case("F2 force legacy ", 100, total / 2, wait_words);
    end

    $display("---- A1: FPGA auto mode, 0x0E = total, no trigger");
    configure(31, 3, 1, 16'hFFFF);
    arm(0, total, 0, total, 8'd255);
    finish_and_read(0, total, "A1 auto pre 0   ");
    arm(200, total, 0, total, 8'd255);
    finish_and_read(200, total, "A1 auto pre 200 ");

    $display("---- A2: FPGA auto mode, 0x0E = total, CH1 step across the level at about word total / 2");
    tri_on = 0;
    adc1A = 60; adc1B = 60;
    lvl16 = out16(8'd128, 3, 1, 31, fw_shift(3, 1, 31));
    configure(31, 3, 1, lvl16[15:0]);
    arm(200, total, 0, total, 8'd128);
    #((total / 2) * word_ns * 1.0);
    adc1A = 200; adc1B = 200;
    finish_and_read(200, total, "A2 auto trig    ");
    check(trig == 1, "A2: 0x18 reads triggered");
    cross_i = -1;
    for (i = 1; i < total; i = i + 1)
      if (cross_i < 0 && y[i - 1][15:0] < lvl16 && y[i][15:0] >= lvl16) cross_i = i;
    $display("A2: level16 %0d, crossing at record word %0d (pretrigger 200)", lvl16, cross_i);
    check(cross_i >= 199 && cross_i <= 203, "A2: the crossing lies at the pretrigger (-1 .. +3 words)");

    $display("---- A3: FPGA auto mode, 0x0E = total, the CH1 step 500 words after the time-out stop");
    adc1A = 60; adc1B = 60;
    #(40 * word_ns * 1.0);
    arm(200, total, 0, total, 8'd128);
    #((total + 500) * word_ns * 1.0);
    adc1A = 200; adc1B = 200;
    #(200 * word_ns * 1.0);
    finish_and_read(200, total, "A3 late edge    ");
    check(trig == 0, "A3: 0x18 stays 0 for an edge after the stop");
    adc1A = 60; adc1B = 60;
    tri_on = 1;

    $display("---- A4: FPGA auto mode with a 1.3-ring time-out whose stop lands at ring word 1000 + pretrigger");
    configure(31, 3, 1, 16'hFFFF);
    #(40 * word_ns * 1.0);
    for (pre = 0; pre <= 200; pre = pre + 200) begin
      arm(pre, total, 0, 8192 + 1000 + pre - 2, 8'd255);  // W = set + 2 = 8192 + 1000 + pre
      finish_and_read(pre, total, pre ? "A4 long pre 200 " : "A4 long pre 0   ");
    end

    if (bench) begin
      $display("---- B: the host capture of the bench 2026-10-20 19:52 (fw 0.2.102 on 0x153B): D 199 CIC3+FIR N 200, pretrigger 3000, total 6000, 16-bit");
      // B1: untriggered, the firmware's auto mode on 0x153B+ (pc_cmds.c h_arm: arm_normal 1, 0x0E = total samples,
      // pc_hal_force at the deadline); the force after 1.37 rings puts W at ring word about 3000, inside the record's
      // ring words 0 .. 5999 on 0x153A; the record then wraps through the ring end, so the firmware's split read is exercised
      configure(199, 3, 1, 16'hFFFF);
      force_case("B1 bench untrig ", 3000, 6000, 8192 + 3000 - 3);
      // B2: triggered, the same arm with level 128: a CH1 step 500 words after the pretrigger has filled, no force
      tri_on = 0;
      adc1A = 60; adc1B = 60;
      lvl16 = out16(8'd128, 3, 1, 199, fw_shift(3, 1, 199));
      configure(199, 3, 1, lvl16[15:0]);
      #(40 * word_ns * 1.0);
      arm(3000, 6000, 1, 6000, 8'd128);
      #(3500 * word_ns * 1.0);
      adc1A = 200; adc1B = 200;
      finish_and_read(3000, 6000, "B2 bench trig   ");
      check(trig == 1, "B2: 0x18 reads triggered");
      cross_i = -1;
      for (i = 1; i < 6000; i = i + 1)
        if (cross_i < 0 && y[i - 1][15:0] < lvl16 && y[i][15:0] >= lvl16) cross_i = i;
      $display("B2: level16 %0d, crossing at record word %0d (pretrigger 3000)", lvl16, cross_i);
      check(cross_i >= 2999 && cross_i <= 3003, "B2: the crossing lies at the pretrigger (-1 .. +3 words)");
      adc1A = 60; adc1B = 60;
      tri_on = 1;
    end

    $display("RESULT: %0d checks, %0d errors -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #2000000000;
    $display("TIMEOUT");
    $display("RESULT: %0d checks, %0d errors -> FAIL", checks, errors + 1);
    $finish;
  end

endmodule
