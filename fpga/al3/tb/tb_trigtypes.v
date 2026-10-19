// Trigger types testbench (fpga/al3/src/zaklad.v, 0x153A): pulse width, Nth edge and timeout on CH1, CH2 and EXT,
// legacy and decimated. Run with fpga/al3/tb/run.sh (ONLY=trigtypes) or vvp (+seed, +runs).
//
// One instance, the AD9288 model and the MCU byte sequences of tb_trigpos.v (pc_hal_arm and the poll). For every
// capture the testbench rebuilds the trigger stream the FPGA saw since the release of 0x01:
//   - ADC sources: the reset value of trigger_data (code 0) followed by every word written to the sample memory (core A
//     of the channel, or its 16-bit sample with K >= 1); the memory holds the whole stream because a capture writes
//     fewer than 8192 words. The arm flag of each sample (sampling_half_way after the write clock edge that loaded it)
//     is logged by a monitor.
//   - EXT: ext_level and sampling_half_way at every clk_200MHz edge since cap_rst200 fell.
// A reference model (task model below, written from the definitions in fpga/al3/README.md "Changes" section 10) finds
// the sample on which each trigger type fires. The checks:
//   1. exact position: ADC: (memory index of the trigger sample) - trigger_address is the same constant as for edge
//      captures of the same case (measured in the same run); EXT: the time from the clock that decides the trigger to
//      the write of word trigger_address lies in the window the EXT edge captures span (less than one word);
//   2. intent: the stimulus places the qualifying event at a known instant (the trailing edge of the k-th pulse, the
//      Nth edge of a burst, the moment a level has been held for N samples), with margins of 2 samples or more around
//      every threshold; the trigger sample found by the model must be that event (within one sample);
//   3. the Nth edge with N = 1 and no idle time triggers exactly where the edge type does (same model index).
// Boundary cases (pulse widths of N - 1, N, N + 1 samples) come from randomised pulses near the thresholds and are
// checked by the model only.
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
  // Monitors: the ADC trigger stream's arm flags and write times, the EXT stream

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

  //---------------------------------------------------------------------------------
  // One capture

  integer ta, det, idx, i, done, polls, t0e, dsamp;
  real    q, t_det;
  reg     dir;

  task capture(input integer typ, input integer pol, input integer lng, input integer nn, input integer hh,
               input integer early);
    begin
      // trigger configuration (type before the source / edge writes is allowed in any order; write both after)
      wr1(8'h5E, {5'd0, lng[0], typ[1:0]});
      wr3(8'h5F, nn);
      wr3(8'h60, hh);
      wr1(8'h16, pol);
      wr1(8'h17, 8'd128);
      if (c_k[cur_c] != 0) wr2(8'h55, lvl16);
      dir = pol ^ (typ == 1);
      inv = (c_src[cur_c] == 1);
      sig = inv ^ pol[0];                                // the channel idles LO (pol 0) or HI (pol 1); pulses go the other way
      #(20 * tu + 200);
      // arm (pc_hal_arm)
      wr1(8'h0F, 8'h00);
      wr1(8'h1A, 8'h01);
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05); polls = 0; rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      wr2(8'h0B, 13'd64);
      wr2(8'h0C, 13'd256);
      wr4(8'h0E, 32'h00FFFFFF);
      n_w = 0; n_e = 0;
      mon_on = 1;
      wr1(8'h01, 8'h00);
      // the stimulus: after the arm (pretrigger filled), or (early) half a burst before it
      if (early) begin
        wait (dut.sample_write_address >= ((c_src[cur_c] == 2) ? 62 : 40));
      end else begin
        wait (dut.sampling_half_way === 1'b1);
        #(3 * tu + (($random(seed) & 255) * 0.01 * tu));
      end
      fork play; join_none
      polls = 0; done = 0;
      while (!done && polls < 6000) begin rd1(8'h0A); done = rd_d[0]; polls = polls + 1; end
      wait fork;
      mon_on = 0;
      wr1(8'h0F, 8'h01);
      rd2(8'h14);
      ta = rd16 & 8191;
      check(done == 1, "capture done");
      check(ta == dut.trigger_address, "0x14 over the bus equals trigger_address");
      // rebuild the stream and run the model
      if (c_src[cur_c] != 2) begin
        mn = n_w + 1;
        ma[0] = ~dir;                                    // the reset value 0 of trigger_data: below the level
        marm[0] = 0; darm[0] = 0;
        for (i = 0; i < n_w; i = i + 1) begin
          if (c_k[cur_c] == 0)
            ma[i + 1] = ((((c_src[cur_c] == 1) ? dut.samples.mem[i][23:16] : dut.samples.mem[i][7:0]) >= 8'd128) ~^ dir);
          else
            ma[i + 1] = ((((c_src[cur_c] == 1) ? dut.samples.mem[i][31:16] : dut.samples.mem[i][15:0]) >= lvl16) ~^ dir);
          marm[i + 1] = arm_w[i];
          darm[i + 1] = arm_w[i];
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
          darm[i] = (typ == 0) ? ((t0e - 1 + i < n_e && t0e - 1 + i >= 0) ? arm_e[t0e - 1 + i] : 1'b0)
                               : ((t0e + i < n_e) ? arm_e[t0e + i] : 1'b0);
        end
      end
      model(typ, lng, nn, hh, det);
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
        idx = det - 1;                                     // memory index of the trigger sample
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
          p_ns  = tz[t0e - 1 + det + ((typ == 0) ? 0 : 1)] / 1000.0;
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

  integer typ, pol, lng, nn, hh, r, nruns, kq, kk, c, early;
  integer type_runs = 0;
  integer only;

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    if (!$value$plusargs("runs=%d", nruns)) nruns = 2;
    logf = $fopen("trigtypes.log", "w");
    $display("tb_trigtypes: seed %0d, %0d runs per type and case", seed, nruns);
    #300;
    wr1(8'h50, 8'h00);
    wr1(8'h57, 8'h00);
    rd2(8'h06);
    $display("version %h", rd16);
    check(rd16 == 16'h153A, "cmd 0x06 reads 0x153A");

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
          // Nth edge with N = 1 and H = 0 equals the edge type on the same stimulus
          n_ev = 0; cur = 2.0 + jit(0);
          pulse(5, 10); pulse(5, 10);
          capture(2, pol, 0, 1, 0, 0); position(2, "Nth N=1");
          kk = det;
          model(0, 0, 1, 0, det);
          check(kk == det, "Nth edge N = 1 fires on the sample the edge type fires on");
          sig = inv;
          // timeout: held pulses of N - 3, N - 1.5, then one of 3 N: the trigger N + 1 samples after its edge
          n_ev = 0; cur = 1.0 + jit(0);
          pulse(nn - 3 + jit(0), nn); pulse(nn - 1.5 + jit(0), nn);
          t_q = (cur + nn) * tu; pulse(3 * nn, nn);
          capture(3, pol, 0, nn, 0, 0); position(3, "timeout"); intent("timeout: N samples after the edge");
          sig = inv;
          type_runs = type_runs + 9;
        end
      $display("case %0d done, %0d checks so far, %0d failed", c, checks, errors);
    end

    // the status read: type, flags and the edges still to count (Nth edge, N = 5) after a capture that triggered
    configure(0);
    wr1(8'h5E, 8'h02); wr3(8'h5F, 24'd5); wr3(8'h60, 24'd0);
    rd2(8'h34);
    wr_cmd(8'h34); rd_byte; check(rd_d[1:0] == 2'd2 && rd_d[3] == 1'b0, "read 0x34: type and source read back");
    wr1(8'h5E, 8'h00);

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
