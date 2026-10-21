// Fractional square generator testbench (fpga/al3/src/zaklad.v, 0x153D: module sq_gen, commands 0x6C index 0 / 1, 0x6D,
// 0x50, read 0x39; docs/plans/fpga-0x153c.md sections 7.2 / 7.4 reduced by the owner's cut of 2026-10-21). Run with
// fpga/al3/tb/run.sh (ONLY=square) or vvp (+seed, +logfile). Derived from fpga-d2's tb_pattern.v (its G, S and B cases;
// the burst, marker and trigger-engine cases went with the feature). One instance, the AD9288 model and the MCU byte
// sequences of tb_trigtypes.v; the calibration pad is monitored for every edge (time and level) while the generator
// drives it.
//   G. The generator on 0x50 alone (nothing else written): the pin is driven low (the idle level) with 0x50 = 1, hi-Z
//      with 0x50 = 0; 0x51 / 0x52 (the DDS) and the cut pattern registers 0x6E / 0x6F / 0x76 .. 0x7D change nothing;
//      idle level 1 through 0x6D bit 2; 0x28 bit 1; 0x39 reads 0 while off; 0x06 = 0x153D.
//   L. The start latency: step 0 begins 8 .. 9 clocks after the rising edge of the 0x6D data strobe (fw-d5's figure).
//   S. The square wave, programmed with the firmware's SET_GEN sequence (P16 / H16 of section 7.4: entry 0 high, entry 1
//      low, 0x6D = 0x33 to enable, 0x23 for a change) at 1 kHz, 999.5 Hz, 10 kHz 25 %, 1 MHz, 10 MHz, 7.3 MHz, the
//      prototype's 3.5 + 2.25 clocks and the 2 + 2 clock minimum: every edge lands within one 5 ns clock of its ideal
//      time (the cumulative actual time is floor(ideal): 0 .. 1 clock early), the mean period and high time over the
//      periods equal P16 / 65536 and H16 / 65536 clocks within 1 / periods.
//   B. The double buffer: a frequency / duty change on the running square (0x6D = 0x23): the committed pair is taken at
//      once (the step in progress has more than three clocks left), the step in progress completes with its old
//      duration, every later step is new (no glitch), the accumulator is not cleared (the first new steps' carries come
//      from the running accumulator); two commits in a row both take effect in order; mode 0 stops at once; modes 1 / 2
//      act as 0; a commit while idle is taken at once and the start runs it; 0x6C index >= 2 ignored; a mode-3 write
//      while running is not a restart.
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

  reg [255:0] strength;
  integer i2, n_1700, n_1300;
  real    dur0, t_strobe, t_first;
  integer acc_b;
  reg     ok;

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    if (!$value$plusargs("logfile=%s", logname)) logname = "square.log";
    logf = $fopen(logname, "w");
    $display("tb_square: seed %0d", seed);
    #300;

    //-------------------------------------------------------------------------------
    // G. 0x50 alone: the firmware of 0x1532..0x153C (0x50 / 0x51 / 0x52 only) gets the idle level, no DDS
    $sformat(strength, "%v", pad);
    check(strength[23:0] == "We0", "G: power-on pin hi-Z (pull-down)");
    wr1(8'h50, 8'h01);
    #50;
    $sformat(strength, "%v", pad);
    check(strength[23:0] == "St0", "G: 0x50 = 1 with the generator off drives the idle level 0");
    rd1(8'h28);
    check(rd_d[1] == 1'b1, "G: 0x28 bit 1 = 1 while the pin is driven");
    mon_start;
    wr4(8'h51, 32'd85899346);                             // the retired DDS registers: ignored
    wr4(8'h52, 32'h80000000);
    #2000;
    check(n_edge == 0 && pad === 1'b0, "G: 0x51 / 0x52 writes change nothing (no DDS)");
    // the cut pattern registers: ignored (no decoder entry, no mcu_state step)
    wr2(8'h6E, 16'd3);
    wr4(8'h6F, 32'd1000);
    wr1(8'h76, 8'h01); wr1(8'h77, 8'h01); wr1(8'h78, 8'd50); wr2(8'h79, 16'h1234); wr1(8'h7A, 8'h02);
    wr3(8'h7B, 24'd3); wr3(8'h7C, 24'd7); wr1(8'h7D, 8'h07);
    #2000;
    check(n_edge == 0 && pad === 1'b0, "G: writes to the cut pattern registers 0x6E / 0x6F / 0x76 .. 0x7D change nothing");
    rd1(8'h42);
    rd1(8'h39);
    check(rd_d == 8'h00, "G: 0x39 reads 0 with the generator off (0x42 is not decoded)");
    wr1(8'h6D, 8'h04);                                    // idle level 1, mode 0
    #50;
    check(pad === 1'b1, "G: 0x6D bit 2 sets the idle level the pin carries");
    rd4(8'h39);
    check(rd32 == 32'h00000000, "G: 0x39 reads 0 with the generator off (idle level 1 is not a status bit)");
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
    check(rd16 == 16'h153D, "cmd 0x06 reads 0x153D");

    //-------------------------------------------------------------------------------
    // L. The start latency: the first edge 8 .. 9 clocks (40 .. 45 ns) after the rising edge of the 0x6D data strobe
    wr1(8'h50, 8'h01);
    gen_calc(64'd10000000000, 64'd5000);                  // 10 MHz
    pstep(0, 1'b1, 1'b0, 1'b0, n_h, f_h);
    pstep(1, 1'b0, 1'b0, 1'b1, n_l, f_l);
    mon_start;
    wr_cmd(8'h6D);
    mcu_rws = 1; mcu_dcs = 0; mcu_dout = 8'h33;
    #(0.3 * stb) mcu_clk = 0; #(0.4 * stb) mcu_clk = 1; t_strobe = $realtime; #(0.3 * stb);
    wait_edges(1, 1000.0);
    t_first = e_t[0];
    $display("L: first edge %0.3f ns after the 0x6D strobe's rising edge", t_first - t_strobe);
    $fdisplay(logf, "START latency %0.3f ns", t_first - t_strobe);
    check(n_edge >= 1 && e_v[0] == 1'b1, "L: the first edge after the start is the rising edge of step 0");
    check(t_first - t_strobe >= 39.9 && t_first - t_strobe <= 50.1, "L: step 0 begins 40 .. 50 ns (8 .. 10 clocks) after the 0x6D data strobe");
    rd1(8'h39);
    check(rd_d[7] == 1'b1 && rd_d[5] == 1'b0, "L: 0x39 running, no commit pending");
    wait_edges(2004, 150000.0);
    square_check(1000, "10 MHz 50 % from the start", 1'b1);
    // the 0x39 step bit follows the step in progress (10 MHz: 50 ns per step; sampled on the pad level)
    wr1(8'h6D, 8'h00);
    #100;
    mon_pad = 0;

    //-------------------------------------------------------------------------------
    // S. The square wave with the firmware's SET_GEN sequences
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
    #1200000;                                             // the step in progress (at most 75 us) and a whole new period
    mon_start;
    wait_edges(8, 5000000.0);
    square_check(3, "1 kHz 50 %", 1'b0);
    gen_calc(64'd999500, 64'd5000);                       // 999.5 Hz: fractional
    check(f_h != 0 || f_l != 0, "S: 999.5 Hz has a fractional period");
    gen_write(1'b0);
    #1200000;
    mon_start;
    wait_edges(8, 5000000.0);
    square_check(3, "999.5 Hz 50 % (fractional)", 1'b0);
    // the prototype's 3.5 + 2.25 clocks (P = 5.75): entry 0 n 3 f 0x8000, entry 1 n 2 f 0x4000
    pstep(0, 1'b1, 1'b0, 1'b0, 32'd3, 16'h8000);
    pstep(1, 1'b0, 1'b0, 1'b1, 32'd2, 16'h4000);
    wr1(8'h6D, 8'h23);
    #1200000;                                             // the 999.5 Hz step in progress completes first
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
    // the 2 + 2 clock square (50 MHz, below the firmware's 25 MHz cap): the shortest steps
    pstep(0, 1'b1, 1'b0, 1'b0, 32'd2, 16'd0);
    pstep(1, 1'b0, 1'b0, 1'b1, 32'd2, 16'd0);
    wr1(8'h6D, 8'h00);
    #100;
    mon_start;
    wr1(8'h6D, 8'h23);                                    // commit while idle: taken at once; mode 3 written = a start
    P16 = 64'd4 * 65536; H16 = 64'd2 * 65536;
    wait_edges(2004, 100000.0);
    square_check(1000, "2 + 2 clocks (50 MHz)", 1'b1);
    mon_pad = 0;
    $display("S: worst edge deviation span over all square waves %0.3f ns (limit 5), pad mismatches %0d", worst_dev_all, pin_err);
    check(worst_dev_all < 5.0, "S: worst edge deviation span under one clock");
    check(pin_err == 0, "S: the pad equals the generator's level on every clock while driven");

    //-------------------------------------------------------------------------------
    // B. The double buffer on a running square: 2000 + 2000 clocks (50 kHz), then 1500 + 32768/65536 and 1200 + 16384/65536.
    // The stripped design takes a committed pair as soon as the step in progress has at least three clocks left (at once
    // in practice: the firmware's steps are >= 4 clocks), so the step in progress completes with its old duration and
    // every step from the next boundary on is new; pending is therefore set for a few clocks at most
    gen_calc(64'd50000000, 64'd5000);
    check(n_h == 2000 && n_l == 2000, "B: 50 kHz = 2000 + 2000");
    gen_write(1'b1);
    #100000;
    mon_start;
    wait_edges(5, 200000.0);                              // inside the run
    // commit the new values right after a rising edge (step 0 in progress)
    @(posedge pad);
    pstep(0, 1'b1, 1'b0, 1'b0, 32'd1500, 16'h8000);
    pstep(1, 1'b0, 1'b0, 1'b1, 32'd1200, 16'h4000);
    acc_b = dut.u_sq.acc;
    wr1(8'h6D, 8'h23);
    #50;
    check(dut.u_sq.pending == 1'b0, "B: the commit is taken within a few clocks (the step in progress has > 3 clocks left)");
    rd1(8'h39);
    check(rd_d[7] == 1'b1 && rd_d[5] == 1'b0, "B: 0x39 running, pending 0 after the take");
    check(dut.u_sq.e0[47:16] == 32'd1500 && dut.u_sq.e1[47:16] == 32'd1200 && dut.u_sq.cnt[32] == 1'b0,
          "B: the active pair holds the new entries while the old step is still counting");
    @(negedge pad);                                       // the boundary into step 1: the first new step
    wait_edges(n_edge + 8, 200000.0);
    mon_pad = 0;
    // intervals: every step is a complete old (2000) or new (1500 / 1501, 1200 / 1201) step; the step the commit landed in
    // (step 0, 2000) is old, then every step is new
    ok = 1;
    for (i2 = 0; i2 + 1 < n_edge; i2 = i2 + 1) begin
      dur0 = e_t[i2 + 1] - e_t[i2];
      if (!(dur0 > 2000 * 5.0 - 0.01 && dur0 < 2000 * 5.0 + 0.01) &&
          !(e_v[i2] == 1'b1 && dur0 > 1500 * 5.0 - 0.01 && dur0 < 1501 * 5.0 + 0.01) &&
          !(e_v[i2] == 1'b0 && dur0 > 1200 * 5.0 - 0.01 && dur0 < 1201 * 5.0 + 0.01)) begin
        ok = 0;
        $display("    B: step %0d level %b lasted %0.2f ns", i2, e_v[i2], dur0);
      end
      $fdisplay(logf, "BUFFER step %0d level %b %0.2f ns", i2, e_v[i2], dur0);
    end
    check(ok, "B: every step after the change is a complete old or new step (no glitch)");
    // the step after the commit's (the low step at the first boundary) is already new: 1200 or 1201 clocks
    // the accumulator continues: the first new step (level 1) lasts 1500 + carry(acc + 0x8000), acc_b being the
    // accumulator during the step the commit landed in (the old square has f = 0, so acc is constant until the change)
    i2 = 0;
    while (i2 + 1 < n_edge && !(e_v[i2] == 1'b1 && e_t[i2 + 1] - e_t[i2] < 1600 * 5.0)) i2 = i2 + 1;
    if (i2 + 1 < n_edge) begin
      dur0 = e_t[i2 + 1] - e_t[i2];
      // the low step before it (new, 1200 + carry(acc_b + 0x4000)) consumed the first fraction; the high step's carry
      // comes from acc_b + 0x4000 (mod 2^16) + 0x8000
      check(dur0 > (1500 + (((acc_b + 32'h4000) & 32'hFFFF) + 32'h8000 >> 16)) * 5.0 - 0.01 &&
            dur0 < (1500 + (((acc_b + 32'h4000) & 32'hFFFF) + 32'h8000 >> 16)) * 5.0 + 0.01,
            "B: the first new high step's carry comes from the running accumulator (not cleared at a commit)");
      $fdisplay(logf, "BUFFER acc before %0d, first new high step %0.2f ns", acc_b, dur0);
      check(i2 >= 1 && e_v[i2 - 1] == 1'b0 && e_t[i2] - e_t[i2 - 1] > 1200 * 5.0 - 0.01 && e_t[i2] - e_t[i2 - 1] < 1201 * 5.0 + 0.01,
            "B: the low step right after the commit's step is already new (1200 + carry)");
    end else check(0, "B: a new step was found");
    // two commits in a row: X = 1700 / 1700, then Y = 1300 / 1300 about 4 us later (18 strobes each). Both are taken at
    // once, so X runs for the steps between the two commits and Y from the boundary after the second
    mon_start;
    @(posedge pad);
    pstep(0, 1'b1, 1'b0, 1'b0, 32'd1700, 16'd0);
    pstep(1, 1'b0, 1'b0, 1'b1, 32'd1700, 16'd0);
    wr1(8'h6D, 8'h23);
    pstep(0, 1'b1, 1'b0, 1'b0, 32'd1300, 16'd0);
    pstep(1, 1'b0, 1'b0, 1'b1, 32'd1300, 16'd0);
    wr1(8'h6D, 8'h23);
    #50;
    check(dut.u_sq.pending == 1'b0 && dut.u_sq.e0[47:16] == 32'd1300, "B: the second commit replaced the first in the active pair");
    wait_edges(12, 200000.0);
    mon_pad = 0;
    ok = 1; n_1700 = 0; n_1300 = 0;
    for (i2 = 0; i2 + 1 < n_edge; i2 = i2 + 1) begin
      dur0 = e_t[i2 + 1] - e_t[i2];
      if (dur0 > 1700 * 5.0 - 0.01 && dur0 < 1700 * 5.0 + 0.01) n_1700 = n_1700 + 1;
      else if (dur0 > 1300 * 5.0 - 0.01 && dur0 < 1300 * 5.0 + 0.01) n_1300 = n_1300 + 1;
      else if (!((dur0 > 1500 * 5.0 - 0.01 && dur0 < 1501 * 5.0 + 0.01) || (dur0 > 1200 * 5.0 - 0.01 && dur0 < 1201 * 5.0 + 0.01))) ok = 0;
      $fdisplay(logf, "BUFFER2 step %0d level %b %0.2f ns", i2, e_v[i2], dur0);
    end
    $display("B: two commits: %0d steps of 1700, %0d of 1300 clocks", n_1700, n_1300);
    check(ok && n_1700 >= 1 && n_1700 <= 2 && n_1300 >= 6, "B: the first commit's values run for the one or two steps before the second is taken, then the second's");
    check(e_t[n_edge - 1] - e_t[n_edge - 2] > 1300 * 5.0 - 0.01 && e_t[n_edge - 1] - e_t[n_edge - 2] < 1300 * 5.0 + 0.01,
          "B: the second commit's values (1300) run");
    wr1(8'h6D, 8'h00);                                    // stop
    #100;
    check(pad === 1'b0 && dut.u_sq.running == 1'b0, "B: mode 0 stops the generator, pin at the idle level");
    rd4(8'h39);
    check(rd32 == 32'h00000000, "B: 0x39 reads 0 after the stop");
    // modes 1 and 2 act as 0: a mode-1 write while idle starts nothing, a mode-2 write while running stops
    mon_start;
    wr1(8'h6D, 8'h21);
    #5000;
    check(n_edge == 0 && pad === 1'b0, "B: mode 1 (not a mode of this design) starts nothing");
    wr1(8'h6D, 8'h33);
    wait_edges(4, 100000.0);
    wr1(8'h6D, 8'h02);
    #100;
    check(dut.u_sq.running == 1'b0 && pad === 1'b0, "B: mode 2 written while running stops the generator (acts as 0)");
    // a commit while idle is taken at once and the start uses it
    pstep(0, 1'b1, 1'b0, 1'b0, 32'd300, 16'd0);
    pstep(1, 1'b0, 1'b0, 1'b1, 32'd500, 16'd0);
    wr1(8'h6D, 8'h20);
    #50;
    check(dut.u_sq.pending == 1'b0 && dut.u_sq.e0[47:16] == 32'd300, "B: a commit while idle is taken at once");
    mon_start;
    wr1(8'h6D, 8'h13);                                    // mode 3 + start
    wait_edges(5, 100000.0);
    mon_pad = 0;
    check(n_edge >= 5 && e_v[0] == 1'b1 && e_t[1] - e_t[0] > 300 * 5.0 - 0.01 && e_t[1] - e_t[0] < 300 * 5.0 + 0.01 &&
          e_t[2] - e_t[1] > 500 * 5.0 - 0.01 && e_t[2] - e_t[1] < 500 * 5.0 + 0.01, "B: the start runs the committed 300 / 500 pair from step 0");
    // a 0x6C write to index 2 .. 31 is ignored, and a write without a commit changes nothing
    pstep(2, 1'b1, 1'b0, 1'b0, 32'd9, 16'd0);
    pstep(0, 1'b1, 1'b0, 1'b0, 32'd77, 16'd0);
    #20000;
    check(dut.u_sq.e0[47:16] == 32'd300 && dut.u_sq.sh0[47:16] == 32'd77, "B: index 2 ignored; an entry without a commit stays in the shadow");
    wr1(8'h6D, 8'h03);                                    // mode 3 again while running: no restart, no change
    #50;
    check(dut.u_sq.running == 1'b1 && dut.u_sq.e0[47:16] == 32'd300, "B: a mode-3 write while running is not a restart and takes nothing");
    wr1(8'h6D, 8'h00);
    wr1(8'h50, 8'h00);

    $fclose(logf);
    $display("RESULT: %0d checks, %0d failed -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #40_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
