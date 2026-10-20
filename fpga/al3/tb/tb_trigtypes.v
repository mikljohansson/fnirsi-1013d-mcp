// Trigger types and hold-off testbench (fpga/al3/src/zaklad.v, 0x153C C1): pulse width, Nth edge and timeout on CH1,
// CH2 and EXT, legacy and decimated; the trigger hold-off (0x1B) across sequencer restarts and MCU re-arms; the 0x153B
// stop gate per type. Run with fpga/al3/tb/run.sh (ONLY=trigtypes, step 14) or vvp (+seed, +runs, +logfile).
// Rebased from gen8-features-wip 9de82ca (built on 0x1539) onto 0x153B: "longer" is 0x5E bit 3, the type is 3 bits
// (4..7 act as edge), the ADC qualifier processes word 0 of the capture first (q_hr_adc; gen8 processed the reset value
// of trigger_data first), the version word is 0x153B or later.
//
// One instance, the AD9288 model and the MCU byte sequences of tb_trigpos.v (pc_hal_arm and the poll). For every
// capture the testbench rebuilds the trigger stream the FPGA saw since the release of 0x01:
//   - ADC sources: every word written to the sample memory (core A of the channel, or its 16-bit sample with K >= 1);
//     for the edge type the reset value of trigger_data (code 0) comes first (the edge compare sees it as the sample
//     before word 0); the memory holds the whole stream because a capture writes fewer than 8192 words. The arm flag of
//     each sample (sampling_half_way after the write clock edge that loaded it) is logged by a monitor.
//   - EXT: ext_level and sampling_half_way at every clk_200MHz edge since cap_rst200 fell.
// A reference model (task model below, written from the definitions in docs/plans/fpga-0x153c.md section 3.3 and
// fpga/al3/README.md "Changes" section 12) finds the sample on which each trigger type fires. The checks:
//   1. exact position: ADC: (memory index of the trigger sample) - trigger_address is the same constant as for edge
//      captures of the same case (measured in the same run); EXT: the time from the clock that decides the trigger to
//      the write of word trigger_address lies in the window the EXT edge captures span (less than one word);
//   2. intent: the stimulus places the qualifying event at a known instant (the trailing edge of the k-th pulse, the
//      Nth edge of a burst, the moment a level has been held for N samples), with margins of 2 samples or more around
//      every threshold; the trigger sample found by the model must be that event (within one sample);
//   3. the Nth edge with N = 1 and no idle time triggers exactly where the edge type does (same memory index);
//   4. the reserved types 4 and 7 trigger where the edge type does; 0x34 / 0x37 read the configuration back.
// Boundary cases (pulse widths of N - 1, N, N + 1 samples) come from randomised pulses near the thresholds and are
// checked by the model only.
// Hold-off (CH1 legacy at 10 MSa/s, a square wave of period P = 40 words = 4 us, hold-off 4.5 P): a 4-segment run
// triggers every fifth edge (5 P apart: the hold-off outlives the sequencer's restart and the pretrigger fill, which
// alone would give 2 P), two single captures re-armed by the MCU are 5 P apart, 0x66 = 1 then 0x01 = 1 clears the timer
// (the next trigger is the first edge after the arm) and the one-shot is consumed (the arm after that is held off again),
// 0x1B = 0 releases the mask at once, an auto capture inside the hold-off ends untriggered with 0x14 = W - total +
// pretrigger, 0x37 bit 4 follows the timer; the same spacing on EXT and with the pulse-width type.
// Stop gate (0x153B rule): a capture that ended on the auto time-out takes no trigger from a qualifying event
// afterwards, for every type on CH1 and EXT: 0x18 stays 0, 0x14 and sampling_triggered / ext_pending do not move.
// Late write: at divider 999 the type and N written after the 0x05 wait, right before 0x01 = 0, apply to that capture
// (the contract's no-0x01-window rule; gen8's reset kept the stale value at slow rates).
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

  reg  sig = 0;                                         // CH1 HI when 1, CH2 its complement, EXT pad = sig
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

  integer errors = 0, checks = 0, seed, logf;
  reg [8*200-1:0] logname;

  task check(input cond, input [8*140-1:0] what);
    begin
      checks = checks + 1;
      if (!cond) begin
        errors = errors + 1;
        $display("FAIL  t=%0t  %0s", $time, what);
      end
    end
  endtask

  //---------------------------------------------------------------------------------
  // AD9288 model (as tb_trigpos.v): sample on the encode rising edge, output 4 encode cycles later, tPD 3.3 ns

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
  // Monitors: the ADC trigger stream's arm flags and write times, the EXT stream, the trigger instants

  localparam NS = 8192, NE = 65536;
  reg        arm_w [0:NS - 1];                            // sampling_half_way after the edge that wrote word i
  reg [63:0] wt [0:NS - 1];                               // write time of word i (ps)
  integer    n_w = 0;                                     // words written since the release
  reg        z_e [0:NE - 1];                              // ext_level after clk_200MHz edge t
  reg        arm_e [0:NE - 1];                            // sampling_half_way before edge t
  reg        rst_e [0:NE - 1];                            // cap_rst200 before edge t
  reg [63:0] tz [0:NE - 1];
  integer    n_e = 0;
  reg        mon_on = 0;
  reg        r_adv, r_we, r_rst, r_arm, r_rst200;
  integer    r_addr;

  always @(posedge dut.sample_write_clock) begin
    r_adv = dut.swc_adv; r_we = dut.sample_write_enable; r_rst = dut.cap_rst; r_addr = dut.sample_write_address;
    #0.1;
    if (mon_on && !r_rst && r_adv && r_we && r_addr < NS) begin
      arm_w[r_addr] = dut.sampling_half_way;
      wt[r_addr] = $realtime * 1000.0;
      if (r_addr + 1 > n_w) n_w = r_addr + 1;
    end
  end

  always @(posedge dut.clk_200MHz) begin
    r_arm = dut.sampling_half_way; r_rst200 = dut.cap_rst200;
    #0.1;
    if (mon_on && n_e < NE) begin
      z_e[n_e] = dut.ext_level; arm_e[n_e] = r_arm; rst_e[n_e] = r_rst200; tz[n_e] = $realtime * 1000.0;
      n_e = n_e + 1;
    end
  end

  // trigger instants (sampling_triggered rises) and the hold-off state 10 ns after each, for the hold-off cases
  integer n_trig = 0;
  real    trig_t [0:63];
  reg     trig_ho [0:63];
  always @(posedge dut.sampling_triggered) begin
    if (n_trig < 64) begin
      trig_t[n_trig] = $realtime;
      #10 trig_ho[n_trig] = dut.ho_act;
      n_trig = n_trig + 1;
    end
  end

  // +dbg=1: EXT qualifier events, ext_pending and the trigger
  integer dbg = 0;
  initial if (!$value$plusargs("dbg=%d", dbg)) dbg = 0;
  always @(posedge dut.clk_200MHz) if (dbg && mon_on) begin
    if (dut.ext_qev) $display("  dbg t=%0t ext_qev (check %b pending %b triggered %b)", $time, dut.trigger_check_enable, dut.ext_pending, dut.sampling_triggered);
    if (dut.ext_pending && dut.swc_rise) $display("  dbg t=%0t pending at swc_rise, adv_q %b", $time, dut.swc_adv_q);
  end

  //---------------------------------------------------------------------------------
  // MCU bus (as tb_trigpos.v)

  real stb = 43.0;
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
  task wr3(input [7:0] c, input [23:0] v); begin wr_cmd(c); wr_byte(v[23:16]); wr_byte(v[15:8]); wr_byte(v[7:0]); end endtask
  task wr4(input [7:0] c, input [31:0] v);
    begin wr_cmd(c); wr_byte(v[31:24]); wr_byte(v[23:16]); wr_byte(v[15:8]); wr_byte(v[7:0]); end
  endtask
  reg [15:0] rd16;
  task rd2(input [7:0] c); begin wr_cmd(c); rd_byte; rd16[15:8] = rd_d; rd_byte; rd16[7:0] = rd_d; end endtask
  task rd1(input [7:0] c); begin wr_cmd(c); rd_byte; end endtask

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
  function [15:0] out16(input [7:0] v, input [1:0] k, input [31:0] d, input [5:0] sh);
    reg [127:0] g;
    integer i;
    begin
      g = v;
      for (i = 0; i < k; i = i + 1) g = g * (2 * ((d / 2) + 1));
      g = g >> sh;
      out16 = (g > 65535) ? 16'hFFFF : g[15:0];
    end
  endfunction

  //---------------------------------------------------------------------------------
  // Reference model. a[k]: processed sample k is on the pre-detect side; arm[k]: the trigger could fire with sample k
  // as the data sample (and an Nth edge into sample k counts, idle time 0). Returns the data index of the first trigger,
  // or -1. typ 0 edge, 1 pulse width (lng: longer than N), 2 Nth edge (idle H), 3 timeout.

  reg     ma   [0:NE];
  reg     marm [0:NE];                                   // arm when sample k is processed (an Nth edge into k counts)
  reg     darm [0:NE];                                   // arm when sample k is the data sample of a detection
  integer mn;

  task model(input integer typ, input integer lng, input integer nn, input integer hh, output integer det);
    integer k, run_pre, run_post, vld, ap, cnt, cv, past, gap, lastt, fire, n1, neff;
    begin
      det = -1;
      run_pre = 0; run_post = 0; vld = 0; ap = 0; cnt = 0; past = 0; lastt = -1;
      cv = (hh == 0);
      n1 = (nn % 65536 == 0) ? 0 : (nn % 65536) - 1;       // Nth edge: N - 1 (N = 0 acts as 1)
      neff = (nn == 0) ? 1 : nn;                           // timeout: N = 0 acts as 1
      for (k = 0; k < mn && det < 0; k = k + 1) begin
        // the decision for candidate k (state = samples 0 .. k-1)
        gap = (hh != 0) && (k - 1 - lastt >= hh - 1);
        case (typ)
          0: fire = ma[k];
          1: fire = ma[k] && vld && (lng ? (run_pre >= nn) : (run_pre <= nn - 2));
          2: fire = ma[k] && (cv || gap) && ((gap ? 0 : cnt) == n1) && !(past && !gap);
          default: fire = !ma[k] && (run_post == neff - 1);
        endcase
        if (k + 1 < mn && fire && !ma[k + 1] && darm[k + 1]) det = k + 1;
        // process sample k
        if (ap && !ma[k] && (cv || gap) && (hh == 0 ? marm[k] : 1)) begin   // a counted edge into k
          if (gap) begin cnt = 1; past = (n1 == 0) ? 1 : 0; end
          else if (!past) begin past = (cnt == n1); cnt = cnt + 1; end
          cv = 1;
        end else if (gap) begin cnt = 0; past = 0; cv = 1; end
        if (ap != ma[k]) lastt = k;
        if (k == 0 && ap != ma[0]) lastt = 0;
        if (ma[k]) begin run_pre = run_pre + 1; run_post = 0; end else begin run_pre = 0; run_post = run_post + 1; vld = 1; end
        ap = ma[k];
      end
    end
  endtask

  //---------------------------------------------------------------------------------
  // Cases: source (0 CH1, 1 CH2, 2 EXT), K, divider

  integer nc = 0;
  integer c_src [0:15], c_k [0:15], c_d [0:15];
  integer c_cal [0:15], c_cal_set [0:15];                // ADC: index - trigger_address of the edge captures
  real    c_qmin [0:15], c_qmax [0:15];                  // EXT: (write time of word ta) - (deciding clock), ns

  task add_case(input integer src, input integer k, input integer d);
    begin
      c_src[nc] = src; c_k[nc] = k; c_d[nc] = d; c_cal_set[nc] = 0; c_qmin[nc] = 1.0e9; c_qmax[nc] = -1.0e9;
      nc = nc + 1;
    end
  endtask

  integer cur_c = -1;
  reg [5:0]  sh;
  reg [15:0] lvl16;
  real       tu;                                         // trigger stream sample period (ns)

  task configure(input integer c);
    begin
      wr4(8'h0D, c_d[c]);
      sh = (c_k[c] == 0) ? 6'd0 : shift_for(c_k[c], 2 * ((c_d[c] / 2) + 1));
      wr1(8'h53, c_k[c]);
      wr1(8'h56, 8'h00);
      wr1(8'h54, {2'd0, sh});
      if (c_k[c] != 0) #((c_k[c] + 10) * ((c_d[c] / 2) + 1) * 10 + 1000);
      wr1(8'h15, (c_src[c] == 2) ? 8'd2 : c_src[c]);
      ext_en = (c_src[c] == 2);
      lvl16 = (out16(LO, c_k[c], c_d[c], sh) + out16(HI, c_k[c], c_d[c], sh)) / 2;
      tu = (c_src[c] == 2) ? 5.0 : (c_k[c] == 0) ? 10.0 * (c_d[c] + 1) : 10.0 * ((c_d[c] / 2) + 1);
      cur_c = c;
    end
  endtask

  //---------------------------------------------------------------------------------
  // Stimulus: up to 64 level changes of the channel signal at absolute times (ps); the CH2 signal is the complement

  integer    n_ev;
  real       ev_t [0:63];                                // ns after the stimulus start
  reg        inv = 0;
  real       t_q;                                        // intended instant of the qualifying event (ns after start)
  real       t_start;

  task play;
    integer i;
    real    tprev;
    begin
      t_start = $realtime;
      tprev = 0.0;
      for (i = 0; i < n_ev; i = i + 1) begin
        #(ev_t[i] - tprev);
        tprev = ev_t[i];
        sig = ~sig;
      end
    end
  endtask

  // free-running square wave on sig (hold-off cases): half period sq_half ns while sq_on
  reg  sq_on = 0;
  real sq_half = 2000.0;
  always begin
    if (sq_on) begin #(sq_half); if (sq_on) sig = ~sig; end
    else @(posedge sq_on);
  end

  //---------------------------------------------------------------------------------
  // One capture

  integer ta, det, idx, i, done, polls, t0e, dsamp, qtyp;
  real    q, t_det;
  reg     dir;

  // the stream of the last capture for the model: with_reset = 1 puts the reset value of trigger_data (code 0) first,
  // as the edge compare sees it; the qualified types start with word 0 (q_hr_adc)
  task build_stream(input integer with_reset);
    begin
      if (c_src[cur_c] != 2) begin
        if (with_reset) begin
          mn = n_w + 1;
          ma[0] = ~dir;                                  // code 0: below the level
          marm[0] = 0; darm[0] = 0;
        end else
          mn = n_w;
        for (i = 0; i < n_w; i = i + 1) begin
          if (c_k[cur_c] == 0)
            ma[i + with_reset] = ((((c_src[cur_c] == 1) ? dut.samples.mem[i][23:16] : dut.samples.mem[i][7:0]) >= 8'd128) ~^ dir);
          else
            ma[i + with_reset] = ((((c_src[cur_c] == 1) ? dut.samples.mem[i][31:16] : dut.samples.mem[i][15:0]) >= lvl16) ~^ dir);
          marm[i + with_reset] = arm_w[i];
          darm[i + with_reset] = arm_w[i];
        end
      end else begin
        // EXT: processed sample k is z[t0 - 2 + k], t0 the first edge with cap_rst200 low
        t0e = 0;
        while (t0e < n_e && rst_e[t0e]) t0e = t0e + 1;
        mn = n_e - t0e + 2;
        for (i = 0; i < mn; i = i + 1) begin
          ma[i]   = ((t0e - 2 + i >= 0) ? z_e[t0e - 2 + i] : 1'b0) ~^ dir;
          marm[i] = (t0e + i < n_e) ? arm_e[t0e + i] : 1'b0;
          // edge type: ext_pending is set on the edge after the decision; other types one edge later (ext_qev_r)
          darm[i] = (qtyp == 0) ? ((t0e - 1 + i < n_e && t0e - 1 + i >= 0) ? arm_e[t0e - 1 + i] : 1'b0)
                                : ((t0e + i < n_e) ? arm_e[t0e + i] : 1'b0);
        end
      end
    end
  endtask

  // the firmware's arm (pc_hal_arm / fpga_arm_reset) with the type registers written before 0x01 = 1
  task arm_fw(input integer pre_w, input integer tot_w, input normal, input [31:0] t_out);
    begin
      wr1(8'h0F, 8'h00);
      wr1(8'h1A, normal ? 8'h01 : 8'h00);
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05); polls = 0; rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      wr2(8'h0B, pre_w);
      wr2(8'h0C, tot_w);
      wr4(8'h0E, t_out);
      n_w = 0; n_e = 0;
      mon_on = 1;
      wr1(8'h01, 8'h00);
    end
  endtask

  task poll_done(input integer max_polls);
    begin
      polls = 0; done = 0;
      while (!done && polls < max_polls) begin rd1(8'h0A); done = rd_d[0]; polls = polls + 1; end
    end
  endtask

  task capture(input integer typ, input integer pol, input integer lng, input integer nn, input integer hh,
               input integer early);
    begin
      qtyp = (typ & 4) ? 0 : (typ & 3);                  // 4..7 act as edge
      // trigger configuration (type before the source / edge writes is allowed in any order; write both after)
      wr1(8'h5E, {4'd0, lng[0], typ[2:0]});
      wr3(8'h5F, nn);
      wr3(8'h60, hh);
      wr1(8'h16, pol);
      wr1(8'h17, 8'd128);
      if (c_k[cur_c] != 0) wr2(8'h55, lvl16);
      dir = pol ^ (qtyp == 1);
      inv = (c_src[cur_c] == 1);
      sig = inv ^ pol[0];                                // the channel idles LO (pol 0) or HI (pol 1); pulses go the other way
      #(20 * tu + 200);
      arm_fw(64, 256, 1, 32'h00FFFFFF);
      // the stimulus: after the arm (pretrigger filled), or (early) half a burst before it
      if (early) begin
        wait (dut.sample_write_address >= ((c_src[cur_c] == 2) ? 62 : 40));
      end else begin
        wait (dut.sampling_half_way === 1'b1);
        #(3 * tu + (($random(seed) & 255) * 0.01 * tu));
      end
      fork play; join_none
      poll_done(6000);
      wait fork;
      mon_on = 0;
      wr1(8'h0F, 8'h01);
      rd2(8'h14);
      ta = rd16 & 8191;
      check(done == 1, "capture done");
      check(ta == dut.trigger_address, "0x14 over the bus equals trigger_address");
      // rebuild the stream and run the model
      build_stream(qtyp == 0);
      model(qtyp, lng, nn, hh, det);
    end
  endtask

  // position checks after capture(); edge captures calibrate the case
  // ADC: trigger_address = (memory index of the trigger sample) - c, c measured by the edge captures (1 in every case);
  // except on the first armed sample, where trigger_read_address has not started counting yet and trigger_address
  // equals the index (a property of the capture logic since 0x1532, edge type included).
  // EXT: the trigger applies on the first write clock rise after the clock that sets ext_pending, so that clock lies
  // between the writes of words trigger_address and trigger_address + 1 (exact; no calibration needed). ext_pending is
  // set on the clock after the decision for the edge type, two clocks after it for the other types (ext_qev_r).
  integer is_cal, first_arm, exp_off;
  real    p_ns;
  task position(input integer typ, input [8*40-1:0] name);
    begin
      if (c_src[cur_c] != 2) begin
        idx = (qtyp == 0) ? det - 1 : det;                 // memory index of the trigger sample
        t_det = (idx >= 0) ? wt[idx] / 1000.0 - t_start : -1.0;
        first_arm = 0;
        while (first_arm < n_w && !arm_w[first_arm]) first_arm = first_arm + 1;
        if (is_cal) begin
          if (!c_cal_set[cur_c]) begin c_cal[cur_c] = idx - ta; c_cal_set[cur_c] = 1; end
          check(idx - ta == c_cal[cur_c], "edge captures: trigger sample - trigger_address constant");
        end
        exp_off = (idx == first_arm) ? c_cal[cur_c] - 1 : c_cal[cur_c];
        if (!is_cal)
          check(det > 0 && c_cal_set[cur_c] && idx - ta == exp_off, "trigger type at the model's sample (same offset as the edge type)");
        $fdisplay(logf, "RUN case %0d src %0d K %0d D %0d %0s det %0d idx %0d ta %0d off %0d expected %0d first armed %0d", cur_c,
                  c_src[cur_c], c_k[cur_c], c_d[cur_c], name, det, idx, ta, idx - ta, exp_off, first_arm);
      end else begin
        if (det > 0 && t0e + det < n_e) begin
          p_ns  = tz[t0e - 1 + det + ((qtyp == 0) ? 0 : 1)] / 1000.0;
          q     = p_ns - wt[ta] / 1000.0;
          t_det = tz[t0e - 2 + det] / 1000.0 - t_start;
          check(wt[ta] / 1000.0 < p_ns && p_ns < wt[(ta + 1) % NS] / 1000.0,
                "EXT: the deciding clock lies between the writes of words trigger_address and trigger_address + 1");
        end else begin
          q = 1.0e9; t_det = -1.0;
          check(0, "EXT: the model finds the trigger");
        end
        $fdisplay(logf, "RUN case %0d src 2 %0s det %0d clock - write(ta) %0.2f ns, word %0.1f ns", cur_c, name, det, q, 10.0 * (c_d[cur_c] + 1));
      end
    end
  endtask

  // intent: the trigger sample's time lies within [-1.5, +1.5] samples of the intended event (ADC sampling and the
  // pipeline add a fixed latency, measured by the edge calibration as lat)
  real lat [0:15];
  task intent(input [8*60-1:0] what);
    begin
      check(t_det - t_q - lat[cur_c] > -1.5 * tu - 1.0 && t_det - t_q - lat[cur_c] < 1.5 * tu + 1.0, what);
      $fdisplay(logf, "  intent %0s: trigger %0.1f ns, event %0.1f ns, latency %0.1f ns", what, t_det, t_q, lat[cur_c]);
    end
  endtask

  //---------------------------------------------------------------------------------
  // Stimulus builders (times in samples of the trigger stream, tu ns each; j = jitter up to 0.3 sample)

  real cur;
  function real jit(input integer dummy);
    jit = (($random(seed) & 32'h7FFFFFFF) % 1000) * 0.0003;
  endfunction

  task pulse(input real w, input real gap_after);        // pulse of the counted polarity: start, end
    begin
      ev_t[n_ev] = cur * tu; n_ev = n_ev + 1;
      cur = cur + w;
      ev_t[n_ev] = cur * tu; n_ev = n_ev + 1;
      cur = cur + gap_after;
    end
  endtask

  //---------------------------------------------------------------------------------
  // Hold-off cases (CH1 legacy D 9, tu = 100 ns; square wave of period P = 40 samples = 4 us; hold-off 4.5 P)

  localparam real P_NS  = 4000.0;
  localparam      HO_CLK = 3600;                         // 18 us = 4.5 P in 5 ns clocks
  real    dt;
  integer k_ho, idx_e, idx_n;

  // spacing of consecutive triggers against m periods, within one word of the 10 MSa/s write clock (EXT triggers apply
  // on a write clock rise too)
  task spacing(input integer first, input integer last, input real m_per, input [8*100-1:0] what);
    begin
      for (k_ho = first; k_ho < last; k_ho = k_ho + 1) begin
        dt = trig_t[k_ho + 1] - trig_t[k_ho];
        check(dt > m_per * P_NS - 101.0 && dt < m_per * P_NS + 101.0, what);
        $fdisplay(logf, "  holdoff %0s: trigger %0d - %0d = %0.1f ns (expected %0.1f P)", what, k_ho + 1, k_ho, dt, m_per);
      end
    end
  endtask

  // a trigger spacing that shows no hold-off: between 1.5 P and 3.5 P (the record, the restart / re-arm and the
  // pretrigger fill take 1.3 .. 2.5 P)
  task spacing_free(input integer k, input [8*100-1:0] what);
    begin
      dt = trig_t[k + 1] - trig_t[k];
      check(dt > 1.5 * P_NS && dt < 3.5 * P_NS, what);
      $fdisplay(logf, "  holdoff %0s: trigger %0d - %0d = %0.1f ns (no hold-off expected)", what, k + 1, k, dt);
    end
  endtask

  // the sequencer run of the hold-off case: 0x57..0x5C inside the 0x01 window (tb_seg's order)
  task arm_seq(input integer pre_w, input integer tot_w, input integer count, input integer m);
    begin
      wr1(8'h0F, 8'h00);
      wr1(8'h1A, 8'h01);
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05); polls = 0; rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      wr2(8'h0B, pre_w);
      wr2(8'h0C, tot_w);
      wr4(8'h0E, 32'h00FFFFFF);
      wr1(8'h57, 8'h01);
      wr2(8'h58, count);
      wr1(8'h59, m);
      wr4(8'h5C, 32'd0);
      wr1(8'h01, 8'h00);
    end
  endtask

  task holdoff_cases;
    begin
      configure(0);
      wr1(8'h5E, 8'h00); wr1(8'h16, 8'h00); wr1(8'h17, 8'd128);
      inv = 0; sig = 0;
      sq_half = P_NS / 2.0;
      sq_on = 1;
      #(3 * P_NS);

      // HO0: no hold-off: a 4-segment run triggers every second edge (the restart and the pretrigger fill take 1.3 .. 2.5 P)
      wr4(8'h1B, 32'd0);
      n_trig = 0;
      arm_seq(16, 64, 4, 7);
      poll_done(20000);
      check(done == 1 && n_trig == 4, "HO0: 4 segments without hold-off");
      if (n_trig == 4) spacing(0, 3, 2.0, "HO0: segments 2 P apart without hold-off");
      rd1(8'h37);
      check(rd_d[4] == 1'b0, "HO0: 0x37 bit 4 (hold-off running) reads 0 with 0x1B = 0");

      // HO1: hold-off 4.5 P across sequencer restarts: every fifth edge
      wr4(8'h1B, HO_CLK);
      wr1(8'h66, 8'h01);                                 // start clean: the arm's 0x01 = 1 clears any running timer
      n_trig = 0;
      arm_seq(16, 64, 4, 7);
      poll_done(20000);
      check(done == 1 && n_trig == 4, "HO1: 4 segments with hold-off");
      if (n_trig == 4) begin
        spacing(0, 3, 5.0, "HO1: segments 5 P apart (hold-off 4.5 P across sequencer restarts)");
        check(trig_ho[1] && trig_ho[2] && trig_ho[3], "HO1: ho_act set 10 ns after each trigger");
      end
      rd1(8'h37);
      check(rd_d[4] == 1'b1, "HO1: 0x37 bit 4 reads 1 right after the run (the last trigger's hold-off runs)");
      #(HO_CLK * 5 + 1000);
      rd1(8'h37);
      check(rd_d[4] == 1'b0, "HO1: 0x37 bit 4 reads 0 after the hold-off");
      check(dut.ho_run == 1'b0, "HO1: the timer stopped by itself");

      // HO2: across MCU re-arms (sequencer off): two single captures 5 P apart
      wr1(8'h57, 8'h00);
      n_trig = 0;
      arm_fw(16, 64, 1, 32'h00FFFFFF); poll_done(20000); check(done == 1, "HO2: first capture done");
      arm_fw(16, 64, 1, 32'h00FFFFFF); poll_done(20000); check(done == 1, "HO2: second capture done");
      check(n_trig == 2, "HO2: two triggers");
      if (n_trig == 2) spacing(0, 1, 5.0, "HO2: re-armed capture 5 P after the first trigger");

      // HO3: 0x66 = 1, then the arm's 0x01 = 1 clears the timer: the next trigger is the first edge after the arm
      wr1(8'h66, 8'h01);
      arm_fw(16, 64, 1, 32'h00FFFFFF); poll_done(20000); check(done == 1, "HO3: capture done");
      check(n_trig == 3, "HO3: a trigger");
      if (n_trig == 3) spacing_free(1, "HO3: 0x66 then 0x01 = 1 cleared the hold-off");
      check(dut.ho_clr_req == 1'b0, "HO3: the 0x66 request is consumed by the 0x01 = 1");
      // HO3b: the one-shot is consumed: the next re-arm is held off again
      arm_fw(16, 64, 1, 32'h00FFFFFF); poll_done(20000); check(done == 1, "HO3b: capture done");
      check(n_trig == 4, "HO3b: a trigger");
      if (n_trig == 4) spacing(2, 3, 5.0, "HO3b: the arm after the consumed 0x66 is held off again (5 P)");

      // HO4: 0x1B = 0 releases the mask at once (the timer of the last trigger is still running)
      wr4(8'h1B, 32'd0);
      rd1(8'h37);
      check(rd_d[4] == 1'b0, "HO4: 0x37 bit 4 reads 0 right after 0x1B = 0");
      arm_fw(16, 64, 1, 32'h00FFFFFF); poll_done(20000); check(done == 1, "HO4: capture done");
      check(n_trig == 5, "HO4: a trigger");
      if (n_trig == 5) spacing_free(3, "HO4: 0x1B = 0 released the hold-off");

      // HO5: an auto capture inside the hold-off ends untriggered with the 0x153B address
      wr4(8'h1B, HO_CLK);
      arm_fw(16, 64, 1, 32'h00FFFFFF); poll_done(20000); check(done == 1, "HO5: triggered capture done");
      check(n_trig == 6, "HO5: the triggered capture triggered");
      arm_fw(16, 64, 0, 32'd100); poll_done(20000); check(done == 1, "HO5: auto capture done");
      rd1(8'h18);
      check(rd_d[0] == 1'b0 && n_trig == 6, "HO5: the auto capture inside the hold-off is untriggered");
      rd2(8'h14);
      check((rd16 & 8191) == ((dut.w_last - 13'd64 + 13'd16) & 13'h1FFF), "HO5: 0x14 = W - total + pretrigger for the held-off auto capture");
      wr1(8'h66, 8'h01);                                 // the next arm starts clean

      // HO6: EXT edge trigger with the hold-off: two single captures 5 P apart
      configure(5);
      wr1(8'h5E, 8'h00); wr1(8'h16, 8'h00);
      n_trig = 0;
      arm_fw(16, 64, 1, 32'h00FFFFFF); poll_done(20000); check(done == 1, "HO6: EXT first capture done");
      arm_fw(16, 64, 1, 32'h00FFFFFF); poll_done(20000); check(done == 1, "HO6: EXT second capture done");
      check(n_trig == 2, "HO6: two EXT triggers");
      if (n_trig == 2) spacing(0, 1, 5.0, "HO6: EXT re-armed capture 5 P after the first trigger");
      wr1(8'h66, 8'h01);

      // HO7: the pulse-width type (every half-period pulse qualifies: shorter than 30 samples) with the hold-off
      configure(0);
      wr1(8'h5E, 8'h01); wr3(8'h5F, 24'd30); wr3(8'h60, 24'd0); wr1(8'h16, 8'h00);
      n_trig = 0;
      arm_fw(16, 64, 1, 32'h00FFFFFF); poll_done(20000); check(done == 1, "HO7: pulse-width first capture done");
      arm_fw(16, 64, 1, 32'h00FFFFFF); poll_done(20000); check(done == 1, "HO7: pulse-width second capture done");
      check(n_trig == 2, "HO7: two pulse-width triggers");
      if (n_trig == 2) spacing(0, 1, 5.0, "HO7: pulse width re-armed capture 5 P after the first trigger");
      wr1(8'h66, 8'h01);
      wr4(8'h1B, 32'd0);
      wr1(8'h5E, 8'h00);
      sq_on = 0;
      #(P_NS);
      sig = 0;
      #(P_NS);
      // the 0x66 request without a following 0x01 = 1 stays pending: consume it with a reset
      wr1(8'h01, 8'h01); #500; wr1(8'h01, 8'h00); #500;
      check(dut.ho_clr_req == 1'b0 && dut.ho_run == 1'b0, "hold-off cases end clean");
    end
  endtask

  //---------------------------------------------------------------------------------
  // Stop gate: a capture ended on the auto time-out, then a qualifying event; CH1 (case 0) and EXT (case 5), every type

  integer ta0, sg_typ, sg_c;
  reg     st0, ep0;

  task stopgate_case(input integer c, input integer typ);
    begin
      configure(c);
      wr1(8'h5E, typ[7:0]); wr3(8'h5F, 24'd8); wr3(8'h60, 24'd0); wr1(8'h16, 8'h00); wr1(8'h17, 8'd128);
      wr4(8'h1B, 32'd0);
      inv = 0; sig = 0;
      #(10 * tu + 500);
      arm_fw(16, 64, 0, 32'd200);                        // auto, time-out at word 202, no event: untriggered
      poll_done(20000);
      check(done == 1, "SG: auto capture done");
      rd1(8'h18); st0 = rd_d[0];
      rd2(8'h14); ta0 = rd16 & 8191;
      check(st0 == 1'b0, "SG: the time-out capture is untriggered");
      check(ta0 == ((dut.w_last - 13'd64 + 13'd16) & 13'h1FFF), "SG: 0x14 = W - total + pretrigger before the event");
      // the event that would qualify
      n_ev = 0; cur = 2.0;
      case (typ & 3)
        0: begin ev_t[0] = cur * tu; n_ev = 1; end       // a step
        1: pulse(4.0, 10.0);                             // a pulse shorter than N = 8
        2: for (i = 0; i < 10; i = i + 1) pulse(3.0, 3.0);   // 10 edges (N = 8 from the arm)
        default: begin ev_t[0] = cur * tu; n_ev = 1; end // held high for the rest (timeout N = 8)
      endcase
      play;
      #(40 * tu + 500);
      check(dut.sampling_triggered == 1'b0 && dut.ext_pending == 1'b0, "SG: no trigger latched after the stop");
      rd1(8'h18);
      check(rd_d[0] == 1'b0, "SG: 0x18 stays 0 after the event");
      rd2(8'h14);
      check((rd16 & 8191) == ta0, "SG: 0x14 unchanged after the event");
      sig = 0;
      #(10 * tu + 500);
    end
  endtask

  //---------------------------------------------------------------------------------
  // Late write (the contract's "no 0x01 = 1 window" rule, docs/plans/fpga-0x153c.md 3.5): CH1 legacy at divider 999
  // (10 us per word), the type registers written after the 0x05 wait, right before 0x01 = 0, inside the last write clock
  // period of the reset (the 0x1537 race for 0x0B). Nth edge N = 3 after a stale N = 1: the trigger must be the third
  // edge. gen8's reset (cap_rst only) kept the stale N here; q_hr_adc loads the new one on the edge that writes word 0.

  real lat_lw;
  task lw_arm(input integer typ, input integer nn_late);
    begin
      wr1(8'h0F, 8'h00);
      wr1(8'h1A, 8'h01);
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05); polls = 0; rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      wr2(8'h0B, 64);
      wr2(8'h0C, 256);
      wr4(8'h0E, 32'h00FFFFFF);
      wr1(8'h5E, typ[7:0]);                              // the late writes: type and N inside the window
      wr3(8'h5F, nn_late);
      n_w = 0; n_e = 0;
      mon_on = 1;
      wr1(8'h01, 8'h00);
      wait (dut.sampling_half_way === 1'b1);
      #(3 * tu + (($random(seed) & 255) * 0.01 * tu));
      fork play; join_none
      poll_done(60000);
      wait fork;
      mon_on = 0;
      wr1(8'h0F, 8'h01);
      rd2(8'h14);
      ta = rd16 & 8191;
      check(done == 1, "late write: capture done");
    end
  endtask

  task latewrite_case;
    begin
      configure(0);
      wr4(8'h0D, 32'd999);
      tu = 10000.0;
      wr1(8'h16, 8'h00); wr1(8'h17, 8'd128); wr3(8'h60, 24'd0); wr4(8'h1B, 32'd0);
      inv = 0; sig = 0; dir = 0;
      #(20 * tu);
      // edge calibration at this rate (the latency in ns scales with the word period)
      wr1(8'h5E, 8'h00); wr3(8'h5F, 24'd1);
      qtyp = 0;
      n_ev = 0; cur = 2.0; ev_t[0] = cur * tu; n_ev = 1; t_q = cur * tu;
      lw_arm(0, 1);
      build_stream(1); model(0, 0, 1, 0, det);
      idx = det - 1; t_det = (idx >= 0) ? wt[idx] / 1000.0 - t_start : -1.0;
      check(det > 0 && idx - ta == c_cal[0], "late write: edge capture at D 999 at the calibrated offset");
      lat_lw = t_det - t_q;
      sig = 0;
      #(5 * tu);
      // stale N = 1 in the register, N = 3 written inside the window: the third edge
      wr3(8'h5F, 24'd1);
      qtyp = 2;
      n_ev = 0; cur = 1.0;
      for (kk = 0; kk < 5; kk = kk + 1) begin if (kk == 2) t_q = cur * tu; pulse(4.0, 6.0); end
      lw_arm(2, 3);
      build_stream(0); model(2, 0, 3, 0, det);
      idx = det; t_det = (idx >= 0) ? wt[idx] / 1000.0 - t_start : -1.0;
      check(det > 0 && idx - ta == c_cal[0], "late write: Nth edge at the model's sample");
      check(t_det - t_q - lat_lw > -1.5 * tu - 1.0 && t_det - t_q - lat_lw < 1.5 * tu + 1.0,
            "late write: N written inside the 0x01 window applies (third edge, not the stale first)");
      $fdisplay(logf, "RUN late write D 999: edge latency %0.1f ns, Nth det %0d idx %0d ta %0d, trigger %0.1f ns, event %0.1f ns",
                lat_lw, det, idx, ta, t_det, t_q);
      sig = 0;
      wr1(8'h5E, 8'h00);
      wr4(8'h0D, c_d[0]);
      tu = 10.0 * (c_d[0] + 1);
      #(20 * tu);
    end
  endtask

  integer typ, pol, lng, nn, hh, r, nruns, kq, kk, c, early;
  integer type_runs = 0;
  integer only;

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    if (!$value$plusargs("runs=%d", nruns)) nruns = 2;
    if (!$value$plusargs("logfile=%s", logname)) logname = "trigtypes.log";
    logf = $fopen(logname, "w");
    $display("tb_trigtypes: seed %0d, %0d runs per type and case", seed, nruns);
    #300;
    wr1(8'h50, 8'h00);
    wr1(8'h57, 8'h00);
    rd2(8'h06);
    $display("version %h", rd16);
    check(rd16[15:8] == 8'h15 && rd16[7:0] >= 8'h3B, "cmd 0x06 reads 0x153B or later");

    add_case(0, 0, 9);                                   // CH1 legacy 10 MSa/s
    add_case(1, 0, 9);                                   // CH2 legacy 10 MSa/s
    add_case(0, 0, 0);                                   // CH1 legacy 200 MSa/s (stream 100 MSa/s, core A)
    add_case(0, 1, 19);                                  // CH1 boxcar N = 20
    add_case(1, 2, 1);                                   // CH2 CIC2 N = 2
    add_case(2, 0, 9);                                   // EXT legacy 10 MSa/s
    add_case(2, 1, 19);                                  // EXT with K = 1

    if (!$value$plusargs("only=%d", only)) only = -1;
    for (c = 0; c < nc; c = c + 1) if (only < 0 || only == c) begin
      configure(c);
      // edge calibration: single steps, both polarities
      is_cal = 1;
      lat[c] = 0.0;
      for (r = 0; r < 3; r = r + 1)
        for (pol = 0; pol < 2; pol = pol + 1) begin
          n_ev = 0; cur = 2.0 + jit(0);
          ev_t[0] = cur * tu; n_ev = 1;                  // one step away from the idle level
          t_q = cur * tu;
          capture(0, pol, 0, 1, 0, 0);
          position(0, "edge");
          if (r == 0 && pol == 0) lat[c] = t_det - t_q;
          check(det > 0 && t_det - t_q - lat[c] > -1.5 * tu - 1.0 && t_det - t_q - lat[c] < 1.5 * tu + 1.0,
                "edge: trigger at the step (latency constant within a sample)");
          sig = inv;
        end
      is_cal = 0;
      $display("case %0d (src %0d K %0d D %0d): edge offset %0d, latency %0.1f ns", c, c_src[c], c_k[c], c_d[c], c_cal[c], lat[c]);

      nn = (c_src[c] == 2) ? 60 : 8;
      for (r = 0; r < nruns; r = r + 1)
        for (pol = 0; pol < 2; pol = pol + 1) begin
          // pulse width, shorter than N: pulses N + 4, N + 2.5, then N - 3 (qualifies), then N - 3 again
          n_ev = 0; cur = 1.0 + jit(0);
          pulse(nn + 4 + jit(0), 2 * nn); pulse(nn + 2.5 + jit(0), 2 * nn);
          pulse(nn - 3 + jit(0), 0.0); t_q = cur * tu; cur = cur + 2 * nn; pulse(nn - 3, 2 * nn);
          capture(1, pol, 0, nn, 0, 0); position(1, "pw<N"); intent("pulse width < N: the trailing edge of pulse 3");
          sig = inv;
          // pulse width, longer than N: N - 4, N - 2.5, then N + 3 (qualifies)
          n_ev = 0; cur = 1.0 + jit(0);
          pulse(nn - 4 + jit(0), 2 * nn); pulse(nn - 2.5 + jit(0), 2 * nn);
          pulse(nn + 3 + jit(0), 0.0); t_q = cur * tu; cur = cur + 2 * nn; pulse(nn + 3, 2 * nn);
          capture(1, pol, 1, nn, 0, 0); position(1, "pw>N"); intent("pulse width > N: the trailing edge of pulse 3");
          sig = inv;
          // pulse width near the threshold (model only): widths N - 1.5 .. N + 1.5 at random
          n_ev = 0; cur = 1.0;
          for (kk = 0; kk < 6; kk = kk + 1) pulse(nn - 1.5 + (($random(seed) & 32'h7FFFFFFF) % 3000) * 0.001, nn + 2);
          pulse(nn - 4, nn + 2); pulse(nn + 4, nn + 2);
          lng = $random(seed) & 1;
          capture(1, pol, lng, nn, 0, 0); position(1, lng ? "pw>N near" : "pw<N near");
          check(det > 0, "near-threshold pulse width: the model finds a trigger");
          sig = inv;
          // Nth edge, idle time 0, N = 3: edges counted from the arm
          kq = 3;
          n_ev = 0; cur = 1.0 + jit(0);
          for (kk = 0; kk < 5; kk = kk + 1) begin if (kk == kq - 1) t_q = cur * tu; pulse(4 + jit(0), 6); end
          capture(2, pol, 0, kq, 0, 0); position(2, "Nth N=3 H=0"); intent("Nth edge (3) from the arm");
          sig = inv;
          // the same with the train starting before the arm (model only: edges before the arm do not count)
          n_ev = 0; cur = 1.0 + jit(0);
          for (kk = 0; kk < 30; kk = kk + 1) pulse(4 + jit(0), 6);
          capture(2, pol, 0, kq, 0, 1); position(2, "Nth N=3 H=0 early");
          sig = inv;
          // Nth edge with an idle time: a burst already running at the arm (counted from its own start), the trigger is
          // the Nth edge of that burst if it comes after the arm, else of the next burst. Bursts of 6 pulses, gaps of 3,
          // idle 40 between bursts, idle time H = 20
          kq = 4; hh = 20;
          n_ev = 0; cur = 1.0;
          for (kk = 0; kk < 12; kk = kk + 1) begin if (kk == kq - 1) t_q = cur * tu; pulse(3 + jit(0), (kk == 5) ? 40 : 3); end
          capture(2, pol, 0, kq, hh, 0); position(2, "Nth N=4 H=20"); intent("Nth edge (4) of the first burst after the idle level");
          sig = inv;
          // bursts running across the arm (model only: a burst counts from its start, also before the arm)
          n_ev = 0; cur = 1.0;
          for (kk = 0; kk < 30; kk = kk + 1) pulse(3 + jit(0), (kk % 6 == 5) ? 40 : 3);
          capture(2, pol, 0, kq, hh, 1); position(2, "Nth N=4 H=20 early");
          sig = inv;
          // Nth edge with N = 1 and H = 0 equals the edge type on the same stimulus (same memory index)
          n_ev = 0; cur = 2.0 + jit(0);
          pulse(5, 10); pulse(5, 10);
          capture(2, pol, 0, 1, 0, 0); position(2, "Nth N=1");
          idx_n = (c_src[c] == 2) ? det : det;
          if (c_src[c] != 2) begin build_stream(1); model(0, 0, 1, 0, det); idx_e = det - 1; end
          else begin qtyp = 0; build_stream(1); model(0, 0, 1, 0, det); idx_e = det; qtyp = 2; end
          check(idx_n == idx_e, "Nth edge N = 1 fires on the sample the edge type fires on");
          sig = inv;
          // timeout: held pulses of N - 3, N - 1.5, then one of 3 N: the trigger N + 1 samples after its edge
          n_ev = 0; cur = 1.0 + jit(0);
          pulse(nn - 3 + jit(0), nn); pulse(nn - 1.5 + jit(0), nn);
          t_q = (cur + nn) * tu; pulse(3 * nn, nn);
          capture(3, pol, 0, nn, 0, 0); position(3, "timeout"); intent("timeout: N samples after the edge");
          sig = inv;
          type_runs = type_runs + 9;
        end
      // reserved types 4 and 7 act as the edge type (position against the edge calibration); 0x37 bit 3 = type bit 2
      for (pol = 0; pol < 2; pol = pol + 1) begin
        n_ev = 0; cur = 2.0 + jit(0);
        ev_t[0] = cur * tu; n_ev = 1; t_q = cur * tu;
        capture((pol == 0) ? 4 : 7, pol, 0, 8, 0, 0); position(0, "reserved type");
        intent("reserved type (4 / 7): trigger at the step like the edge type");
        rd1(8'h37);
        check(rd_d[3] == 1'b1 && rd_d[4] == 1'b0, "read 0x37: type bit 2 set for a reserved type, hold-off off");
        sig = inv;
      end
      $display("case %0d done, %0d checks so far, %0d failed", c, checks, errors);
    end

    // the status reads: type, flags and the edges still to count (Nth edge, N = 5) after a capture that triggered
    configure(0);
    wr1(8'h5E, 8'h02); wr3(8'h5F, 24'd5); wr3(8'h60, 24'd0);
    rd2(8'h34);
    wr_cmd(8'h34); rd_byte; check(rd_d[1:0] == 2'd2 && rd_d[2] == 1'b0 && rd_d[3] == 1'b0, "read 0x34: type and source read back");
    wr1(8'h5E, 8'h09);                                   // pulse width, longer (bit 3)
    wr_cmd(8'h34); rd_byte; check(rd_d[1:0] == 2'd1 && rd_d[2] == 1'b1, "read 0x34: longer (0x5E bit 3) read back in bit 2");
    rd1(8'h37);
    check(rd_d == 8'h00, "read 0x37: 0 for a type below 4 with the hold-off off");
    wr1(8'h15, 8'd2);
    wr_cmd(8'h34); rd_byte; check(rd_d[3] == 1'b1, "read 0x34: source EXT read back");
    check(dut.trigger_ext == 1'b0, "EXT with a qualified type leaves the EXT edge path off");
    wr1(8'h5E, 8'h00);
    check(dut.trigger_ext == 1'b1, "0x5E = 0 restores the EXT edge path");
    check(dut.trigger_edge == dut.trig_pol, "0x5E = 0 restores the detect direction");
    wr1(8'h15, 8'd0);

    // late write, hold-off and stop-gate cases
    latewrite_case;
    holdoff_cases;
    for (sg_c = 0; sg_c < 2; sg_c = sg_c + 1)
      for (sg_typ = 0; sg_typ < 4; sg_typ = sg_typ + 1)
        stopgate_case((sg_c == 0) ? 0 : 5, sg_typ);
    $display("hold-off and stop-gate cases done, %0d checks so far, %0d failed", checks, errors);

    $fclose(logf);
    $display("type captures: %0d", type_runs);
    $display("RESULT: %0d checks, %0d failed -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #3_000_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
