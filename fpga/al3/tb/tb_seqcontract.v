// Contract testbench: the firmware's 0x1536 / 0x1537 sequencer byte sequences and the 0x153C register sequences against
// the simulated FPGA. Run with fpga/al3/tb/run.sh (ONLY=contract) or vvp +transcript=<file>.
//
// The MCU side replays, strobe by strobe, the sequences of fw-seg's protocol 1.17 firmware (fw 0.2.56 branch,
// commit 65abebf: pc_hal_arm, pc_seq_hw.c, h_read_seq, h_read_raw; listed in fpga/al3/README.md section 6,
// "Firmware contract"). Every strobe goes into the transcript:
//   C xx    command byte written (i_mcu_dcs = 1)
//   W xx    data byte written
//   R xx    data byte read (the value on the bus after the strobe)
//   D xx    the dummy strobe after a pointer write (its byte is discarded)
//   # text  the step of the sequence; "# poll" lines precede polling loops, whose length depends on timing
// The firmware's host tests can log their HAL calls in the same format and diff them against this file; the R
// lines are the bytes a correct FPGA returns for the stimulus below (deterministic for a given seed).
//
// Scenarios:
//   S  legacy segments: 20 MSa/s (D = 9), CH1 rising, 4 segments of 2^6 words, pre 10, total 40, hold-off 0
//   SR READ_RAW addr_mode 3 of segment 2 (entry read, slot start, wrap split) for both CH1 cores
//   A  legacy averaging: 10 MSa/s (D = 19), EXT, 1 position, N = 4, pre 8, total 32; read at S = 2 and raw
//   F  16-bit averaging: CIC2 + FIR at N = 20 (D = 19), CH1, 2 positions, N = 8, pre 6, total 24; read at S = 3
//   P  circular segments stopped with 0x5D = 1 then 0x1A = 0
//   T  (0x153C C1, fw-c5) SET_TRIGGER2 pulse width longer than N = 5 words on CH1 at D = 9 with a hold-off, a host
//      capture: three 3-word pulses do not trigger, the trailing edge of a 10-word pulse does; the 0x37 / 0x34 status
//   PK (0x153C C2, fw-c6) peak detect: 0x53 = 1, 0x56 = 2, 0x54 = 0, 0x55 = 128 << 8 at N = 20, CH1 rising; the 0x29
//      read-back and READ_RAW layout 3 (min bytes through 0x20, max bytes through 0x21)
//   K  (0x153C C3, fw-c7) the counter: 0x62 / 0x61 (gate 10 ms on the trigger source), a 100 kHz square, the 0x35 snapshot
//   L  (0x153D D1, fw-d4 fpga_log.c) the streaming logger: LOG_START CH1 format 0 at D = 399 (the 500 kS/s tier), two
//      drain steps (the lanes with fpga_read_lane, the second 0x38, 0x6A), LOG_STOP with the tail; the 0x38 snapshots
//      against the memory, W = count mod 8192, no lap, the write stopped and 0x14 = W after the stop
// 0x153C: the boot writes fpga_trig_write(edge, hold-off 0, clear) and the counter's 0x62 / 0x61 (gate 100 ms) after the
// version read, and ARM 7 carries the trigger type registers (0x5E always; 0x5F, 0x60 for a qualified type) as
// fpga_trig_arm does, before 0x0E. The firmware still writes 0x0F in every arm (a no-op since 0x153C).
// 0x153D: the generator is the pattern (fw-d5): "generator off" at the setup is fpga_pat_drive(0) (0x50 with four data
// bytes, as 1.00o5 writes it) and fpga_pat_level(0) (0x6D = 0); the boot adds fpga_ptrig_write(zeros) (0x7D = 0, 0x76 ..
// 0x7A, 0x7D = 04: the pattern trigger engine disarmed) between the trigger write and the counter's.
// Checks against independent expectations: status bytes, table entries (valid, triggered, offset = 0x14 mod 2^m
// for the last segment, the step at record index pre + 1 in every slot), averaged lanes equal to N x the
// single-record value, raw lanes equal to the sums, the stop.
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

  reg  sig = 0;
  reg  ext_en = 0;
  wire pad;
  assign pad = ext_en ? sig : 1'bz;
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

  integer errors = 0, checks = 0, seed, tf;

  task check(input cond, input [8*120-1:0] what);
    begin
      checks = checks + 1;
      if (!cond) begin errors = errors + 1; $display("FAIL  t=%0t  %0s", $time, what); end
    end
  endtask

  task note(input [8*200-1:0] s);                 // 0x153C: 200 characters (longer scenario lines)
    begin $fdisplay(tf, "# %0s", s); end
  endtask

  //---------------------------------------------------------------------------------
  // ADC: AD9288-like, 4 encode cycles pipeline (as tb_trigpos.v), CH1 = step LO/HI, CH2 its complement

  localparam [7:0] LO = 8'd40, HI = 8'd210;
  reg [7:0] pA [0:7];
  reg [7:0] pB [0:7];
  integer ia, ib;
  initial for (ia = 0; ia < 8; ia = ia + 1) begin pA[ia] = LO; pB[ia] = LO; end
  always @(posedge e1A) begin
    for (ia = 7; ia > 0; ia = ia - 1) pA[ia] = pA[ia - 1];
    pA[0] = sig ? HI : LO;
    adc1A <= #3.3 pA[4];
    adc2A <= #3.3 8'd255 - pA[4];
  end
  always @(posedge e1B) begin
    for (ib = 7; ib > 0; ib = ib - 1) pB[ib] = pB[ib - 1];
    pB[0] = sig ? HI : LO;
    adc1B <= #3.3 pB[4];
    adc2B <= #3.3 8'd255 - pB[4];
  end

  //---------------------------------------------------------------------------------
  // MCU bus with the transcript

  real stb = 97.1;
  task mcu_strobe;
    begin #(0.3 * stb) mcu_clk = 0; #(0.4 * stb) mcu_clk = 1; #(0.3 * stb); end
  endtask

  integer quiet = 0;                                     // 1: poll loops are not transcribed line by line
  task wr_cmd(input [7:0] c);
    begin
      mcu_rws = 1; mcu_dcs = 1; mcu_dout = c; mcu_strobe; mcu_dcs = 0;
      if (!quiet) $fdisplay(tf, "C %h", c);
    end
  endtask
  task wr_byte(input [7:0] d);
    begin
      mcu_rws = 1; mcu_dcs = 0; mcu_dout = d; mcu_strobe;
      if (!quiet) $fdisplay(tf, "W %h", d);
    end
  endtask
  reg [7:0] rd_d;
  task rd_byte;
    begin
      mcu_rws = 0; mcu_dcs = 0; mcu_strobe; rd_d = bus; mcu_rws = 1;
      if (!quiet) $fdisplay(tf, "R %h", rd_d);
    end
  endtask
  task dummy;
    begin
      mcu_rws = 0; mcu_dcs = 0; mcu_strobe; mcu_rws = 1;
      $fdisplay(tf, "D %h", bus);
    end
  endtask
  task wr1(input [7:0] c, input [7:0] d);  begin wr_cmd(c); wr_byte(d); end endtask
  task wr2(input [7:0] c, input [15:0] v); begin wr_cmd(c); wr_byte(v[15:8]); wr_byte(v[7:0]); end endtask
  task wr4(input [7:0] c, input [31:0] v);
    begin wr_cmd(c); wr_byte(v[31:24]); wr_byte(v[23:16]); wr_byte(v[15:8]); wr_byte(v[7:0]); end
  endtask

  // pass: 0x1F = first, command, dummy strobe, n reads into pb
  reg [7:0] pb [0:4095];
  integer   pi;
  task pass(input [7:0] cmd, input integer first, input integer n);
    begin
      wr2(8'h1F, first);
      wr_cmd(cmd);
      dummy;
      for (pi = 0; pi < n; pi = pi + 1) begin rd_byte; pb[pi] = rd_d; end
    end
  endtask

  //---------------------------------------------------------------------------------
  // Firmware sequences (fw-seg, protocol 1.17)

  reg [7:0] st [0:7], st2 [0:7];
  integer   polls, i, j, b, agree, tries;

  // 0x153C (fw-c5, fpga_bus.c fpga_trig_arm / fpga_trig_write): the trigger type registers inside the arm window, and the
  // SET_TRIGGER2 / boot write with the hold-off and the one-shot clear
  reg [7:0]  t_type = 0;                                 // the 0x5E value: bits 2..0 type, bit 3 longer
  reg [23:0] t_n = 0, t_h = 0;                           // 0x5F, 0x60
  task fw_trig_arm;
    begin
      wr1(8'h5E, t_type);
      if (t_type[2:0] != 3'd0) begin
        wr_cmd(8'h5F); wr_byte(t_n[23:16]); wr_byte(t_n[15:8]); wr_byte(t_n[7:0]);
        wr_cmd(8'h60); wr_byte(t_h[23:16]); wr_byte(t_h[15:8]); wr_byte(t_h[7:0]);
      end
    end
  endtask
  task fw_trig_write(input [7:0] typ, input [23:0] n, input [23:0] h, input [31:0] holdoff, input clear);
    begin
      t_type = typ; t_n = n; t_h = h;
      fw_trig_arm;
      wr4(8'h1B, holdoff);
      if (clear) wr1(8'h66, 8'h01);
    end
  endtask
  reg [7:0] ts37;
  reg [7:0] ts34 [0:2];
  task fw_trig_status;
    begin
      wr_cmd(8'h37); rd_byte; ts37 = rd_d;
      wr_cmd(8'h34); rd_byte; ts34[0] = rd_d; rd_byte; ts34[1] = rd_d; rd_byte; ts34[2] = rd_d;
    end
  endtask

  // 0x153C (fw-c7, fpga_counter.c): 0x62 level / hysteresis, then 0x61 gate / source (a value change restarts the gate);
  // the main-loop pass reads 0x35, 16 bytes from one snapshot
  reg [7:0] cnt [0:15];
  task fw_counter_config(input [7:0] level, input [7:0] hyst, input [1:0] gate, input [1:0] src);
    begin
      wr_cmd(8'h62); wr_byte(level); wr_byte(hyst);
      wr1(8'h61, {4'd0, src, gate});
    end
  endtask
  task fw_counter_read;
    begin
      wr_cmd(8'h35);
      for (i = 0; i < 16; i = i + 1) begin rd_byte; cnt[i] = rd_d; end
    end
  endtask

  // 0x153D (fw-d5, fpga_bus.c fpga_ptrig_write): 0x7D = 0, the edge registers 0x76 .. 0x79, the type registers 0x7A (0x7B,
  // 0x7C for a qualified type), 0x7D = armed | rearm << 1 | 0x04 (clear the event flag) | route << 3
  task fw_ptrig_write(input [7:0] src, input [7:0] pol, input [7:0] lvl, input [15:0] lvl16, input [7:0] typ,
                      input [23:0] n, input [23:0] h, input armed, input rearm);
    begin
      wr1(8'h7D, 8'h00);
      wr1(8'h76, src); wr1(8'h77, pol); wr1(8'h78, lvl); wr2(8'h79, lvl16);
      wr1(8'h7A, typ);
      if (typ[2:0] != 3'd0) begin
        wr_cmd(8'h7B); wr_byte(n[23:16]); wr_byte(n[15:8]); wr_byte(n[7:0]);
        wr_cmd(8'h7C); wr_byte(h[23:16]); wr_byte(h[15:8]); wr_byte(h[7:0]);
      end
      wr1(8'h7D, {5'd0, 1'b1, rearm, armed});
    end
  endtask

  // 0x153D (fw-d4, fpga_log.c): fw_log_snapshot = fpga_log_snapshot (0x38, 8 bytes); fw_log_start = pc_hal_log_start (0x0D,
  // pc_dec_apply's filter writes) + fpga_log_start (0x69 = 1, 0x57 = 0, 0x1A = 0, fpga_arm_reset with pre = total = 0 and
  // 0x0E = 0, the snapshot, 0x6A = base, 0x69 = 3); fw_log_chunk = fpga_log_chunk (n = count - consumed words, at most 2048,
  // each lane with fpga_read_lane = 0x1F, the lane command, the dummy strobe, n strobes; the second snapshot; 0x6A = R + n
  // when the ring did not lap); fpga_log_stop = 0x69 = 0 then snapshot + chunk until a chunk reads nothing
  reg [7:0]  lg [0:7];
  reg [31:0] lg_cnt;
  reg [12:0] lg_w;
  reg [15:0] lg_un;
  integer    lg_base, lg_cons, lg_n, lg_lane, nlanes;
  reg [7:0]  lanes [0:3];
  reg [7:0]  lgd [0:8191];                               // lane l, word j of the last chunk at lgd[l * 2048 + j]
  task fw_log_snapshot;
    begin
      wr_cmd(8'h38);
      for (i = 0; i < 8; i = i + 1) begin rd_byte; lg[i] = rd_d; end
      lg_w = {lg[0][4:0], lg[1]}; lg_cnt = {lg[2], lg[3], lg[4], lg[5]}; lg_un = {lg[6], lg[7]};
    end
  endtask
  task fw_log_start(input [31:0] divider, input [1:0] k);
    begin
      note("LOG_START 1: 0x0D, the filter (0x53, 0x56), then fpga_log_start: 0x69 = 01, 0x57 = 00, 0x1A = 00");
      wr4(8'h0D, divider);
      wr1(8'h53, {6'd0, k});
      wr1(8'h56, 8'h00);
      wr1(8'h69, 8'h01);
      wr1(8'h57, 8'h00);
      wr1(8'h1A, 8'h00);
      note("LOG_START 2: fpga_arm_reset with 0x0B = 0x0C = 0: 0x01 = 01, poll 0x05, 0x0B, 0x0C, 0x5E, 0x0E = 0, 0x01 = 00");
      wr2(8'h0B, 16'd0);
      wr2(8'h0C, 16'd0);
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05);
      note("poll 0x05: R lines until bit 0 = 1");
      polls = 0; quiet = 1; rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      quiet = 0;
      $fdisplay(tf, "R %h", rd_d);
      wr2(8'h0B, 16'd0);
      wr2(8'h0C, 16'd0);
      fw_trig_arm;
      wr4(8'h0E, 32'd0);
      wr1(8'h01, 8'h00);
      note("LOG_START 3: 0x38 (8 bytes), 0x6A = base = W - count, 0x69 = 03 (stream on, clear a stale lap)");
      fw_log_snapshot;
      lg_base = (lg_w - lg_cnt[12:0]) & 8191;
      lg_cons = 0;
      wr2(8'h6A, lg_base);
      wr1(8'h69, 8'h03);
    end
  endtask
  task fw_log_chunk;
    begin
      lg_n = lg_cnt - lg_cons;
      if (lg_n > 2048) lg_n = 2048;
      if (lg_n > 0) begin
        note("drain: per lane 0x1F = R, the lane command, the dummy strobe, n strobes; then 0x38 and 0x6A = R + n (no lap)");
        for (lg_lane = 0; lg_lane < nlanes; lg_lane = lg_lane + 1) begin
          pass(lanes[lg_lane], (lg_base + lg_cons) & 8191, lg_n);
          for (j = 0; j < lg_n; j = j + 1) lgd[lg_lane * 2048 + j] = pb[j];
        end
        fw_log_snapshot;
        if (lg[0][6] || (lg_cnt - lg_cons) > 8176) begin
          check(0, "L: the ring lapped during a chunk (not expected at this rate)");
          lg_n = 0;
        end else begin
          lg_cons = lg_cons + lg_n;
          wr2(8'h6A, (lg_base + lg_cons) & 8191);
        end
      end
    end
  endtask
  // the lanes of the last chunk against the memory words it came from (lane 0 = core B [15:8] / 16-bit high byte, lane 1 =
  // core A [7:0] / low byte for CH1), and how many LO -> HI transitions core A holds
  integer lg_tr;
  task lg_check_chunk(input integer cons0);
    begin
      okc = 1; lg_tr = 0;
      for (j = 0; j < lg_n; j = j + 1) begin
        if (lgd[j] !== dut.samples.mem[(lg_base + cons0 + j) & 8191][15:8]) okc = 0;
        if (lgd[2048 + j] !== dut.samples.mem[(lg_base + cons0 + j) & 8191][7:0]) okc = 0;
        if (lgd[2048 + j] != LO && lgd[2048 + j] != HI) okc = 0;
        if (j > 0 && lgd[2048 + j - 1] == LO && lgd[2048 + j] == HI) lg_tr = lg_tr + 1;
      end
    end
  endtask

  // 1-8: host ARM; mode 0 = off (0x57 = 00 written), 1 segments, 2 averaging; c56 = 0x56 (bit 0 FIR, bit 1 peak detect)
  task fw_arm(input [1:0] k, input [1:0] c56, input [5:0] sh, input [15:0] lvl16, input normal,
              input [12:0] pre, input [12:0] total, input [1:0] mode, input circ, input [11:0] count,
              input [3:0] m, input [15:0] n, input [31:0] hold5, input [31:0] tmo);
    begin
      note("ARM 1: filter (0x53, 0x56, 0x54, 0x55) and the settle wait");
      wr1(8'h53, {6'd0, k});
      wr1(8'h56, {6'd0, c56});
      if (k != 0) begin
        wr1(8'h54, {2'd0, sh});
        wr2(8'h55, lvl16);
        #((k + 10) * 10 * 10 + 1000);
      end
      note("ARM 2-3: 0x0F = 00, 0x1A");
      wr1(8'h0F, 8'h00);
      wr1(8'h1A, {7'd0, normal});
      note("ARM 4: 0x0B pre, 0x0C total (before the reset)");
      wr2(8'h0B, {3'd0, pre});
      wr2(8'h0C, {3'd0, total});
      note("ARM 5: sequencer setting");
      wr1(8'h57, {5'd0, circ, mode});
      if (mode != 0) begin
        wr2(8'h58, {4'd0, count});
        wr1(8'h59, {4'd0, m});
        wr2(8'h5A, n);                                  // segments: the firmware stores N = 0 (unused in mode 1)
        wr1(8'h5B, 8'h00);
        wr4(8'h5C, hold5);
      end
      note("ARM 6: 0x01 = 01, poll 0x05 bit 0");
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05);
      note("poll 0x05: R lines until bit 0 = 1");
      polls = 0; quiet = 1; rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      quiet = 0;
      $fdisplay(tf, "R %h", rd_d);
      note("ARM 7: 0x0B, 0x0C again, the trigger type (0x5E; 0x5F, 0x60 for a qualified type), 0x0E time-out");
      wr2(8'h0B, {3'd0, pre});
      wr2(8'h0C, {3'd0, total});
      fw_trig_arm;
      wr4(8'h0E, tmo);
      note("ARM 8: 0x01 = 00");
      wr1(8'h01, 8'h00);
    end
  endtask

  // main-loop poll: 0x0A then 0x18, until 0x0A = 1 (the transcript keeps only the last pass)
  reg [7:0] r0a, r18;
  task fw_poll_done(input integer max);
    begin
      note("poll: C 0a, R, C 18, R per main-loop pass until 0x0A bit 0 = 1 (last pass shown)");
      polls = 0; quiet = 1; r0a = 0;
      while (r0a[0] !== 1'b1 && polls < max) begin
        wr_cmd(8'h0A); rd_byte; r0a = rd_d;
        wr_cmd(8'h18); rd_byte; r18 = rd_d;
        polls = polls + 1;
        #2000;
      end
      quiet = 0;
      $fdisplay(tf, "C 0a"); $fdisplay(tf, "R %h", r0a); $fdisplay(tf, "C 18"); $fdisplay(tf, "R %h", r18);
    end
  endtask

  // at 0x0A = 1: 0x2A read until two reads agree (at most 4), then 0x0F = 01 and 0x14
  reg [15:0] ta;
  task fw_finish;
    begin
      note("DONE: 0x2A (8 bytes) until two reads agree");
      wr_cmd(8'h2A); for (i = 0; i < 8; i = i + 1) begin rd_byte; st[i] = rd_d; end
      agree = 0; tries = 1;
      while (!agree && tries < 4) begin
        wr_cmd(8'h2A); for (i = 0; i < 8; i = i + 1) begin rd_byte; st2[i] = rd_d; end
        agree = 1;
        for (i = 0; i < 8; i = i + 1) if (st[i] !== st2[i]) agree = 0;
        for (i = 0; i < 8; i = i + 1) st[i] = st2[i];
        tries = tries + 1;
      end
      note("DONE: 0x0F = 01, 0x14");
      wr1(8'h0F, 8'h01);
      wr_cmd(8'h14); rd_byte; ta[15:8] = rd_d; rd_byte; ta[7:0] = rd_d;
    end
  endtask

  // READ_SEQ what 0: table entries first .. first + count - 1
  reg [63:0] ent [0:255];
  task fw_read_table(input integer first, input integer count);
    begin
      note("READ_SEQ 0: per byte b = 0..7: 0x1F = first, C 2b+b, dummy, count reads");
      for (b = 0; b < 8; b = b + 1) begin
        pass(8'h2B + b, first & 11'h7FF, count);
        for (j = 0; j < count; j = j + 1) ent[j][8 * b +: 8] = pb[j];
      end
    end
  endtask

  // READ_SEQ what 1: average, per channel c; legacy bytes 4c..4c+3, 16-bit 4c, 4c+1 (raw: 4c..4c+3)
  reg [63:0] avw [0:2047];
  task fw_read_avg(input [4:0] s, input raw, input fmt16, input integer first, input integer count);
    integer c, nb;
    begin
      note("READ_SEQ 1: 0x5B = S, 0x57 = 02 | raw << 3, then one pass per lane byte");
      wr1(8'h5B, {3'd0, s});
      wr1(8'h57, raw ? 8'h0A : 8'h02);
      for (j = 0; j < count; j = j + 1) avw[j] = 0;
      nb = (fmt16 && !raw) ? 2 : 4;
      for (c = 0; c < 2; c = c + 1)
        for (b = 4 * c; b < 4 * c + nb; b = b + 1) begin
          pass(8'h2B + b, first, count);
          for (j = 0; j < count; j = j + 1) avw[j][8 * b +: 8] = pb[j];
        end
    end
  endtask

  // READ_RAW addr_mode 3: segment s, record words j0 .. j0 + n - 1, one core; split at the slot end
  reg [7:0] rw [0:4095];
  integer   rstart, rlen, rfirst;
  reg [63:0] e1;
  task fw_read_seg(input integer s, input integer m, input integer pre, input integer j0, input integer n,
                   input [1:0] core);
    begin
      note("READ_RAW 3: entry of the segment (8 x: 0x1F = s, C 2b+b, dummy, 1 read)");
      for (b = 0; b < 8; b = b + 1) begin pass(8'h2B + b, s, 1); e1[8 * b +: 8] = pb[0]; end
      rstart = (s << m) + ((e1[52:40] - pre + j0) & ((1 << m) - 1));
      rfirst = ((s + 1) << m) - rstart;
      if (rfirst > n) rfirst = n;
      note("READ_RAW 3: 0x1F = slot word, C 20+core, dummy, reads to the slot end, then the wrap part");
      pass(8'h20 + core, rstart, rfirst);
      for (j = 0; j < rfirst; j = j + 1) rw[j] = pb[j];
      if (rfirst < n) begin
        pass(8'h20 + core, s << m, n - rfirst);
        for (j = rfirst; j < n; j = j + 1) rw[j] = pb[j - rfirst];
      end
    end
  endtask

  //---------------------------------------------------------------------------------
  // Stimulus helpers

  // a step per record: high for `width` ns, `count` times, each `gap` ns after the last record's restart
  task steps(input integer count, input integer first_ns, input integer gap_ns, input integer width_ns);
    integer q;
    begin
      #(first_ns);
      for (q = 0; q < count; q = q + 1) begin
        sig = 1; #(width_ns); sig = 0; #(gap_ns);
      end
    end
  endtask

  function [5:0] shift_for(input [1:0] k, input [31:0] mm);
    reg [127:0] g;
    integer bb, ii;
    begin
      g = 255;
      for (ii = 0; ii < k; ii = ii + 1) g = g * mm;
      bb = 0;
      for (ii = 0; ii < 128; ii = ii + 1) if (g[ii]) bb = ii + 1;
      shift_for = (bb > 16) ? bb - 16 : 0;
    end
  endfunction

  //---------------------------------------------------------------------------------

  reg [8*80-1:0] tname;
  integer xcr, okc, base, off, slot, sh16, n_rec, w, ke, kt, kw;
  reg [15:0] lane, rec16 [0:63];
  reg [7:0]  recA [0:63];
  reg [63:0] rawsum;

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    if (!$value$plusargs("transcript=%s", tname)) tname = "contract.txt";
    tf = $fopen(tname, "w");
    #300;
    note("setup: generator off (0x153D: fpga_pat_drive(0) = 0x50 with four data bytes, fpga_pat_level(0) = 0x6D = 0), divider, trigger CH1 rising, level 128");
    wr4(8'h50, 32'd0);
    wr1(8'h6D, 8'h00);
    wr4(8'h0D, 32'd9);
    wr1(8'h15, 8'h00);
    wr1(8'h16, 8'h00);
    wr1(8'h17, 8'd128);
    wr_cmd(8'h06); rd_byte; rd_byte;
    note("boot (0x153C / 0x153D, fw-c5 / fw-d5 / fw-c7): fpga_trig_write(edge, hold-off 0, clear), fpga_ptrig_write(zeros: the engine disarmed), counter 0x62 = 128 / 6, 0x61 = gate 100 ms on the trigger source");
    fw_trig_write(8'h00, 24'd0, 24'd0, 32'd0, 1'b1);
    fw_ptrig_write(8'h00, 8'h00, 8'h00, 16'h0000, 8'h00, 24'd0, 24'd0, 1'b0, 1'b0);
    fw_counter_config(8'd128, 8'd6, 2'd2, 2'd0);
    #2000;

    //-------------------------------------------------------------------------------
    // S: legacy segments, 4 x 2^6, pre 10, total 40 (word 100 ns)
    $fdisplay(tf, "#");
    note("=== S: legacy segments, D = 9, CH1, 4 segments, m = 6, pre 10, total 40, hold-off 0");
    fork
      fw_arm(2'd0, 1'b0, 6'd0, 16'd0, 1'b1, 13'd10, 13'd40, 2'd1, 1'b0, 12'd4, 4'd6, 16'd0, 32'd0, 32'h00FFFFFF);
      begin wait (dut.sample_system_reset === 1'b1); wait (dut.sample_system_reset === 1'b0);
            steps(4, 2500, 4300, 2000); end
    join
    fw_poll_done(20000);
    fw_finish;
    check(st[0] == 8'h81 && {st[1], st[2], st[3]} == 4 && {st[4], st[5]} == 0, "S: 0x2A = done, segments, count 4, 0 untriggered");
    fw_read_table(0, 4);
    okc = 1;
    for (i = 0; i < 4; i = i + 1) begin
      if (ent[i][63] !== 1'b1 || ent[i][53] !== 1'b1) okc = 0;
      // the step at record index pre + 1 of the slot (core A, convention of 0x14)
      base = i << 6; off = ent[i][52:40];
      xcr = -1;
      for (j = 1; j < 40 && xcr < 0; j = j + 1)
        if (dut.samples.mem[base + ((off - 10 + j - 1) & 63)][7:0] < 128 && dut.samples.mem[base + ((off - 10 + j) & 63)][7:0] >= 128)
          xcr = j;
      if (xcr != 11) begin okc = 0; $display("S segment %0d: step at %0d", i, xcr); end
    end
    check(okc, "S: 4 valid triggered entries, the step at record index pre + 1 in every slot");
    check(ent[3][52:40] == (ta[12:0] & 63), "S: last entry offset = 0x14 mod 2^m");
    $fdisplay(tf, "#");
    note("=== SR: READ_RAW 3 of segment 2, words 0..39, core A (0x20) then core B (0x21)");
    fw_read_seg(2, 6, 10, 0, 40, 2'd0);
    okc = 1;
    for (j = 0; j < 40; j = j + 1) begin
      recA[j] = rw[j];
      if (rw[j] !== dut.samples.mem[(2 << 6) + ((e1[52:40] - 10 + j) & 63)][7:0]) okc = 0;
    end
    fw_read_seg(2, 6, 10, 0, 40, 2'd1);
    for (j = 0; j < 40; j = j + 1)
      if (rw[j] !== dut.samples.mem[(2 << 6) + ((e1[52:40] - 10 + j) & 63)][15:8]) okc = 0;
    check(okc && recA[10] == LO && recA[11] == HI, "SR: segment 2 read over the bus equals its slot; step at word 11");

    //-------------------------------------------------------------------------------
    // A: legacy averaging, EXT, D = 19 (word 200 ns), 1 position, N = 4, pre 8, total 32
    $fdisplay(tf, "#");
    note("=== A: legacy averaging, D = 19, EXT rising, 1 position, N = 4, pre 8, total 32");
    wr4(8'h0D, 32'd19);
    wr1(8'h15, 8'h02);
    ext_en = 1;
    #3000;
    fork
      fw_arm(2'd0, 1'b0, 6'd0, 16'd0, 1'b1, 13'd8, 13'd32, 2'd2, 1'b0, 12'd1, 4'd0, 16'd4, 32'd0, 32'h00FFFFFF);
      begin wait (dut.sample_system_reset === 1'b1); wait (dut.sample_system_reset === 1'b0);
            steps(4, 3000, 6000, 6000); end
    join
    fw_poll_done(20000);
    fw_finish;
    check(st[0] == 8'h82 && {st[1], st[2], st[3]} == 4, "A: 0x2A = done, averaging, count 4");
    // the record of the last pass from the sample memory; the steps are 60 words apart, so every record is the same
    for (j = 0; j < 32; j = j + 1) recA[j] = dut.samples.mem[(ta[12:0] - 8 + j) & 8191][7:0];
    fw_read_avg(5'd2, 1'b0, 1'b0, 0, 32);
    okc = 1;
    for (j = 0; j < 32; j = j + 1)
      if (avw[j][15:0] !== ((4 * recA[j]) >> 2)) okc = 0;
    check(okc, "A: lane CH1A at S = 2 equals the record value (4 equal records)");
    fw_read_avg(5'd0, 1'b1, 1'b0, 0, 32);
    okc = 1;
    for (j = 0; j < 32; j = j + 1)
      if (avw[j][15:0] !== 4 * recA[j]) okc = 0;
    check(okc, "A: raw lane CH1A = sum of 4 records");
    ext_en = 0;
    wr1(8'h15, 8'h00);

    //-------------------------------------------------------------------------------
    // F: 16-bit averaging, CIC2 + FIR at N = 20, CH1, 2 positions, N = 8, pre 6, total 24
    $fdisplay(tf, "#");
    note("=== F: 16-bit averaging, CIC2 + FIR, D = 19, CH1, 2 positions, N = 8, pre 6, total 24");
    sh16 = shift_for(2'd2, 10) + 1;
    fork
      fw_arm(2'd2, 1'b1, sh16, ((((LO + HI) / 2) * 100) >> sh16), 1'b1, 13'd6, 13'd24, 2'd2, 1'b0, 12'd2, 4'd0,
             16'd8, 32'd0, 32'h00FFFFFF);
      begin wait (dut.sample_system_reset === 1'b1); wait (dut.sample_system_reset === 1'b0);
            steps(16, 1500, 4000, 3000); end
    join
    fw_poll_done(20000);
    fw_finish;
    check(st[0] == 8'h92 && {st[1], st[2], st[3]} == 16, "F: 0x2A = done, 16-bit, averaging, count 16");
    // CH1 trigger: every record identical; the last record (position 1) from the sample memory
    for (j = 0; j < 24; j = j + 1) rec16[j] = dut.samples.mem[(ta[12:0] - 6 + j) & 8191][15:0];
    fw_read_avg(5'd3, 1'b0, 1'b1, 0, 48);
    okc = 1;
    for (j = 0; j < 24; j = j + 1) begin
      if (avw[24 + j][15:0] !== rec16[j]) okc = 0;          // position 1: 8 x rec >> 3
      if (avw[j][15:0] !== rec16[j]) okc = 0;               // position 0: the same step response
    end
    check(okc, "F: L0 (CH1) at S = 3 equals the single record in both positions");
    fw_read_avg(5'd0, 1'b1, 1'b1, 24, 24);
    okc = 1;
    for (j = 0; j < 24; j = j + 1) if (avw[j][31:0] !== 8 * rec16[j]) okc = 0;
    check(okc, "F: raw CH1 sum (bytes 0-3) = 8 x the record");

    //-------------------------------------------------------------------------------
    // P: circular segments, stop
    $fdisplay(tf, "#");
    note("=== P: circular legacy segments (D = 9, 4 x 2^5, pre 4, total 20), steps, then STOP");
    wr1(8'h53, 8'h00);
    wr4(8'h0D, 32'd9);
    fork
      fw_arm(2'd0, 1'b0, 6'd0, 16'd0, 1'b1, 13'd4, 13'd20, 2'd1, 1'b1, 12'd4, 4'd5, 16'd0, 32'd0, 32'h00FFFFFF);
      begin wait (dut.sample_system_reset === 1'b1); wait (dut.sample_system_reset === 1'b0);
            steps(6, 1000, 2500, 1000); end
    join
    #3000;
    note("STOP: 0x5D = 01, then 0x1A = 00, then poll 0x0A");
    wr1(8'h5D, 8'h01);
    wr1(8'h1A, 8'h00);
    fw_poll_done(20000);
    fw_finish;
    check(st[0][7] && st[0][6] && st[0][2] && {st[1], st[2], st[3]} == 6, "P: stopped circular run, overflow, count 6");
    fw_read_table(0, 4);
    check(ent[2][63] == 1'b0 && ent[0][63] && ent[1][63] && ent[3][63], "P: the slot being refilled (2) is invalid");
    wr1(8'h57, 8'h00);

    //-------------------------------------------------------------------------------
    // T: pulse width trigger (0x153C C1): longer than N = 5 words on CH1 at D = 9 (word 100 ns), hold-off 500 clocks
    $fdisplay(tf, "#");
    note("=== T: SET_TRIGGER2 pulse width longer than N = 5 words, CH1 positive pulse, D = 9, hold-off 500 x 5 ns; host ARM pre 10, total 40");
    wr4(8'h0D, 32'd9);
    fw_trig_write(8'h09, 24'd5, 24'd0, 32'd500, 1'b1);
    fork
      fw_arm(2'd0, 2'd0, 6'd0, 16'd0, 1'b1, 13'd10, 13'd40, 2'd0, 1'b0, 12'd0, 4'd0, 16'd0, 32'd0, 32'h00FFFFFF);
      begin wait (dut.sample_system_reset === 1'b1); wait (dut.sample_system_reset === 1'b0);
            steps(3, 2500, 700, 300);                    // three 3-word pulses: shorter than N, no trigger
            steps(1, 0, 3000, 1000); end                 // one 10-word pulse: the trigger on its trailing edge
    join
    fw_poll_done(20000);
    fw_finish;
    note("status: 0x37 then 0x34 (fpga_trig_status)");
    fw_trig_status;
    check(r18[0] == 1'b1, "T: triggered");
    check(ts34[0][1:0] == 2'd1 && ts34[0][2] == 1'b1 && ts34[0][4] == 1'b1 && ts37[3] == 1'b0,
          "T: 0x34 = pulse width, longer, a pulse began; 0x37 type bit 2 = 0");
    // the record: the trailing (falling) edge of the 10-word pulse at record index pre + 1 (core A), nine HI words before it
    xcr = -1;
    for (j = 1; j < 40 && xcr < 0; j = j + 1)
      if (dut.samples.mem[(ta[12:0] - 10 + j - 1) & 8191][7:0] >= 128 && dut.samples.mem[(ta[12:0] - 10 + j) & 8191][7:0] < 128)
        xcr = j;
    okc = 1;
    for (j = 2; j < 11; j = j + 1) if (dut.samples.mem[(ta[12:0] - 10 + j) & 8191][7:0] < 128) okc = 0;
    check(xcr == 11 && okc, "T: the falling edge of the long pulse at record index pre + 1 with nine HI words before it (the short pulses did not trigger)");
    fw_trig_write(8'h00, 24'd0, 24'd0, 32'd0, 1'b1);     // SET_TRIGGER: back to the edge type, no hold-off

    //-------------------------------------------------------------------------------
    // PK: peak detect (0x153C C2): 0x56 bit 1 with 0x53 = 1 (fw-c6), N = 20 (D = 19), CH1 rising at 128 << 8
    $fdisplay(tf, "#");
    note("=== PK: peak detect, 0x53 = 01, 0x56 = 02, 0x54 = 00, 0x55 = 128 << 8, D = 19 (N = 20), CH1 rising, pre 6, total 24");
    wr4(8'h0D, 32'd19);
    fork
      fw_arm(2'd1, 2'd2, 6'd0, 16'h8000, 1'b1, 13'd6, 13'd24, 2'd0, 1'b0, 12'd0, 4'd0, 16'd0, 32'd0, 32'h00FFFFFF);
      begin wait (dut.sample_system_reset === 1'b1); wait (dut.sample_system_reset === 1'b0);
            steps(1, 2500, 3000, 3000); end
    join
    fw_poll_done(20000);
    fw_finish;
    note("0x29 after the peak arm");
    wr_cmd(8'h29); rd_byte; st[0] = rd_d; rd_byte; st[1] = rd_d;
    check(st[0] == 8'h81 && st[1] == 8'hC0, "PK: 0x29 = 0x81 0xC0 (16-bit, K 1; peak active and written)");
    note("READ_RAW layout 3: CH1 min bytes (0x20) then max bytes (0x21), 0x1F = 0x14 - pre, dummy strobe, count reads");
    pass(8'h20, (ta[12:0] - 6) & 8191, 24);
    for (j = 0; j < 24; j = j + 1) recA[j] = pb[j];
    pass(8'h21, (ta[12:0] - 6) & 8191, 24);
    okc = 1;
    for (j = 0; j < 24; j = j + 1)
      if ({pb[j], recA[j]} !== dut.samples.mem[(ta[12:0] - 6 + j) & 8191][15:0]) okc = 0;
    check(okc, "PK: the min / max bytes read over the bus equal the memory words");
    check(recA[0] == LO && pb[0] == LO && recA[23] == HI && pb[23] == HI, "PK: { max, min } = { LO, LO } before and { HI, HI } after the step");
    check(recA[7] == LO && pb[7] == HI && recA[6] == LO && pb[6] == LO && recA[8] == HI,
          "PK: the window with the edge reads { HI, LO } at record index pre + 1 (the trigger word)");
    wr1(8'h56, 8'h00); wr1(8'h53, 8'h00);

    //-------------------------------------------------------------------------------
    // K: counter (0x153C C3): the gate restarted at 10 ms on the trigger source (CH1), a 100 kHz square, the 0x35 snapshot
    $fdisplay(tf, "#");
    note("=== K: counter, 0x62 = 128 / 6, 0x61 = 01 (gate 10 ms, source = the trigger source, CH1), 100 kHz square for 10.5 ms, then the 0x35 read");
    fw_counter_config(8'd128, 8'd6, 2'd1, 2'd0);
    steps(1050, 0, 5000, 5000);
    note("pass: 0x35, 16 bytes");
    fw_counter_read;
    ke = {cnt[2], cnt[3], cnt[4], cnt[5]};
    kt = {cnt[6], cnt[7], cnt[8], cnt[9]};
    kw = {cnt[10], cnt[11], cnt[12], cnt[13]};
    $display("K: 0x35 flags %h seq %0d E %0d T %0d W %0d min %0d max %0d", cnt[0], cnt[1], ke, kt, kw, cnt[14], cnt[15]);
    check(cnt[0][7] == 1'b1 && cnt[0][1:0] == 2'd1 && cnt[0][5:4] == 2'b00, "K: new result, gate 10 ms, source CH1");
    check(ke >= 999 && ke <= 1001, "K: E = 999 .. 1001 periods in the 10 ms gate at 100 kHz");
    check(ke > 0 && kt == ke * 2000, "K: T = E x 2000 clocks (10 us periods)");
    check(ke > 0 && kw * 2 >= kt - 2 * ke && kw * 2 <= kt + 2 * ke, "K: duty 50 % within one sample per period");
    check(cnt[14] == LO && cnt[15] == HI, "K: min / max = the square's codes");
    wr1(8'h61, 8'h00);

    //-------------------------------------------------------------------------------
    // L: streaming logger (0x153D D1, fw-d4): LOG_START CH1 format 0 (legacy: lanes 0x21 = core B, 0x20 = core A) at
    // D = 399 (a word every 4 us = 500 kS/s per channel, the contract's tier 1), a 20 us / 20 us square (5 words per half),
    // a drain step after 250 us and another after 150 us more, then LOG_STOP with the tail
    $fdisplay(tf, "#");
    note("=== L: streaming logger, LOG_START CH1 format 0 (lanes 0x21, 0x20), D = 399, 0x1A = 0, 0x0E = 0; a 20 us square; two drains; LOG_STOP");
    sig = 0;
    nlanes = 2; lanes[0] = 8'h21; lanes[1] = 8'h20;
    fw_log_start(32'd399, 2'd0);
    check(lg[0][7] == 1'b1 && lg[0][6] == 1'b0, "L: 0x38 after the arm: streaming, no lap");
    check(lg_w == lg_cnt[12:0] && lg_un == 0, "L: W = count mod 8192 and nothing unread before 0x6A");
    fork
      steps(10, 0, 20000, 20000);
      begin
        #250000;
        fw_log_snapshot;
        check(lg_cnt >= 60 && lg_cnt <= 64 && lg_w == lg_cnt[12:0] && lg_un == lg_cnt[15:0],
              "L: 250 us later 60 .. 64 words written (4 us each), W = count, unread = count");
        check(dut.sample_write_enable === 1'b1 && dut.sampling_triggered === 1'b0,
              "L: the write runs and no trigger is taken while streaming (0x0A and 0x18 read 0)");
        fw_log_chunk;
        lg_check_chunk(0);
        check(lg_n >= 60 && okc && lg_tr >= 5, "L: chunk 1 = the memory words, core A LO / HI with the square's 5 rising steps");
        check(lg_un == lg_cnt[15:0] - lg_cons[15:0] + lg_n[15:0], "L: the second snapshot (before 0x6A) reads unread = count - consumed");
        #150000;
        fw_log_snapshot;
        fw_log_chunk;
        lg_check_chunk(lg_cons - lg_n);
        check(lg_n >= 35 && okc && lg_tr >= 3, "L: chunk 2 contiguous with chunk 1 (the memory words from R)");
      end
    join
    note("LOG_STOP: 0x69 = 00, then the tail: 0x38 and a chunk, repeated until a chunk reads nothing");
    wr1(8'h69, 8'h00);
    tries = 0;
    while (tries < 8) begin
      fw_log_snapshot;
      fw_log_chunk;
      tries = (lg_n == 0) ? 8 : tries + 1;
    end
    check(lg[0][7] == 1'b0 && lg[0][6] == 1'b0, "L: 0x38 after LOG_STOP: stream 0, no lap");
    check(dut.sample_write_enable === 1'b0 && lg_un == 0 && lg_cons == lg_cnt,
          "L: the write stopped (auto mode, 0x0E = 0), the tail drained: consumed = count, unread 0");
    check(dut.trigger_address[12:0] == lg_w && lg_w == lg_cnt[12:0], "L: 0x14 = W = count mod 8192 after the stop");
    check(lg_cnt >= 100 && lg_cnt <= 118, "L: 400 us of square plus the bus time at 4 us per word: 100 .. 118 words in all");
    note("log ended: the UI's 0x0D restored (pc_log_finish), normal mode again");
    wr4(8'h0D, 32'd9);
    wr1(8'h1A, 8'h01);

    $fclose(tf);
    $display("RESULT: %0d checks, %0d failed -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #500_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
