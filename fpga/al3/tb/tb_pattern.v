// Pattern generator and pattern trigger engine testbench (fpga/al3/src/zaklad.v, 0x153D D2; docs/plans/fpga-0x153c.md
// sections 7.2, 7.4, 7.5 and 9.1 step 18). Run with fpga/al3/tb/run.sh (ONLY=pattern) or vvp (+seed, +logfile).
// One instance, the AD9288 model and the MCU byte sequences of tb_trigtypes.v. The calibration pad is monitored for
// every edge (time and level) while the generator drives it; the tb drives it only for the engine's EXT source.
//   G. The generator on 0x50 alone (nothing else written): the pin is driven low (the idle level) with 0x50 = 1, hi-Z
//      with 0x50 = 0, 0x51 / 0x52 writes change nothing; idle level 1 through 0x6D bit 2; 0x28 bit 1.
//   S. The square wave, programmed with the firmware's SET_GEN sequence (P16 / H16 of section 7.4: entry 0 high, entry 1
//      low with end, 0x6D = 0x33 to enable, 0x23 for a change) at 1 kHz, 999.5 Hz, 10 kHz 25 %, 1 MHz, 10 MHz, 7.3 MHz
//      and the prototype's 3.5 + 2.25 clocks: every edge lands within one 5 ns clock of its ideal time (the cumulative
//      actual time is floor(ideal): 0 .. 1 clock early), the mean period and high time over the periods equal P16 / 65536
//      and H16 / 65536 clocks within 1 / periods.
//   B. The double buffer: a frequency / duty change on the running square (0x6D = 0x23): no glitch (every step is a
//      complete old or new step), the pending bit reads 1 until the next boundary and 0 after, the new values apply from
//      the second boundary (the queued step completes), the accumulator is not cleared (the first new step's carry comes
//      from the running accumulator); a commit overwritten by a second commit before a boundary: the first values never
//      appear.
//   P. Bursts: a 5-step table with alternating levels, mode 1 (one burst, then idle), mode 2 with R = 3 and a gap (R
//      bursts, R - 1 gaps, exact lengths), mode 2 with gap 0, mode 3 (continuous, stopped by mode 0 at once), a start
//      during a burst ignored, 0x39 (running, in gap, step, bursts, pending, mode active), idle level 1, entry 7 as end,
//      index >= 8 ignored; the accumulator cleared at every start (two bursts of fractional steps edge for edge).
//   M. The marker as the capture's trigger source (0x15 = 3 with 0x6D bit 3): a normal capture triggers at the marker
//      with the EXT position convention (the marker clock between the writes of words 0x14 and 0x14 + 1); without bit 3,
//      or with 0x15 = 2, no trigger; 0x39 byte 3 bit 1.
//   E. The pattern trigger engine: every type on CH1 (legacy and K = 2), CH2 and EXT against tb_trigtypes.v's reference
//      model run on the stream the engine saw (logged at its ce clocks): the burst starts 6 clocks after the event
//      clock, i.e. 37.5 ns after the qualifying sample's write edge (CH) or about 55 ns after the pad edge (EXT);
//      one shot and re-arm, 0x42, the start sources (MCU / engine / both), the capture triggering on CH1 while the
//      engine fires on CH2's third edge (own engine).
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

  reg  sig = 0;                                         // CH1 HI when 1, CH2 its complement, EXT pad = sig when ext_en
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
  // Pad edge monitor (while the generator drives the pad)

  localparam NEDGE = 4096;
  real    e_t [0:NEDGE - 1];                            // time of edge i (ns)
  reg     e_v [0:NEDGE - 1];                            // level after it
  integer n_edge = 0;
  reg     mon_pad = 0;
  reg     pad_q = 0;

  // the generator's level (what the pad carries while 0x50 = 1; the EXT engine case needs the pad for the stimulus)
  wire gen_lvl = dut.pat_level;
  always @(gen_lvl) begin
    #0.001;
    if (mon_pad && gen_lvl !== pad_q && n_edge < NEDGE) begin
      e_t[n_edge] = $realtime - 0.001;
      e_v[n_edge] = gen_lvl;
      n_edge = n_edge + 1;
    end
    pad_q = gen_lvl;
  end

  // the pad follows the level whenever the pin is driven
  integer pin_err = 0;
  always @(posedge dut.clk_200MHz) if (mon_pad && dut.gen_out_enable && pad !== gen_lvl) pin_err = pin_err + 1;

  task mon_start;
    begin
      pad_q = gen_lvl; n_edge = 0; mon_pad = 1;
    end
  endtask

  // write time of word i (ps), for the marker position check
  localparam NS = 8192, NE = 65536;
  reg [63:0] wt [0:NS - 1];
  reg        mon_w = 0;
  reg        r_adv, r_we, r_rst;
  integer    r_addr;
  always @(posedge dut.sample_write_clock) begin
    r_adv = dut.swc_adv; r_we = dut.sample_write_enable; r_rst = dut.cap_rst; r_addr = dut.sample_write_address;
    #0.1;
    if (mon_w && !r_rst && r_adv && r_we && r_addr < NS)
      wt[r_addr] = $realtime * 1000.0;
  end

  // the engine's stream: a at every ce clock after its reset
  reg        ma   [0:NE];
  reg        marm [0:NE];
  reg        darm [0:NE];
  reg [63:0] tz   [0:NE];
  integer    mn = 0;
  reg        mon_q = 0;
  reg        r_ce, r_a, r_qrst;
  always @(posedge dut.clk_200MHz) begin
    r_ce = dut.pq_ce; r_a = dut.pq_a; r_qrst = dut.pq_rst;
    #0.1;
    if (mon_q && r_ce && !r_qrst && mn < NE) begin
      ma[mn] = r_a; marm[mn] = 1; darm[mn] = 1; tz[mn] = $realtime * 1000.0;
      mn = mn + 1;
    end
  end

  // marker instants
  real    t_mark = -1.0;
  integer wa_mark = -1, n_mark = 0;
  always @(posedge dut.pat_marker) begin
    t_mark = $realtime; wa_mark = dut.sample_write_address; n_mark = n_mark + 1;
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
  reg [31:0] rd32;
  task rd4(input [7:0] c);
    begin wr_cmd(c); rd_byte; rd32[31:24] = rd_d; rd_byte; rd32[23:16] = rd_d; rd_byte; rd32[15:8] = rd_d; rd_byte; rd32[7:0] = rd_d; end
  endtask

  // pattern step: 0x6C, 7 bytes
  task pstep(input integer idx, input lvl, input mark, input fin, input [31:0] n, input [15:0] f);
    begin
      wr_cmd(8'h6C);
      wr_byte({fin, mark, lvl, idx[4:0]});
      wr_byte(n[31:24]); wr_byte(n[23:16]); wr_byte(n[15:8]); wr_byte(n[7:0]);
      wr_byte(f[15:8]); wr_byte(f[7:0]);
    end
  endtask

  // the firmware's SET_GEN arithmetic (contract 7.4): f in mHz, d in basis points
  reg [63:0] P16, H16, L16;
  reg [31:0] n_h, n_l;
  reg [15:0] f_h, f_l;
  task gen_calc(input [63:0] f_mhz, input [63:0] d_bp);
    begin
      P16 = (64'd65536 * 64'd200000000000 + f_mhz / 2) / f_mhz;
      H16 = (P16 * d_bp + 5000) / 10000;
      L16 = P16 - H16;
      n_h = H16 >> 16; f_h = H16[15:0];
      n_l = L16 >> 16; f_l = L16[15:0];
    end
  endtask
  task gen_write(input enable);                        // entries 0 / 1 and the commit (fw-d5's sequences)
    begin
      pstep(0, 1'b1, 1'b0, 1'b0, n_h, f_h);
      pstep(1, 1'b0, 1'b0, 1'b1, n_l, f_l);
      wr1(8'h6D, enable ? 8'h33 : 8'h23);              // mode 3 | commit (| start when enabling)
    end
  endtask

  //---------------------------------------------------------------------------------
  // Square-wave checks on the recorded edges: edge 0 is the rising edge that starts a period (or the burst), edge i's
  // ideal time is t0 + (periods * P16 + (i odd ? H16 : 0)) / 65536 clocks; the actual time is the floor of the ideal
  // cumulative clock count: 0 .. 1 clock early. Also the mean period / high time.

  real    dev, dev_min, dev_max, t_ideal, mean_p, mean_h, exp_p, exp_h;
  integer first_rise, i, np;
  real    worst_dev_all = 0.0;

  // fresh = 1: the run started at the first rising edge with the accumulator cleared, so the ideal times are absolute
  // (0 .. 1 clock early); fresh = 0: the pattern was changed on the run, the accumulator carries a residual r, so every
  // edge lies within (r - 1, r] clocks: the span of the deviations is under one clock
  task square_check(input integer periods, input [8*60-1:0] name, input fresh);
    begin
      // find the first rising edge
      first_rise = -1;
      for (i = 0; i < n_edge && first_rise < 0; i = i + 1) if (e_v[i] == 1'b1) first_rise = i;
      check(first_rise >= 0 && n_edge >= first_rise + 2 * periods + 1, "square: enough edges recorded");
      if (first_rise >= 0 && n_edge >= first_rise + 2 * periods + 1) begin
        dev_min = 1.0e9; dev_max = -1.0e9;
        for (i = 1; i <= 2 * periods; i = i + 1) begin
          np = i / 2;
          t_ideal = e_t[first_rise] + (np * P16 + ((i % 2) ? H16 : 64'd0)) * 5.0 / 65536.0;
          dev = e_t[first_rise + i] - t_ideal;
          if (dev < dev_min) dev_min = dev;
          if (dev > dev_max) dev_max = dev;
          check(e_v[first_rise + i] == ((i % 2) ? 1'b0 : 1'b1), "square: edge polarity alternates");
        end
        if (fresh) check(dev_min > -5.0 - 0.001 && dev_max < 0.001, "square: every edge within one clock of ideal (0 .. 1 clock early)");
        check(dev_max - dev_min < 5.0 + 0.001, "square: the edge deviations span less than one clock");
        if (dev_max - dev_min > worst_dev_all) worst_dev_all = dev_max - dev_min;
        mean_p = (e_t[first_rise + 2 * periods] - e_t[first_rise]) / periods;
        exp_p  = P16 * 5.0 / 65536.0;
        mean_h = 0.0;
        for (i = 0; i < periods; i = i + 1) mean_h = mean_h + (e_t[first_rise + 2 * i + 1] - e_t[first_rise + 2 * i]);
        mean_h = mean_h / periods;
        exp_h  = H16 * 5.0 / 65536.0;
        check(mean_p > exp_p - 5.0 / periods - 0.001 && mean_p < exp_p + 5.0 / periods + 0.001, "square: mean period = P16 / 65536 clocks within 1 / periods");
        check(mean_h > exp_h - 5.0 / periods - 0.001 && mean_h < exp_h + 5.0 / periods + 0.001, "square: mean high time = H16 / 65536 clocks within 1 / periods");
        $display("  %0s: %0d periods, P16 %0d (%0d + %0d/65536 clocks), H16 %0d: mean period %0.4f ns (ideal %0.4f), mean high %0.4f ns (ideal %0.4f), edge deviation %0.3f .. %0.3f ns",
                 name, periods, P16, P16 >> 16, P16[15:0], H16, mean_p, exp_p, mean_h, exp_h, dev_min, dev_max);
        $fdisplay(logf, "SQUARE %0s periods %0d P16 %0d H16 %0d mean_p %0.5f ideal %0.5f mean_h %0.5f ideal %0.5f dev %0.3f %0.3f",
                  name, periods, P16, H16, mean_p, exp_p, mean_h, exp_h, dev_min, dev_max);
      end
    end
  endtask

  // wait for n edges on the pad (with a time limit, ns)
  task wait_edges(input integer n, input real limit);
    real t_lim;
    begin
      t_lim = $realtime + limit;
      while (n_edge < n && $realtime < t_lim) #5;
    end
  endtask

  //---------------------------------------------------------------------------------
  // Reference model of tb_trigtypes.v (unchanged): a[k] = processed sample k is on the pre-detect side. Returns the data
  // index of the first trigger (the sample after the deciding one), or -1.

  task model(input integer typ, input integer lng, input integer nn, input integer hh, output integer det);
    integer k, run_pre, run_post, vld, ap, cnt, cv, past, gap, lastt, fire, n1, neff;
    begin
      det = -1;
      run_pre = 0; run_post = 0; vld = 0; ap = 0; cnt = 0; past = 0; lastt = -1;
      cv = (hh == 0);
      n1 = (nn % 65536 == 0) ? 0 : (nn % 65536) - 1;
      neff = (nn == 0) ? 1 : nn;
      for (k = 0; k < mn && det < 0; k = k + 1) begin
        gap = (hh != 0) && (k - 1 - lastt >= hh - 1);
        case (typ)
          0: fire = ma[k];
          1: fire = ma[k] && vld && (lng ? (run_pre >= nn) : (run_pre <= nn - 2));
          2: fire = ma[k] && (cv || gap) && ((gap ? 0 : cnt) == n1) && !(past && !gap);
          default: fire = !ma[k] && (run_post == neff - 1);
        endcase
        if (k + 1 < mn && fire && !ma[k + 1] && darm[k + 1]) det = k + 1;
        if (ap && !ma[k] && (cv || gap) && (hh == 0 ? marm[k] : 1)) begin
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
  // Stimulus (as tb_trigtypes.v): level changes of sig at times relative to the start

  integer n_ev;
  real    ev_t [0:63];
  real    t_start, t_q, cur, tu;
  reg     inv = 0, dir = 0;

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

  function real jit(input integer dummy);
    jit = (($random(seed) & 32'h7FFFFFFF) % 1000) * 0.0003;
  endfunction

  task pulse(input real w, input real gap_after);
    begin
      ev_t[n_ev] = cur * tu; n_ev = n_ev + 1;
      cur = cur + w;
      ev_t[n_ev] = cur * tu; n_ev = n_ev + 1;
      cur = cur + gap_after;
    end
  endtask

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
  // Engine cases: source (0 CH1, 1 CH2, 2 EXT), K, divider

  integer    c_src, c_k, c_d;
  reg [5:0]  sh;
  reg [15:0] lvl16;

  task configure(input integer src, input integer k, input integer d);
    begin
      c_src = src; c_k = k; c_d = d;
      wr4(8'h0D, d);
      sh = (k == 0) ? 6'd0 : shift_for(k, 2 * ((d / 2) + 1));
      wr1(8'h53, k);
      wr1(8'h56, 8'h00);
      wr1(8'h54, {2'd0, sh});
      if (k != 0) #((k + 10) * ((d / 2) + 1) * 10 + 1000);
      lvl16 = (out16(LO, k, d, sh) + out16(HI, k, d, sh)) / 2;
      wr1(8'h76, (src == 2) ? 8'd2 : src[7:0]);
      wr1(8'h50, (src == 2) ? 8'h00 : 8'h01);           // EXT: the pad is the tb's input, the burst is watched on pat_level
      wr1(8'h78, 8'd128);
      wr2(8'h79, lvl16);
      ext_en = (src == 2);
      tu = (src == 2) ? 5.0 : (k == 0) ? 10.0 * (d + 1) : 10.0 * ((d / 2) + 1);
    end
  endtask

  // the engine's trigger configuration and arm (fw-d5's sequence: 0x7D = 0, config, 0x7D = armed | rearm | clear)
  integer qtyp;
  task engine_arm(input integer typ, input integer pol, input integer lng, input integer nn, input integer hh, input rearm);
    begin
      qtyp = (typ & 4) ? 0 : (typ & 3);
      wr1(8'h7D, 8'h00);
      wr1(8'h77, pol[7:0]);
      wr1(8'h7A, {4'd0, lng[0], typ[2:0]});
      wr3(8'h7B, nn);
      wr3(8'h7C, hh);
      dir = pol ^ (qtyp == 1);
      inv = (c_src == 1);
      sig = inv ^ pol[0];
      #(20 * tu + 200);
      mn = 0;
      wr1(8'h7D, {5'd0, 1'b1, rearm, 1'b1});
      #60;                                                // the arm's reset clock has passed
      mon_q = 1;
      mon_start;
    end
  endtask

  // one engine capture: the stimulus, the burst, the model's event against the burst start
  integer det, ev_k, done, polls, ta;
  real    t_burst, t_ev_clk, lat;
  real    worst_eng = 0.0;
  task engine_run(input integer typ, input integer pol, input integer lng, input integer nn, input integer hh,
                  input [8*40-1:0] name);
    begin
      engine_arm(typ, pol, lng, nn, hh, 1'b0);
      #(25 * tu);                                         // the idle time H (20 samples) is seen after the arm
      fork play; join_none
      wait_edges(1, 40 * (nn + 20) * tu + 5000);
      #(10 * tu + 200);
      wait fork;
      mon_q = 0; mon_pad = 0;
      model(qtyp, lng, nn, hh, det);
      ev_k = (qtyp == 3) ? det - 1 : det;
      check(det > 0, "engine: the model finds the event");
      check(n_edge >= 1 && e_v[0] == 1'b1, "engine: the burst started (pad rose once)");
      if (det > 0 && n_edge >= 1) begin
        t_ev_clk = tz[ev_k] / 1000.0;                     // the clock that processed the event sample
        t_burst  = e_t[0];
        check(t_burst > t_ev_clk + 30.0 - 0.2 && t_burst < t_ev_clk + 30.0 + 0.2, "engine: the burst starts 6 clocks after the event clock");
        lat = t_burst - (t_start + t_q);
        if (c_src == 2) check(lat > 15.0 && lat < 70.0, "engine EXT: pad edge to burst under 70 ns (about 55)");
        else check(lat > 0.0 && lat < 12.0 * tu + 200.0, "engine CH: stimulus event to burst within the ADC pipeline + 12 words");
        if (lat > worst_eng) worst_eng = lat;
        $display("  E %0s src %0d K %0d D %0d: stimulus to burst %0.1f ns (word %0.1f ns), event clock to burst %0.1f ns", name, c_src, c_k, c_d, lat, tu, t_burst - t_ev_clk);
        $fdisplay(logf, "ENGINE src %0d K %0d D %0d %0s det %0d event clock %0.1f burst %0.1f lat from stimulus %0.1f ns (word %0.1f ns)",
                  c_src, c_k, c_d, name, det, t_ev_clk, t_burst, lat, tu);
      end
      check(n_edge <= 2, "engine one shot: a single burst");
      sig = inv;
      #(5 * tu + 300);
    end
  endtask

  //---------------------------------------------------------------------------------
  // Capture helpers (the firmware's arm)

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
      mon_w = 1;
      wr1(8'h01, 8'h00);
    end
  endtask

  task poll_done(input integer max_polls);
    begin
      polls = 0; done = 0;
      while (!done && polls < max_polls) begin rd1(8'h0A); done = rd_d[0]; polls = polls + 1; end
    end
  endtask

  //---------------------------------------------------------------------------------
  // Burst table checks: intervals between consecutive pad edges against a list (clocks)

  integer exp_iv [0:63];
  integer n_iv;
  task check_intervals(input [8*60-1:0] name);
    real iv;
    begin
      check(n_edge >= n_iv + 1, "bursts: enough edges");
      for (i = 0; i < n_iv && i + 1 < n_edge; i = i + 1) begin
        iv = e_t[i + 1] - e_t[i];
        check(iv > exp_iv[i] * 5.0 - 0.01 && iv < exp_iv[i] * 5.0 + 0.01, name);
        if (!(iv > exp_iv[i] * 5.0 - 0.01 && iv < exp_iv[i] * 5.0 + 0.01))
          $display("    interval %0d: %0.2f ns, expected %0d clocks", i, iv, exp_iv[i]);
      end
    end
  endtask

  // the 5-step burst table: n = 4, 7, 3, 12, 5, levels 1 0 1 0 1, end on step 4
  task table5;
    begin
      pstep(0, 1'b1, 1'b0, 1'b0, 32'd4, 16'd0);
      pstep(1, 1'b0, 1'b0, 1'b0, 32'd7, 16'd0);
      pstep(2, 1'b1, 1'b1, 1'b0, 32'd3, 16'd0);          // marked
      pstep(3, 1'b0, 1'b0, 1'b0, 32'd12, 16'd0);
      pstep(4, 1'b1, 1'b0, 1'b1, 32'd5, 16'd0);
      pstep(5, 1'b0, 1'b0, 1'b0, 32'd99, 16'd0);         // beyond the end: never used
      wr1(8'h6D, 8'h20);                                  // commit entries 0 / 1 (pending until the next start)
    end
  endtask
  task burst5(input integer base);                      // expected intervals of one burst starting at exp_iv[base]
    begin
      exp_iv[base] = 4; exp_iv[base + 1] = 7; exp_iv[base + 2] = 3; exp_iv[base + 3] = 12; exp_iv[base + 4] = 5;
    end
  endtask

  reg [255:0] strength;
  integer k, r, b1_n, b2_n, typ, pol, nn;
  real    b1_t [0:31];
  real    t_stop, acc_before, dur0;
  integer acc_b, acc_a, n_old_after;
  reg [15:0] exp_acc;
  reg        ok;

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    if (!$value$plusargs("logfile=%s", logname)) logname = "pattern.log";
    logf = $fopen(logname, "w");
    $display("tb_pattern: seed %0d", seed);
    #300;

    //-------------------------------------------------------------------------------
    // G. 0x50 alone: the firmware of 0x1532..0x153C (0x50 / 0x51 / 0x52 only) gets the contract's generator
    $sformat(strength, "%v", pad);
    check(strength[23:0] == "We0", "G: power-on pin hi-Z (pull-down)");
    wr1(8'h50, 8'h01);
    #50;
    $sformat(strength, "%v", pad);
    check(strength[23:0] == "St0", "G: 0x50 = 1 with the pattern off drives the idle level 0");
    rd1(8'h28);
    check(rd_d[1] == 1'b1, "G: 0x28 bit 1 = 1 while the pin is driven");
    mon_start;
    wr4(8'h51, 32'd85899346);                             // the retired DDS registers: ignored
    wr4(8'h52, 32'h80000000);
    #2000;
    check(n_edge == 0 && pad === 1'b0, "G: 0x51 / 0x52 writes change nothing (no DDS)");
    wr1(8'h6D, 8'h04);                                    // idle level 1, mode 0
    #50;
    check(pad === 1'b1, "G: 0x6D bit 2 sets the idle level the pin carries");
    rd4(8'h39);
    check(rd32 == 32'h00000000, "G: 0x39 reads 0 with the pattern off (idle level 1 is not a status bit)");
    wr1(8'h6D, 8'h00);
    #50;
    check(pad === 1'b0, "G: idle level back to 0");
    wr1(8'h50, 8'h00);
    #50;
    $sformat(strength, "%v", pad);
    check(strength[23:0] == "We0", "G: 0x50 = 0 pin hi-Z");
    rd1(8'h28);
    check(rd_d[1] == 1'b0, "G: 0x28 bit 1 = 0 with the pin hi-Z");
    mon_pad = 0;
    rd2(8'h06);
    check(rd16[15:8] == 8'h15 && rd16[7:0] >= 8'h3B, "cmd 0x06 reads 0x153B or later (D3 sets 0x3D)");

    //-------------------------------------------------------------------------------
    // S. The square wave with the firmware's SET_GEN sequences
    wr1(8'h50, 8'h01);
    // 10 MHz (1000 periods), 1 MHz (1000), 7.3 MHz (1000, fractional), 10 kHz 25 % (3), 1 kHz (3), 999.5 Hz (3)
    gen_calc(64'd10000000000, 64'd5000);
    check(n_h == 10 && n_l == 10 && f_h == 0 && f_l == 0, "S: 10 MHz 50 % = 10 + 10 clocks");
    mon_start;
    gen_write(1'b1);
    wait_edges(2004, 150000.0);
    square_check(1000, "10 MHz 50 %", 1'b1);
    // the change to 1 MHz on the running square (0x23: no start): the double-buffer path, checked in B below in detail
    gen_calc(64'd1000000000, 64'd5000);
    check(n_h == 100 && n_l == 100, "S: 1 MHz 50 % = 100 + 100 clocks");
    gen_write(1'b0);
    #3000;
    mon_start;
    wait_edges(2004, 1200000.0);
    square_check(1000, "1 MHz 50 %", 1'b0);
    gen_calc(64'd7300000000, 64'd5000);                   // P = 27.397 clocks: n_h 13 + 45457/65536, n_l 13 + 45457/65536
    gen_write(1'b0);
    #3000;
    mon_start;
    wait_edges(2004, 300000.0);
    square_check(1000, "7.3 MHz 50 % (fractional)", 1'b0);
    gen_calc(64'd10000000, 64'd2500);                     // 10 kHz 25 %
    check(n_h == 5000 && n_l == 15000 && f_h == 0 && f_l == 0, "S: 10 kHz 25 % = 5000 + 15000 clocks");
    gen_write(1'b0);
    #250000;
    mon_start;
    wait_edges(8, 1000000.0);
    square_check(3, "10 kHz 25 %", 1'b0);
    gen_calc(64'd1000000, 64'd5000);                      // 1 kHz, the wizard's: exact
    check(n_h == 100000 && n_l == 100000 && f_h == 0 && f_l == 0, "S: 1 kHz = 100000 + 100000 clocks (exact)");
    gen_write(1'b0);
    #1700000;                                             // the three queued old steps complete first (k+3 rule)
    mon_start;
    wait_edges(8, 5000000.0);
    square_check(3, "1 kHz 50 %", 1'b0);
    gen_calc(64'd999500, 64'd5000);                       // 999.5 Hz: fractional
    check(f_h != 0 || f_l != 0, "S: 999.5 Hz has a fractional period");
    gen_write(1'b0);
    #1700000;                                             // the three queued old steps complete first (k+3 rule)
    mon_start;
    wait_edges(8, 5000000.0);
    square_check(3, "999.5 Hz 50 % (fractional)", 1'b0);
    // the prototype's 3.5 + 2.25 clocks (P = 5.75): entry 0 n 3 f 0x8000, entry 1 n 2 f 0x4000
    pstep(0, 1'b1, 1'b0, 1'b0, 32'd3, 16'h8000);
    pstep(1, 1'b0, 1'b0, 1'b1, 32'd2, 16'h4000);
    wr1(8'h6D, 8'h23);
    #1700000;                                             // the three queued 999.5 Hz steps complete first
    P16 = 64'd5 * 65536 + 64'h8000 + 64'h4000; H16 = 64'd3 * 65536 + 64'h8000;
    mon_start;
    wait_edges(2004, 100000.0);
    square_check(1000, "3.5 + 2.25 clocks", 1'b0);
    // the same pattern started fresh (mode 0, then mode 3): absolute edge times
    wr1(8'h6D, 8'h00);
    #100;
    mon_start;
    wr1(8'h6D, 8'h03);
    wait_edges(2004, 100000.0);
    square_check(1000, "3.5 + 2.25 clocks from a fresh start", 1'b1);
    mon_pad = 0;
    $display("S: worst edge deviation span over all square waves %0.3f ns (limit 5), pad mismatches %0d", worst_dev_all, pin_err);
    check(worst_dev_all < 5.0, "S: worst edge deviation span under one clock");

    //-------------------------------------------------------------------------------
    // B. The double buffer on a running square: 2000 + 2000 clocks (50 kHz), then 1500 + 32768/65536 and 1200 + 16384/65536
    gen_calc(64'd50000000, 64'd5000);
    check(n_h == 2000 && n_l == 2000, "B: 50 kHz = 2000 + 2000");
    gen_write(1'b0);
    #100000;
    mon_start;
    wait_edges(5, 200000.0);                              // inside the run
    // commit the new values right after a rising edge (step 0 in progress): pending must read 1 until the next boundary
    @(posedge pad);
    pstep(0, 1'b1, 1'b0, 1'b0, 32'd1500, 16'h8000);
    pstep(1, 1'b0, 1'b0, 1'b1, 32'd1200, 16'h4000);
    acc_b = dut.u_pat.acc;
    wr1(8'h6D, 8'h23);
    rd1(8'h39);
    check(rd_d[5] == 1'b1, "B: pending reads 1 right after the commit (step in progress)");
    check(dut.u_pat.pending == 1'b1, "B: pending set");
    @(negedge pad);                                       // the boundary into step 1: the commit is taken here
    #20;
    check(dut.u_pat.pending == 1'b0, "B: pending clears at the first boundary after the commit");
    rd1(8'h39);
    check(rd_d[5] == 1'b0, "B: pending reads 0 after the boundary");
    wait_edges(n_edge + 8, 200000.0);
    mon_pad = 0;
    // intervals: every step is a complete old (2000) or new (1500 / 1501, 1200 / 1201) step; the step after the commit
    // boundary (step 1, queued) is old, then new
    n_old_after = 0; ok = 1;
    for (i = 0; i + 1 < n_edge; i = i + 1) begin
      dur0 = e_t[i + 1] - e_t[i];
      if (!(dur0 > 2000 * 5.0 - 0.01 && dur0 < 2000 * 5.0 + 0.01) &&
          !(e_v[i] == 1'b1 && dur0 > 1500 * 5.0 - 0.01 && dur0 < 1501 * 5.0 + 0.01) &&
          !(e_v[i] == 1'b0 && dur0 > 1200 * 5.0 - 0.01 && dur0 < 1201 * 5.0 + 0.01)) begin
        ok = 0;
        $display("    B: step %0d level %b lasted %0.2f ns", i, e_v[i], dur0);
      end
      $fdisplay(logf, "BUFFER step %0d level %b %0.2f ns", i, e_v[i], dur0);
    end
    check(ok, "B: every step after the change is a complete old or new step (no glitch)");
    // the accumulator continues: the first new step (level 1) lasts 1500 + carry(acc + 0x8000), acc_b being the
    // accumulator during the step the commit landed in (the old square has f = 0, so acc is constant until the change)
    i = 0;
    while (i + 1 < n_edge && !(e_v[i] == 1'b1 && e_t[i + 1] - e_t[i] < 1600 * 5.0)) i = i + 1;
    if (i + 1 < n_edge) begin
      dur0 = e_t[i + 1] - e_t[i];
      exp_acc = acc_b + 16'h8000;
      check(dur0 > (1500 + ((acc_b + 32'h8000) >> 16)) * 5.0 - 0.01 && dur0 < (1500 + ((acc_b + 32'h8000) >> 16)) * 5.0 + 0.01,
            "B: the first new step's carry comes from the running accumulator (not cleared)");
      $fdisplay(logf, "BUFFER acc before %0d, first new step %0.2f ns (expected %0d clocks)", acc_b, dur0, 1500 + ((acc_b + 32'h8000) >> 16));
    end else check(0, "B: a new step was found");
    // a commit lost by a second commit before the boundary: X = 1700 / 1700 committed, then Y = 1300 / 1300 committed
    // before the next boundary (the steps are >= 6 us, two 18-strobe sequences take about 4 us)
    mon_start;
    @(posedge pad);
    pstep(0, 1'b1, 1'b0, 1'b0, 32'd1700, 16'd0);
    pstep(1, 1'b0, 1'b0, 1'b1, 32'd1700, 16'd0);
    wr1(8'h6D, 8'h23);
    pstep(0, 1'b1, 1'b0, 1'b0, 32'd1300, 16'd0);
    pstep(1, 1'b0, 1'b0, 1'b1, 32'd1300, 16'd0);
    wr1(8'h6D, 8'h23);
    check(dut.u_pat.pending == 1'b1, "B: second commit before the boundary: still pending");
    wait_edges(10, 200000.0);
    mon_pad = 0;
    ok = 1;
    for (i = 0; i + 1 < n_edge; i = i + 1) begin
      dur0 = e_t[i + 1] - e_t[i];
      if (dur0 > 1700 * 5.0 - 0.01 && dur0 < 1700 * 5.0 + 0.01) ok = 0;
    end
    check(ok, "B: the overwritten commit's values (1700) never appear on the pin");
    check(e_t[n_edge - 1] - e_t[n_edge - 2] > 1300 * 5.0 - 0.01 && e_t[n_edge - 1] - e_t[n_edge - 2] < 1300 * 5.0 + 0.01,
          "B: the second commit's values (1300) run");
    wr1(8'h6D, 8'h00);                                    // stop
    #100;
    check(pad === 1'b0 && dut.u_pat.running == 1'b0, "B: mode 0 stops the generator, pin at the idle level");

    //-------------------------------------------------------------------------------
    // P. Bursts
    table5;
    wr2(8'h6E, 16'd3);
    wr4(8'h6F, 32'd20);
    // P1: mode 1, MCU start: one burst of 5 steps, then idle 0
    mon_start;
    wr1(8'h6D, 8'h11);                                    // mode 1 | start
    wait_edges(6, 2000.0);
    #500;
    n_iv = 5; burst5(0);
    check_intervals("P1: mode 1 step durations on the pin to the clock");
    check(n_edge == 6 && pad === 1'b0, "P1: one burst, then the idle level");
    check(n_mark == 1, "P1: the marked step pulsed the marker once");
    rd4(8'h39);
    check(rd32[31] == 1'b0 && rd32[23:8] == 16'd1 && rd32[0] == 1'b1, "P1: 0x39 after the burst: not running, 1 burst done, mode active");
    // P1b: a start during a burst is ignored (the first step is 4 clocks, so start at a long step: use idle level 1 table later); here: two
    // starts 100 ns apart give one burst because the second lands in the burst (95 clocks long)
    mon_start;
    wr1(8'h6D, 8'h11);
    wr1(8'h6D, 8'h11);                                    // about 90 ns later, inside the 31-clock burst? the bus takes longer: see P3
    wait_edges(12, 3000.0);
    #300;
    check(n_edge == 12 || n_edge == 6, "P1b: two starts give one or two complete bursts, never a torn one");
    if (n_edge == 12) begin n_iv = 11; burst5(0); exp_iv[5] = (e_t[6] - e_t[5]) / 5.0 + 0.5; burst5(6); check_intervals("P1b: both bursts complete"); end
    // P2: mode 2, R = 3, gap 20: 3 bursts, 2 gaps
    wr1(8'h6D, 8'h02);
    #100;
    mon_start;
    wr1(8'h6D, 8'h12);
    wait_edges(18, 5000.0);
    #500;
    n_iv = 17; burst5(0); exp_iv[5] = 20; burst5(6); exp_iv[11] = 20; burst5(12);
    check(n_edge == 18, "P2: 3 bursts and 2 gaps give 18 edges (the last edge ends burst 3 into the idle level)");
    check_intervals("P2: R = 3 with gap 20: bursts and gaps exact");
    rd4(8'h39);
    check(rd32[23:8] == 16'd3 && rd32[31] == 1'b0, "P2: 0x39: 3 bursts done, not running");
    // P2b: gap 0: the bursts follow seamlessly
    wr4(8'h6F, 32'd0);
    mon_start;
    wr1(8'h6D, 8'h12);
    wait_edges(14, 5000.0);
    #500;
    // the last step (level 1) runs into step 0 (level 1): 5 + 4 = 9 clocks without an edge
    // intervals 4,7,3,12 | 9 | 7,3,12 | 9 | 7,3,12,5 (the merged step pairs replace the 5 + 4 at each burst join)
    n_iv = 13;
    exp_iv[0] = 4; exp_iv[1] = 7; exp_iv[2] = 3; exp_iv[3] = 12; exp_iv[4] = 9;
    exp_iv[5] = 7; exp_iv[6] = 3; exp_iv[7] = 12; exp_iv[8] = 9;
    exp_iv[9] = 7; exp_iv[10] = 3; exp_iv[11] = 12; exp_iv[12] = 5;
    check(n_edge == 14, "P2b: R = 3 with gap 0: 14 edges");
    check_intervals("P2b: R = 3 with gap 0: seamless bursts");
    // P2c: 0x39 inside a gap: a long gap (400 clocks = 2 us) and R = 2
    wr4(8'h6F, 32'd400);
    wr2(8'h6E, 16'd2);
    wr1(8'h6D, 8'h12);
    wait (dut.u_pat.in_gap === 1'b1);
    rd4(8'h39);
    check(rd32[31] == 1'b1 && rd32[30] == 1'b1 && rd32[23:8] == 16'd1, "P2c: 0x39 inside the gap: running, in gap, 1 burst done");
    #3000;
    check(dut.u_pat.running == 1'b0 && dut.u_pat.bursts == 16'd2, "P2c: R = 2 done");
    // P3: mode 3 continuous with gap 20, stopped by mode 0
    wr4(8'h6F, 32'd20);
    mon_start;
    wr1(8'h6D, 8'h03);                                    // mode 3: starts by itself
    wait_edges(18, 5000.0);
    n_iv = 17; burst5(0); exp_iv[5] = 20; burst5(6); exp_iv[11] = 20; burst5(12);
    check_intervals("P3: mode 3: bursts and gaps repeat");
    rd4(8'h39);
    check(rd32[31] == 1'b1, "P3: 0x39 running in mode 3");
    wr1(8'h6D, 8'h03);                                    // written again while running: no restart
    wait_edges(30, 5000.0);
    n_iv = 29; exp_iv[17] = 20; burst5(18); exp_iv[23] = 20; burst5(24);
    check_intervals("P3: a mode 3 write while running does not restart the pattern");
    t_stop = $realtime;
    wr1(8'h6D, 8'h00);
    #100;
    check(dut.u_pat.running == 1'b0 && pad === 1'b0, "P3: mode 0 stops at once");
    mon_pad = 0;
    // P4: idle level 1 with mode 1: the pin is 1 between bursts
    wr1(8'h6D, 8'h05);                                    // mode 1, idle 1
    #50;
    check(pad === 1'b1, "P4: idle level 1 while waiting for a start");
    mon_start;
    wr1(8'h6D, 8'h15);                                    // start
    wait_edges(5, 2000.0);
    #300;
    // steps 1 0 1 0 1 against idle 1: edges at the end of step 0 (fall), 1 (rise), 2, 3, and none after step 4
    check(n_edge == 4 && pad === 1'b1, "P4: idle level 1: four edges, the pin back at 1 after the burst");
    wr1(8'h6D, 8'h00);
    // P5: entry 7 acts as end, index >= 8 ignored: an 8-step table without an end bit
    for (k = 0; k < 8; k = k + 1) pstep(k, k[0] ? 1'b0 : 1'b1, 1'b0, 1'b0, 32'd3 + k, 16'd0);
    pstep(9, 1'b1, 1'b0, 1'b1, 32'd2, 16'd0);             // index 9: ignored
    wr1(8'h6D, 8'h20);                                    // commit (mode 0): pending until the start
    #100;
    check(dut.u_pat.pending == 1'b1, "P5: a commit while stopped stays pending");
    mon_start;
    wr1(8'h6D, 8'h11);
    wait_edges(8, 3000.0);
    #500;
    // levels 1 0 1 0 1 0 1 0 from idle 0: the rise plus 7 boundaries; the last step (level 0) merges with the idle level, and
    // a table that did not end at entry 7 would go on to step 0 (more edges)
    for (k = 0; k < 7; k = k + 1) exp_iv[k] = 3 + k;
    n_iv = 7;
    check(n_edge == 8 && dut.u_pat.running == 1'b0 && dut.u_pat.bursts == 16'd1, "P5: an 8-step table without an end bit runs 8 steps once (entry 7 acts as end)");
    check_intervals("P5: 8 steps 3 .. 10 clocks, entry 7 as end");
    check(dut.u_pat.pending == 1'b0, "P5: the pending commit was taken at the start");
    // P6: the accumulator is cleared at every start: two bursts of fractional steps edge for edge
    pstep(0, 1'b1, 1'b0, 1'b0, 32'd3, 16'h8000);
    pstep(1, 1'b0, 1'b0, 1'b0, 32'd2, 16'h4000);
    pstep(2, 1'b1, 1'b0, 1'b0, 32'd4, 16'hC000);
    pstep(3, 1'b0, 1'b0, 1'b0, 32'd3, 16'h7000);
    pstep(4, 1'b1, 1'b0, 1'b0, 32'd2, 16'hF000);
    pstep(5, 1'b0, 1'b0, 1'b1, 32'd5, 16'h9000);
    wr1(8'h6D, 8'h20);
    mon_start;
    wr1(8'h6D, 8'h11);
    wait_edges(6, 2000.0);
    #200;
    b1_n = n_edge;
    for (k = 0; k < b1_n; k = k + 1) b1_t[k] = e_t[k] - e_t[0];
    mon_start;
    wr1(8'h6D, 8'h11);
    wait_edges(6, 2000.0);
    #200;
    check(n_edge == b1_n && b1_n == 6, "P6: both bursts have the same edge count");
    ok = 1;
    for (k = 0; k < b1_n && k < n_edge; k = k + 1) if (e_t[k] - e_t[0] < b1_t[k] - 0.01 || e_t[k] - e_t[0] > b1_t[k] + 0.01) ok = 0;
    check(ok, "P6: the two bursts are identical edge for edge (accumulator cleared at the start)");
    // the fractional burst's edges against the ideal (0 .. 1 clock early): cumulative n + f
    P16 = 0; dev_min = 1.0e9; dev_max = -1.0e9;
    begin : p6_ideal
      reg [63:0] cum;
      integer nn_i [0:5]; integer ff_i [0:5];
      nn_i[0] = 3; ff_i[0] = 16'h8000; nn_i[1] = 2; ff_i[1] = 16'h4000; nn_i[2] = 4; ff_i[2] = 16'hC000;
      nn_i[3] = 3; ff_i[3] = 16'h7000; nn_i[4] = 2; ff_i[4] = 16'hF000; nn_i[5] = 5; ff_i[5] = 16'h9000;
      cum = 0;
      for (k = 0; k < 5 && k + 1 < n_edge; k = k + 1) begin
        cum = cum + nn_i[k] * 65536 + ff_i[k];
        dev = (e_t[k + 1] - e_t[0]) - cum * 5.0 / 65536.0;
        if (dev < dev_min) dev_min = dev;
        if (dev > dev_max) dev_max = dev;
      end
    end
    check(dev_min > -5.001 && dev_max < 0.001, "P6: fractional burst edges 0 .. 1 clock early against the ideal");
    $display("P6: fractional burst edge deviation %0.3f .. %0.3f ns", dev_min, dev_max);
    mon_pad = 0;
    wr1(8'h6D, 8'h00);

    //-------------------------------------------------------------------------------
    // M. The marker as the capture's trigger source (0x15 = 3 with 0x6D bit 3), divider 49 (500 ns words), normal mode
    table5;
    // make the marked step reachable late: step 0 high 400 clocks (2 us), step 1 low 400, step 2 marked
    pstep(0, 1'b1, 1'b0, 1'b0, 32'd400, 16'd0);
    pstep(1, 1'b0, 1'b0, 1'b0, 32'd400, 16'd0);
    wr1(8'h6D, 8'h20);                                    // commit
    wr4(8'h0D, 32'd49);
    wr1(8'h53, 8'h00);
    wr1(8'h15, 8'd3);
    wr1(8'h16, 8'd0);
    wr1(8'h17, 8'd128);
    wr1(8'h5E, 8'h00);
    sig = 0; inv = 0;
    for (r = 0; r < 2; r = r + 1) begin
      wr1(8'h6D, 8'h09);                                  // mode 1, marker enabled
      n_mark = 0;
      arm_fw(100, 200, 1, 32'h00FFFFFF);
      wait (dut.sampling_half_way === 1'b1);
      #(3000 + ($random(seed) & 1023));
      wr1(8'h6D, 8'h19);                                  // start
      poll_done(20000);
      check(done == 1, "M: the capture triggered by the marker completes");
      rd1(8'h18);
      check(rd_d[0] == 1'b1, "M: 0x18 = 1");
      rd2(8'h14);
      ta = rd16 & 8191;
      check(n_mark == 1, "M: one marker pulse");
      check(wt[ta] / 1000.0 < t_mark && t_mark < wt[(ta + 1) % NS] / 1000.0, "M: the marker clock lies between the writes of words 0x14 and 0x14 + 1 (EXT convention)");
      $fdisplay(logf, "MARKER run %0d: marker at %0.1f ns, write address then %0d, 0x14 = %0d, write(ta) %0.1f write(ta+1) %0.1f",
                r, t_mark, wa_mark, ta, wt[ta] / 1000.0, wt[(ta + 1) % NS] / 1000.0);
      rd4(8'h39);
      check(rd32[1] == 1'b1, "M: 0x39 byte 3 bit 1 marker seen since 0x01 = 1");
      wr1(8'h01, 8'h01); #200; wr1(8'h01, 8'h00); #200;
      rd4(8'h39);
      check(rd32[1] == 1'b0, "M: marker seen cleared by 0x01 = 1");
    end
    // without 0x6D bit 3: no trigger (auto capture times out untriggered)
    wr1(8'h6D, 8'h01);
    arm_fw(100, 200, 0, 32'd400);
    wait (dut.sampling_half_way === 1'b1);
    #1000;
    wr1(8'h6D, 8'h11);
    poll_done(20000);
    rd1(8'h18);
    check(done == 1 && rd_d[0] == 1'b0, "M: without 0x6D bit 3 the marker does not trigger (auto time-out)");
    // with 0x15 = 2 (EXT) the EXT path reads the driven pin back (0x1533 behaviour): the capture triggers on the burst's
    // first rising edge, 4 us before the marker, not on the marker
    wr1(8'h15, 8'd2);
    wr1(8'h6D, 8'h09);
    arm_fw(100, 200, 1, 32'h00FFFFFF);
    wait (dut.sampling_half_way === 1'b1);
    #1000;
    wr1(8'h6D, 8'h19);
    poll_done(20000);
    rd1(8'h18); ok = rd_d[0];
    rd2(8'h14);
    ta = rd16 & 8191;
    check(done == 1 && ok == 1'b1, "M: 0x15 = 2 with the pin driven: the EXT path triggers on the pattern's own edge");
    check(wt[(ta + 1) % NS] / 1000.0 < t_mark - 3000.0, "M: 0x15 = 2: the trigger is the burst's first edge, 4 us before the marker, not the marker");
    wr1(8'h6D, 8'h00);
    wr1(8'h15, 8'd0);
    mon_w = 0;
    check(pin_err == 0, "the pad equalled the generator's level on every clock while driven");

    //-------------------------------------------------------------------------------
    // E. The pattern trigger engine: a one-step burst (level 1, 40 clocks) started by the engine
    wr1(8'h50, 8'h01);
    pstep(0, 1'b1, 1'b0, 1'b1, 32'd40, 16'd0);
    wr1(8'h6D, 8'h61);                                    // mode 1, start source 1 (engine)
    wr1(8'h6D, 8'h20 | 8'h41);                            // commit (the entry 0 shadow), mode 1 / source 1 kept
    #100;
    for (k = 0; k < 4; k = k + 1) begin
      case (k)
        0: configure(0, 0, 9);                            // CH1 legacy 10 MSa/s
        1: configure(1, 0, 9);                            // CH2 legacy
        2: configure(0, 2, 19);                           // CH1 CIC2 N = 20
        default: configure(2, 0, 9);                      // EXT
      endcase
      nn = (c_src == 2) ? 60 : 8;
      for (pol = 0; pol < 2; pol = pol + 1) begin
        // edge
        n_ev = 0; cur = 2.0 + jit(0); ev_t[0] = cur * tu; n_ev = 1; t_q = cur * tu;
        engine_run(0, pol, 0, 1, 0, "edge");
        // pulse width shorter than N: N + 4, N + 2.5, then N - 3 (qualifies)
        n_ev = 0; cur = 1.0 + jit(0);
        pulse(nn + 4 + jit(0), 2 * nn); pulse(nn + 2.5 + jit(0), 2 * nn);
        pulse(nn - 3 + jit(0), 0.0); t_q = cur * tu; cur = cur + 2 * nn; pulse(nn - 3, 2 * nn);
        engine_run(1, pol, 0, nn, 0, "pw<N");
        // pulse width longer than N
        n_ev = 0; cur = 1.0 + jit(0);
        pulse(nn - 4 + jit(0), 2 * nn); pulse(nn - 2.5 + jit(0), 2 * nn);
        pulse(nn + 3 + jit(0), 0.0); t_q = cur * tu; cur = cur + 2 * nn; pulse(nn + 3, 2 * nn);
        engine_run(1, pol, 1, nn, 0, "pw>N");
        // Nth edge N = 3 from the arm
        n_ev = 0; cur = 1.0 + jit(0);
        for (i = 0; i < 5; i = i + 1) begin if (i == 2) t_q = cur * tu; pulse(4 + jit(0), 6); end
        engine_run(2, pol, 0, 3, 0, "Nth N=3");
        // Nth edge N = 4 after an idle time of 20: bursts of 6 pulses, gaps 3, idle 40
        n_ev = 0; cur = 1.0;
        for (i = 0; i < 12; i = i + 1) begin if (i == 3) t_q = cur * tu; pulse(3 + jit(0), (i == 5) ? 40 : 3); end
        engine_run(2, pol, 0, 4, 20, "Nth N=4 H=20");
        // timeout: held pulses of N - 3, N - 1.5, then one of 3 N
        n_ev = 0; cur = 1.0 + jit(0);
        pulse(nn - 3 + jit(0), nn); pulse(nn - 1.5 + jit(0), nn);
        t_q = (cur + nn) * tu; pulse(3 * nn, nn);
        engine_run(3, pol, 0, nn, 0, "timeout");
      end
      $display("E: case src %0d K %0d D %0d done, %0d checks so far, %0d failed", c_src, c_k, c_d, checks, errors);
    end
    // 0x42 read-back after the last run (EXT, timeout type 3 is in 0x7A; type bits 1..0 = 3)
    wr_cmd(8'h42); rd_byte;
    check(rd_d[7] == 1'b1 && rd_d[6] == 1'b1 && rd_d[3] == 1'b1 && rd_d[1:0] == 2'd3, "E: 0x42 byte 0: own engine, event seen, source EXT, type 3");
    wr1(8'h7D, 8'h04);                                    // clear the flag (disarmed)
    wr_cmd(8'h42); rd_byte;
    check(rd_d[6] == 1'b0, "E: 0x7D bit 2 clears the event flag");
    check(dut.pq_armed == 1'b0, "E: one shot: the engine is disarmed after its event");
    // re-arm: a train of 5 pulses on CH1 legacy, edge type: 5 bursts
    configure(0, 0, 9);
    engine_arm(0, 0, 0, 1, 0, 1'b1);
    n_ev = 0; cur = 2.0;
    for (i = 0; i < 5; i = i + 1) pulse(10, 20);           // 300 ns pulses, the 200 ns burst ends before the next
    fork play; join_none
    wait_edges(10, 20000.0);
    #1000;
    wait fork;
    mon_q = 0; mon_pad = 0;
    check(n_edge == 10, "E: re-arm: five rising edges give five bursts");
    check(dut.pq_armed == 1'b1, "E: re-arm: still armed");
    wr1(8'h7D, 8'h00);
    sig = 0;
    // one shot with the same train: one burst
    engine_arm(0, 0, 0, 1, 0, 1'b0);
    n_ev = 0; cur = 2.0;
    for (i = 0; i < 5; i = i + 1) pulse(10, 20);
    fork play; join_none
    wait_edges(10, 20000.0);
    wait fork;
    mon_q = 0; mon_pad = 0;
    check(n_edge == 2, "E: one shot: five edges give one burst");
    sig = 0;
    // start sources: 0 = MCU only (the engine's event sets the flag but starts nothing), 1 = engine only (an MCU start is
    // ignored), 2 = both
    wr1(8'h6D, 8'h01);                                    // mode 1, source MCU
    wr1(8'h7D, 8'h04);
    engine_arm(0, 0, 0, 1, 0, 1'b0);
    n_ev = 0; cur = 2.0; pulse(10, 20);
    fork play; join_none
    wait fork;
    #2000;
    mon_q = 0;
    wr_cmd(8'h42); rd_byte;
    check(n_edge == 0 && rd_d[6] == 1'b1, "E: start source 0: the engine's event sets the flag but starts no burst");
    mon_start;
    wr1(8'h6D, 8'h11);
    wait_edges(2, 2000.0);
    check(n_edge == 2, "E: start source 0: the MCU start runs a burst");
    sig = 0;
    wr1(8'h6D, 8'h41);                                    // source 1: engine only
    mon_start;
    wr1(8'h6D, 8'h51);                                    // MCU start: ignored
    #1000;
    check(n_edge == 0, "E: start source 1: the MCU start is ignored");
    wr1(8'h6D, 8'h81);                                    // source 2: both
    engine_arm(0, 0, 0, 1, 0, 1'b0);
    n_ev = 0; cur = 2.0; pulse(10, 20);
    fork play; join_none
    wait_edges(2, 5000.0);
    wait fork;
    mon_q = 0;
    check(n_edge == 2, "E: start source 2: the engine starts a burst");
    sig = 0;
    mon_start;
    wr1(8'h6D, 8'h91);
    wait_edges(2, 2000.0);
    check(n_edge == 2, "E: start source 2: the MCU starts a burst");
    mon_pad = 0;
    // the capture (CH1 edge) and the engine (CH2 Nth edge 3) on the same stimulus, own engine: both fire
    wr1(8'h6D, 8'h41);                                    // engine only
    wr1(8'h15, 8'd0); wr1(8'h16, 8'd0); wr1(8'h17, 8'd128); wr1(8'h5E, 8'h00);
    wr1(8'h76, 8'd1);                                     // engine on CH2
    engine_arm(2, 0, 0, 3, 0, 1'b0);                      // pol 0 on CH2 (inv): sig idles at 1 for CH2 LO
    arm_fw(64, 256, 1, 32'h00FFFFFF);
    wait (dut.sampling_half_way === 1'b1);
    #(3 * tu + 100);
    n_ev = 0; cur = 1.0;
    for (i = 0; i < 5; i = i + 1) begin if (i == 2) t_q = cur * tu; pulse(4, 6); end
    fork play; join_none
    poll_done(6000);
    wait fork;
    mon_q = 0; mon_pad = 0;
    check(done == 1, "E+C: the capture on CH1 completes");
    rd1(8'h18);
    check(rd_d[0] == 1'b1, "E+C: the capture triggered (CH1 edge)");
    model(2, 0, 3, 0, det);
    check(det > 0 && n_edge >= 1 && e_t[0] > tz[det] / 1000.0 + 29.8 && e_t[0] < tz[det] / 1000.0 + 30.2,
          "E+C: the engine's burst starts 6 clocks after CH2's third edge while the capture used CH1's first");
    $fdisplay(logf, "ENGINE+CAPTURE: capture 0x18 %b, engine det %0d, burst %0.1f ns", rd_d[0], det, (n_edge >= 1) ? e_t[0] : -1.0);
    sig = 0;
    wr1(8'h7D, 8'h00);
    wr1(8'h6D, 8'h00);
    wr1(8'h50, 8'h00);

    $fclose(logf);
    $display("RESULT: %0d checks, %0d failed -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #3_000_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
