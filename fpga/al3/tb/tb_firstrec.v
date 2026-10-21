// First decimated record after a configuration change (fpga/al3/src/zaklad.v, 0x153A; fpga-gen10).
// Run with fpga/al3/tb/run.sh (ONLY=firstrec) or directly with vvp (+seed, +total).
//
// Bench (docs/hardware-status.md 2026-10-20 10:52 .. 11:22): on 0x153A a 16-bit capture that follows a change of the
// decimator mode, shift or sample rate sometimes comes back as a whole record of 0xFFFF on both channels, the next
// capture at the same settings is clean, 8-bit captures never do it. This testbench replays the firmware's exact MCU
// byte sequences (pc_hal_timebase_write raw, pc_dec_apply: 0x53, 0x56, 0x54, 0x55 and the settle wait; pc_hal_arm /
// fpga_arm_reset: 0x17, 0x0F, 0x1A, 0x0B / 0x0C before 0x01 = 1 (fw-bus order) or only inside the window (1.00o5 order),
// the 0x05 wait, 0x0B / 0x0C, 0x0E, 0x01 = 0; pc_hal_acq_poll; fpga_read_decim_data through fpga_read_start with its
// dummy strobe) on a constant ADC input, and checks every word of the record against v x G >> S (exact: the FIR's DC
// gain is 1 with its rounding), counting 0xFFFF words. Scenarios are the bench's transitions at divider 4095:
//   S1  legacy capture at D 199, then 0x0D = 4095 and CIC2+FIR (10:55), twice; fw-bus and 1.00o5 arm order
//   S2  the FPGA as a device sweep leaves it (CIC3+FIR at N 16, then a K = 0 UI conversion at D 15), 0x0D = 4095,
//       CIC3+FIR twice, CIC2+FIR twice (11:15 round 0)
//   S3  the Bode pre-scan: legacy capture at D 0, 0x0D = 4095, CIC3+FIR with pretrigger 0 in FPGA auto mode
//   S4  S1's transition and CIC3+FIR -> CIC2+FIR without the settle wait, informational: the transient reaches record
//       words 0 .. 5 and is not 0xFFFF; in auto mode the 16-bit trigger fires on those words, so trigger_address and the
//       record start move (the last words read then lie past the written record). The firmware waits
//       (K + 10) N x 5 ns + 1 us, rounded up to 2 ms ticks, and the record is clean from word 0 after the wait.
//   S5  the firmware fault model: CIC2+FIR at D 4095 armed with the shift of N 400 (S 9, GET_DECIM next_shift 9 /
//       next_n 400 of the 10:57 case): the whole record is 0xFFFF, the bench's signature
// PASS: S1, S2, S3 exact from word 0 in both arm orders; S4 reports the transient (informational); S5 saturates every word.
`timescale 1ns / 1ps

module tb;

  reg i_xtal = 0;
  always #10 i_xtal = ~i_xtal;

  reg       mcu_clk  = 1;
  reg       mcu_rws  = 1;
  reg       mcu_dcs  = 0;
  reg [7:0] mcu_dout = 0;

  localparam [7:0] V1 = 8'd100, V2 = 8'd155;           // constant ADC codes, CH1 and CH2
  reg [7:0] adc1A = V1, adc1B = V1, adc2A = V2, adc2B = V2;

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
  integer total = 16;                                   // record words per capture (+total)

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
  // MCU bus (fpga_control.c: data set up, clock low then high; strobe period about 100 to 300 ns on the real MCU)

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

  // fpga_read_start (0x1F = start, command, read mode, one dummy strobe) then n strobes
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

  //---------------------------------------------------------------------------------
  // Filter helpers (pc_decim.c)

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

  // the firmware's shift for K, FIR and divider d (pc_decim_config: + 1 with the FIR)
  function [5:0] fw_shift(input [1:0] k, input fir, input [31:0] d);
    begin
      fw_shift = (k == 0) ? 6'd0 : (fir ? shift_for(k, (d / 2) + 1) + 1 : shift_for(k, 2 * ((d / 2) + 1)));
    end
  endfunction

  // 16-bit output of a constant input v
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

  integer cur_k = -1, cur_fir = -1, cur_d = -1;
  integer cur_sh = -1;

  // pc_hal_timebase_write raw / fpga_set_sample_rate
  task set_rate(input [31:0] d);
    begin
      if (d != cur_d) begin wr4(8'h0D, d); cur_d = d; end
    end
  endtask

  // pc_dec_apply: mode when changed, 0x56 when changed, shift when changed or the last was K = 0, the level always,
  // then the settle wait when anything (rate included) changed. settle = 0 skips the wait (S4), sh_force >= 0 writes
  // that shift instead of the firmware's (S5).
  reg rate_dirty = 0;
  task apply(input [1:0] k, input fir, input settle, input integer sh_force);
    integer sh;
    begin
      sh = (sh_force >= 0) ? sh_force : fw_shift(k, fir, cur_d);
      if (k != cur_k) begin wr1(8'h53, {6'd0, k}); rate_dirty = 1; end
      if (fir != cur_fir) begin wr1(8'h56, {7'd0, fir}); rate_dirty = 1; end
      if (k != 0) begin
        if (cur_k <= 0 || sh != cur_sh) begin wr1(8'h54, sh[5:0]); rate_dirty = 1; end
        wr2(8'h55, 16'h8000);
        if (rate_dirty && settle) begin
          if (fir) #((k + 10) * (2 * ((cur_d / 2) + 1)) * 5 + 1000);
          else     #((k + 2) * (2 * ((cur_d / 2) + 1)) * 5);
        end
      end
      rate_dirty = 0;
      cur_k = k; cur_fir = fir; cur_sh = (k != 0) ? sh : cur_sh;
    end
  endtask

  // pc_hal_arm / fpga_arm_reset: FPGA auto mode with time-out tot (bh_arm; a constant input never triggers normal mode); order 1 = 0x0B / 0x0C
  // before 0x01 = 1 as well (fw-bus), 0 = only inside the window (1.00o5)
  integer polls;
  task arm(input integer pre_w, input integer tot_w, input normal, input order);
    begin
      wr1(8'h17, 8'd128);
      wr1(8'h0F, 8'h00);
      wr1(8'h1A, normal ? 8'h01 : 8'h00);
      if (order) begin wr2(8'h0B, pre_w); wr2(8'h0C, tot_w); end
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05);
      polls = 0;
      rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      wr2(8'h0B, pre_w);
      wr2(8'h0C, tot_w);
      wr4(8'h0E, normal ? 32'h00FFFFFF : tot_w);
      wr1(8'h01, 8'h00);
    end
  endtask

  // pc_hal_acq_poll until done, pc_hal_acq_finish, then the 16-bit readout of both channels (fpga_read_decim_data)
  // or the legacy core A readout; the record is checked word by word against the constant input
  reg [15:0] y1 [0:8191];
  reg [15:0] y2 [0:8191];
  integer    done, ta, start, i, nsat, nbad, first_bad, last_bad;
  reg [15:0] e1, e2;

  task run_capture(input [8*40-1:0] label, input [1:0] k, input fir, input integer pre_w, input integer tot_w,
                   input normal, input order, input expect_clean);
    integer sh;
    begin
      arm(pre_w, tot_w, normal, order);
      polls = 0; done = 0;
      while (!done && polls < 400000) begin
        rd1(8'h0A); done = rd_d[0];
        rd1(8'h18);
        polls = polls + 1;
      end
      wr1(8'h0F, 8'h01);
      rd2(8'h14);
      ta = rd16 & 8191;
      start = (ta - pre_w) & 8191;
      nsat = 0; nbad = 0; first_bad = -1; last_bad = -1;
      if (k != 0) begin
        read_lane(8'h20, start, tot_w);
        for (i = 0; i < tot_w; i = i + 1) y1[i][7:0] = rb[i];
        read_lane(8'h21, start, tot_w);
        for (i = 0; i < tot_w; i = i + 1) y1[i][15:8] = rb[i];
        read_lane(8'h22, start, tot_w);
        for (i = 0; i < tot_w; i = i + 1) y2[i][7:0] = rb[i];
        read_lane(8'h23, start, tot_w);
        for (i = 0; i < tot_w; i = i + 1) y2[i][15:8] = rb[i];
        sh = cur_sh;
        e1 = out16(V1, k, fir, cur_d, sh[5:0]);
        e2 = out16(V2, k, fir, cur_d, sh[5:0]);
        for (i = 0; i < tot_w; i = i + 1) begin
          if (y1[i] == 16'hFFFF || y2[i] == 16'hFFFF) nsat = nsat + 1;
          if (y1[i] !== e1 || y2[i] !== e2) begin
            nbad = nbad + 1;
            if (first_bad < 0) first_bad = i;
            last_bad = i;
          end
        end
      end else begin
        read_lane(8'h20, start, tot_w);
        for (i = 0; i < tot_w; i = i + 1) if (rb[i] !== V1) begin nbad = nbad + 1; if (first_bad < 0) first_bad = i; last_bad = i; end
        read_lane(8'h22, start, tot_w);
        for (i = 0; i < tot_w; i = i + 1) if (rb[i] !== V2) begin nbad = nbad + 1; if (first_bad < 0) first_bad = i; last_bad = i; end
      end
      $display("%0s  K %0d fir %0d D %0d S %0d order %0d  done %0d ta %0d  words %0d  bad %0d (first %0d last %0d)  0xFFFF %0d  expect %04h/%04h  w0 %04h/%04h  wN %04h/%04h  0x29 %04h",
               label, k, fir, cur_d, cur_sh, order, done, ta, tot_w, nbad, first_bad, last_bad, nsat, e1, e2,
               y1[0], y2[0], y1[tot_w - 1], y2[tot_w - 1], {dut.dec_on, dut.fir_en, dut.fir_act, 3'b000, dut.dec_order, 2'b0, dut.dec_shift});
      check(done == 1, "capture completed");
      if (expect_clean) check(nbad == 0, "record exact from word 0");
    end
  endtask

  //---------------------------------------------------------------------------------
  // Scenarios

  integer order;
  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    if (!$value$plusargs("total=%d", total)) total = 16;
    #2000;
    wr1(8'h01, 8'h00);
    wr1(8'h15, 8'h00);
    wr1(8'h16, 8'h00);
    wr1(8'h57, 8'h00);
    wr1(8'h53, 8'h00);
    wr1(8'h56, 8'h00);
    wr1(8'h54, 8'h00);
    cur_k = 0; cur_fir = 0; cur_sh = 0;
    set_rate(0);

    for (order = 1; order >= 0; order = order - 1) begin
      $display("---- S1 (order %0d): legacy at D 199, then 0x0D = 4095 and CIC2+FIR, twice", order);
      set_rate(199);
      apply(0, 0, 1, -1);
      run_capture("S1 legacy D199  ", 0, 0, total / 2, total, 0, order, 1);
      set_rate(4095);
      apply(2, 1, 1, -1);
      run_capture("S1 CIC2+FIR #1  ", 2, 1, total / 2, total, 0, order, 1);
      apply(2, 1, 1, -1);
      run_capture("S1 CIC2+FIR #2  ", 2, 1, total / 2, total, 0, order, 1);
    end

    order = 1;
    $display("---- S2: sweep end (CIC3+FIR N 16), K = 0 at D 15, 0x0D = 4095, CIC3+FIR x2, CIC2+FIR x2");
    set_rate(15);
    apply(3, 1, 1, -1);
    run_capture("S2 CIC3+FIR N16 ", 3, 1, 0, total, 0, order, 1);
    apply(0, 0, 1, -1);
    run_capture("S2 legacy D15   ", 0, 0, total / 2, total, 0, order, 1);
    set_rate(4095);
    apply(3, 1, 1, -1);
    run_capture("S2 CIC3+FIR #1  ", 3, 1, total / 2, total, 0, order, 1);
    apply(3, 1, 1, -1);
    run_capture("S2 CIC3+FIR #2  ", 3, 1, total / 2, total, 0, order, 1);
    apply(2, 1, 1, -1);
    run_capture("S2 CIC2+FIR #1  ", 2, 1, total / 2, total, 0, order, 1);
    apply(2, 1, 1, -1);
    run_capture("S2 CIC2+FIR #2  ", 2, 1, total / 2, total, 0, order, 1);

    $display("---- S3: Bode pre-scan: legacy at D 0, 0x0D = 4095, CIC3+FIR, pretrigger 0, FPGA auto");
    set_rate(0);
    apply(0, 0, 1, -1);
    run_capture("S3 legacy D0    ", 0, 0, 0, total, 0, order, 1);
    set_rate(4095);
    apply(3, 1, 1, -1);
    run_capture("S3 CIC3+FIR pre ", 3, 1, 0, total, 0, order, 1);

    $display("---- S4: S1's transition without the settle wait (transient length, informational)");
    set_rate(199);
    apply(0, 0, 1, -1);
    run_capture("S4 legacy D199  ", 0, 0, total / 2, total, 0, order, 1);
    set_rate(4095);
    apply(2, 1, 0, -1);
    run_capture("S4 CIC2+FIR nowt", 2, 1, 0, total, 0, order, 0);
    $display("---- S4b: CIC3+FIR -> CIC2+FIR at D 4095 without the settle wait");
    apply(3, 1, 1, -1);
    run_capture("S4b CIC3+FIR    ", 3, 1, 0, total, 0, order, 1);
    apply(2, 1, 0, -1);
    run_capture("S4b CIC2+FIR nwt", 2, 1, 0, total, 0, order, 0);

    $display("---- S5: the firmware fault model: CIC2+FIR at D 4095 armed with the shift of N 400 (S 9)");
    apply(2, 1, 1, 9);
    run_capture("S5 CIC2+FIR S9  ", 2, 1, 0, total, 0, order, 0);
    check(nsat == total, "S5: a stale shift saturates every word of the record (the bench's signature)");
    apply(2, 1, 1, -1);
    run_capture("S5 CIC2+FIR S15 ", 2, 1, 0, total, 0, order, 1);

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
