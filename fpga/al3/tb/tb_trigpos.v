// Trigger position determinism testbench (fpga/al3/src/zaklad.v, 0x1536 and later).
// Run with fpga/al3/tb/run.sh (ONLY=trigpos) or directly with vvp (+seed, +runs, +pl, +order).
//
// Question (docs/plans/task-queue.md, "Trigger position varies by run at decimated rates"): does the FPGA put a
// trigger at the same place in the stored record on every run, or does the decimator/FIR latency, the trigger
// detector (raw or decimated stream) or the record pointer arithmetic add run-to-run variation?
//
// One instance (dut = src/zaklad.v). The ADCs are a behavioural AD9288: each core samples the analog input on
// the rising edge of its encode pin (the dut's o_adc1_encA / encB) and outputs that sample `pl` encode cycles
// later (+pl, default 4 = the datasheet's pipeline delay), 3.3 ns after the encode edge. The testbench keeps the
// sampling instant of every value next to it, so each stored legacy sample has a known true time.
// The analog input is a step (CH1 LO -> HI, CH2 its complement) at a random instant te; the EXT pad carries the
// same step (generator loopback). 0x153A: the sampling-instant tags are carried through a shadow of the dut's
// capture pipeline, so the monitor knows the instant of every stored sample (see the monitors below).
//
// Every capture copies the firmware's MCU byte sequence (pc_hal_arm, pc_dec_apply, pc_hal_acq_poll,
// pc_hal_acq_finish, pc_hal_read_core with its dummy strobe, and fpga_read_decim_data / the 1.00o5 readout without
// it). MCU strobe periods vary per capture (40 to 300 ns), and cases (K, FIR, divider, source, pretrigger) come in
// random order, so every run starts at a random phase of the write clock, the decimation counter and the signal.
//
// Phase 1 (determinism): for every case the crossing of the step in the record read over the bus, relative to
//   the pretrigger written to 0x0B, must be identical in every run (ADC trigger) or within one record sample (EXT,
//   whose edge is asynchronous to the sample grid), and the time from the analog edge to the record sample must
//   stay within one sample period. trigger_address read over 0x14 must equal the register, and the bus readout
//   must equal the sample memory (dummy-strobe path) or the memory one word late (no dummy strobe).
// Phase 2 (directed): the configuration race at slow legacy rates. 0x0B and 0x57..0x5C are written inside the
//   0x01 = 1 window right after a write clock edge, and 0x01 = 0 follows before the next one. 0x1536 kept the
//   previous pretrigger (record shifted by the difference) and the previous slot mask; 0x1537 must not.
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

  reg  sig = 0;                                         // analog step: CH1 HI, CH2 LO, EXT pad 1
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

  //---------------------------------------------------------------------------------
  // Bookkeeping

  integer errors = 0, checks = 0;
  real    dab;                                          // legacy: core B's sampling instant - core A's in a word (ns)
  integer dab_sign = 0;
  integer seed;
  integer logf;

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
  // AD9288 model: sample on the encode rising edge, output pl encode cycles later, tPD 3.3 ns

  localparam [7:0] LO = 8'd40, HI = 8'd210;
  integer pl;
  reg [7:0]  pA [0:7];
  reg [7:0]  pB [0:7];
  reg [63:0] qA [0:7];                                  // sampling instants in ps
  reg [63:0] qB [0:7];
  reg [63:0] tagA = 0, tagB = 0;                        // instant of the value now on the A / B pins
  integer ia, ib;

  initial
    for (ia = 0; ia < 8; ia = ia + 1) begin pA[ia] = LO; pB[ia] = LO; qA[ia] = 0; qB[ia] = 0; end

  always @(posedge e1A) begin
    for (ia = 7; ia > 0; ia = ia - 1) begin pA[ia] = pA[ia - 1]; qA[ia] = qA[ia - 1]; end
    pA[0] = sig ? HI : LO;
    qA[0] = $realtime * 1000.0;
    adc1A <= #3.3 pA[pl];
    adc2A <= #3.3 8'd255 - pA[pl];
    tagA  <= #3.3 qA[pl];
  end

  always @(posedge e1B) begin
    for (ib = 7; ib > 0; ib = ib - 1) begin pB[ib] = pB[ib - 1]; qB[ib] = qB[ib - 1]; end
    pB[0] = sig ? HI : LO;
    qB[0] = $realtime * 1000.0;
    adc1B <= #3.3 pB[pl];
    adc2B <= #3.3 8'd255 - pB[pl];
    tagB  <= #3.3 qB[pl];
  end

  //---------------------------------------------------------------------------------
  // Monitors

  // 0x153A: the tags follow the dut's capture pipeline (fpga/al3/README.md "Changes" section 10): IOB DDR register
  // on clk_200MHz (q2 = pin at the falling edge, q1 = at the rising edge, both out on the rising edge), two more
  // rising-edge stages, the stage registers on the write clock edges (A and the A-referenced B on the falling edge,
  // B on the rising edge), the per-core pick one write clock later on the same edge, then adc*_w on the rising edge.
  // twA / twB are the sampling instants of dut.adc1A_w / dut.adc1B_w; a change of the dut's pipeline must be
  // mirrored here (tb_adccap.v checks the dut's word association independently).
  reg [63:0] tA_c0 = 0, tA_q2 = 0, tA_t0 = 0, tA_q1 = 0, tA_d1 = 0, tA_d2 = 0, tA_dd1 = 0, tA_dd2 = 0;
  reg [63:0] tB_c0 = 0, tB_q2 = 0, tB_t0 = 0, tB_q1 = 0, tB_d1 = 0, tB_d2 = 0, tB_dd1 = 0, tB_dd2 = 0;
  reg [63:0] tsA [0:5];
  reg [63:0] tsB [0:5];
  reg [63:0] tsBf [0:5];
  reg [63:0] ta_m = 0, tb_m = 0, tbf_m = 0, twA = 0, twB = 0;
  integer    tsi;
  initial for (tsi = 0; tsi < 6; tsi = tsi + 1) begin tsA[tsi] = 0; tsB[tsi] = 0; tsBf[tsi] = 0; end
  wire [2:0] selA = (dut.cap_sel[2:0] > 3'd5) ? 3'd5 : dut.cap_sel[2:0];
  wire [2:0] selB = (dut.cap_sel[5:3] > 3'd5) ? 3'd5 : dut.cap_sel[5:3];
  always @(negedge dut.clk_200MHz) begin tA_c0 <= tagA; tB_c0 <= tagB; end
  always @(posedge dut.clk_200MHz) begin
    tA_q2 <= tA_c0; tA_t0 <= tagA; tA_q1 <= tA_t0; tA_d1 <= tA_q1; tA_d2 <= tA_q2; tA_dd1 <= tA_d1; tA_dd2 <= tA_d2;
    tB_q2 <= tB_c0; tB_t0 <= tagB; tB_q1 <= tB_t0; tB_d1 <= tB_q1; tB_d2 <= tB_q2; tB_dd1 <= tB_d1; tB_dd2 <= tB_d2;
  end
  always @(negedge dut.sample_write_clock) begin
    tsA[0] <= tA_q2; tsA[1] <= tA_q1; tsA[2] <= tA_d2; tsA[3] <= tA_d1; tsA[4] <= tA_dd2; tsA[5] <= tA_dd1;
    tsBf[0] <= tB_q2; tsBf[1] <= tB_q1; tsBf[2] <= tB_d2; tsBf[3] <= tB_d1; tsBf[4] <= tB_dd2; tsBf[5] <= tB_dd1;
    ta_m <= tsA[selA]; tbf_m <= tsBf[selB];
  end
  always @(posedge dut.sample_write_clock) begin
    tsB[0] <= tB_q2; tsB[1] <= tB_q1; tsB[2] <= tB_d2; tsB[3] <= tB_d1; tsB[4] <= tB_dd2; tsB[5] <= tB_dd1;
    tb_m <= tsB[selB];
    twA <= ta_m;
    twB <= dut.cap_sel[12] ? tbf_m : tb_m;
  end

  // every memory write: its time and (legacy) the sampling instants of the A and B values it stores
  reg [63:0] wt [0:8191];
  reg [63:0] wA [0:8191];
  reg [63:0] wB [0:8191];
  always @(posedge dut.sample_write_clock)
    if (dut.sample_write_enable & dut.swc_adv) begin
      wt[dut.sample_write_address_phys] = $realtime * 1000.0;
      wA[dut.sample_write_address_phys] = twA;
      wB[dut.sample_write_address_phys] = twB;
    end

  // the analog edge: its instant and the write address at that moment
  reg [63:0] te = 0;
  integer    wa_edge = 0;
  always @(posedge sig) begin
    te = $realtime * 1000.0;
    wa_edge = dut.sample_write_address;
  end

  // exposure to the configuration race: at the release of 0x01 the preloaded trigger read address is not the
  // pretrigger now in 0x0B (0x1532..0x1536 keep it; 0x1537 reloads it on the first edge after the release)
  reg stale_pre = 0;
  always @(negedge dut.sample_system_reset)
    stale_pre = (dut.trigger_read_address !== dut.pretrigger_samples);

  //---------------------------------------------------------------------------------
  // MCU bus, strobe period stb ns (the real MCU: about 100 to 300 ns per strobe)

  real stb = 43.0;

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

  // pc_hal_read_core (dummy = 1: one dummy strobe after the command) or fpga_read_decim_data / the 1.00o5
  // readout (dummy = 0): set 0x1F = start, command, then n read strobes into rb
  reg [7:0] rb [0:8191];
  integer   ri;
  task read_core(input [7:0] cmd, input integer start, input integer n, input dummy);
    begin
      wr2(8'h1F, start & 8191);
      wr_cmd(cmd);
      mcu_rws = 0; mcu_dcs = 0;
      if (dummy) mcu_strobe;
      for (ri = 0; ri < n; ri = ri + 1) begin mcu_strobe; rb[ri] = bus; end
      mcu_rws = 1;
    end
  endtask

  //---------------------------------------------------------------------------------
  // Filter helpers (as tb_seg.v and the firmware's pc_decim.c)

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

  // 16-bit output of a constant input v: v x G >> S, G = N^K (CIC by N) or (N/2)^K (CIC by N/2 before the FIR)
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
  // Cases

  integer    nc = 0;
  reg [1:0]  c_k   [0:63];
  reg        c_fir [0:63];
  reg [31:0] c_d   [0:63];
  reg [1:0]  c_src [0:63];
  // per case statistics
  integer    s_runs [0:63], s_stale [0:63];
  integer    s_omin [0:63], s_omax [0:63];             // crossing - trigger point, record samples
  integer    s_wmin [0:63], s_wmax [0:63];             // legacy: crossing word of core A - pre (words)
  real       s_tmin [0:63], s_tmax [0:63];             // ns: legacy edge - nominal trigger sample; 16-bit edge to write
  real       s_fmin [0:63], s_fmax [0:63];             // 16-bit: interpolated crossing - pre (samples)

  task add_case(input [1:0] k, input fir, input [31:0] d, input [1:0] src);
    begin
      c_k[nc] = k; c_fir[nc] = fir; c_d[nc] = d; c_src[nc] = src;
      s_runs[nc] = 0; s_stale[nc] = 0;
      s_omin[nc] = 99999; s_omax[nc] = -99999; s_wmin[nc] = 99999; s_wmax[nc] = -99999;
      s_tmin[nc] = 1.0e12; s_tmax[nc] = -1.0e12; s_fmin[nc] = 1.0e12; s_fmax[nc] = -1.0e12;
      nc = nc + 1;
    end
  endtask

  //---------------------------------------------------------------------------------
  // One capture as the firmware does it

  integer    cur_k = -1, cur_fir = -1, cur_d = -1, cur_src = -1;
  reg [5:0]  sh;
  reg [15:0] lvl16;
  integer    order = 0;                                 // 1: write 0x0B / 0x0C also before 0x01 = 1
  integer    polls, done, trig, ta, start, i, j, ci, cw, tw_ns;
  reg [15:0] y [0:8191];
  reg [7:0]  ra [0:8191];
  reg [7:0]  rbB [0:8191];
  reg [7:0]  smp [0:16383];
  reg [63:0] stt [0:16383];
  integer    mem_bad, nd_bad, ord_bad, o_s, wcross, is_legacy;
  real       tns, fx, lat;
  reg        bus_ta_ok;

  task configure(input integer c);
    begin
      // UI time base (fpga_set_sample_rate), then pc_dec_apply (mode, FIR, shift, then the settle wait)
      if (c_d[c] != cur_d) wr4(8'h0D, c_d[c]);
      if (c_k[c] != cur_k || c_fir[c] != cur_fir || c_d[c] != cur_d) begin
        sh = (c_k[c] == 0) ? 6'd0 : (c_fir[c] ? shift_for(c_k[c], (c_d[c] / 2) + 1) + 1
                                               : shift_for(c_k[c], 2 * ((c_d[c] / 2) + 1)));
        wr1(8'h53, {6'd0, c_k[c]});
        wr1(8'h56, {7'd0, c_fir[c]});
        wr1(8'h54, {2'd0, sh});
        if (c_k[c] != 0) #((c_k[c] + 10) * ((c_d[c] / 2) + 1) * 10 + 1000);
      end
      if (c_src[c] != cur_src) begin
        wr1(8'h15, {6'd0, c_src[c]});
        wr1(8'h16, 8'h00);
      end
      cur_k = c_k[c]; cur_fir = c_fir[c]; cur_d = c_d[c]; cur_src = c_src[c];
      ext_en = (c_src[c] == 2);
      lvl16 = (out16(LO, c_k[c], c_fir[c], c_d[c], sh) + out16(HI, c_k[c], c_fir[c], c_d[c], sh)) / 2;
    end
  endtask

  // arm (pc_hal_arm), the step at a random instant after the pretrigger filled, poll, finish, read back
  task capture(input integer c, input integer pre_w, input integer tot_w);
    real dly;
    begin
      is_legacy = (c_k[c] == 0);
      tw_ns = is_legacy ? 10 * (c_d[c] + 1) : 10 * ((c_d[c] / 2) + 1);
      wr1(8'h17, 8'd128);
      if (!is_legacy) wr2(8'h55, lvl16);
      wr1(8'h0F, 8'h00);
      wr1(8'h1A, 8'h01);
      if (order) begin wr2(8'h0B, pre_w); wr2(8'h0C, tot_w); end
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05);
      polls = 0;
      rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      wr2(8'h0B, pre_w);
      wr2(8'h0C, tot_w);
      wr4(8'h0E, 32'h00FFFFFF);
      wr1(8'h01, 8'h00);
      dly = (pre_w + 3) * tw_ns + (($random(seed) & 32'h7FFFFFFF) % (6 * tw_ns * 1000)) / 1000.0 + 0.137;
      fork
        begin #(dly) sig = 1; end
      join_none
      // pc_hal_acq_poll until done
      polls = 0; done = 0;
      while (!done && polls < 200000) begin
        rd1(8'h0A); done = rd_d[0];
        rd1(8'h18); trig = rd_d[0];
        polls = polls + 1;
      end
      // pc_hal_acq_finish
      wr1(8'h0F, 8'h01);
      rd2(8'h14);
      ta = rd16 & 8191;
      bus_ta_ok = (ta == dut.trigger_address);
      sig = 0;
      start = (ta - pre_w) & 8191;
      // READ_RAW (pc_hal_read_core, dummy strobe): CH1 core A / low byte, core B / high byte
      read_core(8'h20, start, tot_w, 1);
      for (i = 0; i < tot_w; i = i + 1) ra[i] = rb[i];
      read_core(8'h21, start, tot_w, 1);
      for (i = 0; i < tot_w; i = i + 1) rbB[i] = rb[i];
      mem_bad = 0;
      for (i = 0; i < tot_w; i = i + 1)
        if (ra[i] !== dut.samples.mem[(start + i) & 8191][7:0] || rbB[i] !== dut.samples.mem[(start + i) & 8191][15:8])
          mem_bad = mem_bad + 1;
      // the same read without the dummy strobe (fpga_read_decim_data, the 1.00o5 readout): one word late
      read_core(8'h20, start, tot_w, 0);
      nd_bad = (rb[0] !== dut.samples.mem[start][7:0]);
      for (i = 1; i < tot_w; i = i + 1)
        if (rb[i] !== dut.samples.mem[(start + i - 1) & 8191][7:0]) nd_bad = nd_bad + 1;
      analyse(c, pre_w, tot_w);
    end
  endtask

  // where the step lies in the record
  task analyse(input integer c, input integer pre_w, input integer tot_w);
    begin
      ci = -1; cw = -1; o_s = 0; wcross = 0; tns = 0; fx = 0; lat = 0; ord_bad = 0;
      if (is_legacy) begin
        // core A crossing (the trigger compares core A only)
        for (i = 1; i < tot_w && cw < 0; i = i + 1)
          if (ra[i - 1] < 8'd128 && ra[i] >= 8'd128) cw = i;
        // interleaved by the true sampling instants
        for (i = 0; i < tot_w; i = i + 1) begin
          if (wB[(start + i) & 8191] < wA[(start + i) & 8191]) begin
            smp[2 * i] = rbB[i]; stt[2 * i] = wB[(start + i) & 8191];
            smp[2 * i + 1] = ra[i]; stt[2 * i + 1] = wA[(start + i) & 8191];
          end else begin
            smp[2 * i] = ra[i]; stt[2 * i] = wA[(start + i) & 8191];
            smp[2 * i + 1] = rbB[i]; stt[2 * i + 1] = wB[(start + i) & 8191];
          end
        end
        // position of the edge in the time-ordered record: the number of record samples taken before it (the A/B
        // pairing of a word need not hold the two nearest samples, so count instead of searching the word order)
        ci = 0;
        for (i = 0; i < 2 * tot_w; i = i + 1)
          if (stt[i] < te) begin
            ci = ci + 1;
            if (smp[i] >= 8'd128) ord_bad = ord_bad + 1;     // a sample before the edge reads HI
          end else if (smp[i] < 8'd128) ord_bad = ord_bad + 1;
        if (ci == 0 || ci == 2 * tot_w) ci = -1;
        o_s = ci - 2 * pre_w;
        wcross = cw - pre_w;
        tns = (te * 1.0 - wA[(start + pre_w) & 8191]) / 1000.0;  // edge after core A's sample of word pre
      end else begin
        for (i = 0; i < tot_w; i = i + 1) y[i] = {rbB[i], ra[i]};
        for (i = 1; i < tot_w && ci < 0; i = i + 1)
          if (y[i - 1] < lvl16 && y[i] >= lvl16) ci = i;
        o_s = ci - pre_w;
        if (ci > 0) begin
          fx  = (ci - 1) + (lvl16 * 1.0 - y[ci - 1]) / (y[ci] * 1.0 - y[ci - 1]) - pre_w;
          lat = (wt[(start + ci) & 8191] * 1.0 - te) / 1000.0;   // edge to the memory write of the crossing
        end
      end
      s_runs[c] = s_runs[c] + 1;
      if (stale_pre) s_stale[c] = s_stale[c] + 1;
      if (o_s < s_omin[c]) s_omin[c] = o_s;
      if (o_s > s_omax[c]) s_omax[c] = o_s;
      if (is_legacy) begin
        if (wcross < s_wmin[c]) s_wmin[c] = wcross;
        if (wcross > s_wmax[c]) s_wmax[c] = wcross;
        if (tns < s_tmin[c]) s_tmin[c] = tns;
        if (tns > s_tmax[c]) s_tmax[c] = tns;
      end else begin
        if (lat < s_tmin[c]) s_tmin[c] = lat;
        if (lat > s_tmax[c]) s_tmax[c] = lat;
        if (fx < s_fmin[c]) s_fmin[c] = fx;
        if (fx > s_fmax[c]) s_fmax[c] = fx;
      end
      $fdisplay(logf, "RUN case %0d K %0d fir %0d D %0d src %0d pre %0d tot %0d stb %0.0f stale %0d ta %0d wa_edge %0d trig %0d ci %0d off %0d wcross %0d tns %0.2f fx %0.3f lat %0.1f membad %0d ndbad %0d dab %0.2f",
                c, c_k[c], c_fir[c], c_d[c], c_src[c], pre_w, tot_w, stb, stale_pre, ta, wa_edge, trig, ci, o_s, wcross,
                tns, fx, lat, mem_bad, nd_bad, (wB[(start + pre_w) & 8191] * 1.0 - wA[(start + pre_w) & 8191]) / 1000.0);
      // A/B order: core B's sample of a word lies 5 ns after (or before) core A's, the same way in every run. 0x153D: the
      // encodes run at 100 MHz per core at every rate, so the two samples of a legacy word are half a full-rate word
      // (5 ns) apart whatever the divider (up to 0x153C: half the divided word, 5 (D + 1) ns)
      if (is_legacy) begin
        dab = (wB[(start + pre_w) & 8191] * 1.0 - wA[(start + pre_w) & 8191]) / 1000.0;
        if (dab_sign == 0) dab_sign = (dab > 0) ? 1 : -1;
        check(((dab > 0) ? 1 : -1) == dab_sign && (dab * dab_sign > 4.5) && (dab * dab_sign < 5.5),
              "legacy A/B order: core B 5 ns from core A at every rate (0x153D), same sign in every run");
      end
      check(done == 1 && trig == 1 && ci > 0, "capture triggered and the step is in the record");
      check(bus_ta_ok, "0x14 over the bus equals trigger_address");
      check(mem_bad == 0, "READ_RAW with the dummy strobe returns the record exactly");
      check(ord_bad == 0, "legacy: every record sample taken before the edge reads LO, every later one HI");
      check(nd_bad == 0, "the readout without the dummy strobe returns the record one word late");
    end
  endtask

  //---------------------------------------------------------------------------------
  // Phase 2 helpers: arm with the configuration written right after a write clock edge

  task arm_racing(input integer pre_w, input integer tot_w, input seg, input [3:0] m, input [11:0] cnt);
    begin
      wr1(8'h0F, 8'h00);
      wr1(8'h1A, 8'h01);
      wr1(8'h01, 8'h01);
      wr_cmd(8'h05);
      polls = 0;
      rd_byte;
      while (rd_d[0] !== 1'b1 && polls < 100000) begin rd_byte; polls = polls + 1; end
      @(posedge dut.sample_write_clock);
      #20;
      wr2(8'h0B, pre_w);
      wr2(8'h0C, tot_w);
      wr4(8'h0E, 32'h00FFFFFF);
      if (seg) begin
        wr1(8'h57, 8'h01);
        wr2(8'h58, {4'd0, cnt});
        wr1(8'h59, {4'd0, m});
        wr2(8'h5A, 16'd1);
        wr1(8'h5B, 8'h00);
        wr4(8'h5C, 32'd0);
      end
      wr1(8'h01, 8'h00);
    end
  endtask

  integer ref_off, slot1, k1, run_i, c, pre_r, tot_r, nruns;
  integer order_c [0:4095];
  integer tmp, ok_case, ext_case;
  reg [63:0] tab;
  reg [63:0] t_r2;
  reg [8*80-1:0] logname;
  real span;

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    if (!$value$plusargs("runs=%d", nruns)) nruns = 6;
    if (!$value$plusargs("pl=%d", pl)) pl = 4;
    if (!$value$plusargs("order=%d", order)) order = 0;
    if (!$value$plusargs("logfile=%s", logname)) logname = "trigpos.log";
    logf = $fopen(logname, "w");
    $display("tb_trigpos: seed %0d, %0d runs per case, ADC pipeline %0d encode cycles, config order %0d", seed, nruns, pl, order);
    #300;
    wr1(8'h50, 8'h00);                                  // generator off: EXT reads the loopback step
    wr1(8'h57, 8'h00);
    rd2(8'h06);
    $display("version %h", rd16);

    // cases: legacy (K = 0) at 200, 20, 1 and 0.5 MSa/s, and 16-bit K = 1..3 with and without the FIR at
    // N = 200 (1 MSa/s), N = 20 (10 MSa/s), N = 64 and N = 4; sources CH1 (level) and EXT
    add_case(2'd0, 1'b0, 32'd0,   2'd0); add_case(2'd0, 1'b0, 32'd0,   2'd2);
    add_case(2'd0, 1'b0, 32'd9,   2'd0); add_case(2'd0, 1'b0, 32'd9,   2'd2);
    add_case(2'd0, 1'b0, 32'd199, 2'd0); add_case(2'd0, 1'b0, 32'd199, 2'd2);
    add_case(2'd0, 1'b0, 32'd399, 2'd0);
    for (k1 = 1; k1 <= 3; k1 = k1 + 1) begin
      add_case(k1, 1'b0, 32'd199, 2'd0); add_case(k1, 1'b0, 32'd199, 2'd2);
      add_case(k1, 1'b1, 32'd199, 2'd0); add_case(k1, 1'b1, 32'd199, 2'd2);
    end
    add_case(2'd2, 1'b0, 32'd19, 2'd0); add_case(2'd2, 1'b1, 32'd19, 2'd0); add_case(2'd2, 1'b1, 32'd19, 2'd2);
    add_case(2'd3, 1'b1, 32'd63, 2'd0); add_case(2'd3, 1'b1, 32'd63, 2'd2);
    add_case(2'd2, 1'b0, 32'd3,  2'd0); add_case(2'd2, 1'b0, 32'd3,  2'd2);
    add_case(2'd1, 1'b0, 32'd1,  2'd0);

    //-------------------------------------------------------------------------------
    // Phase 1: shuffled runs
    for (i = 0; i < nc * nruns; i = i + 1) order_c[i] = i % nc;
    for (i = nc * nruns - 1; i > 0; i = i - 1) begin
      j = ($random(seed) & 32'h7FFFFFFF) % (i + 1);
      tmp = order_c[i]; order_c[i] = order_c[j]; order_c[j] = tmp;
    end
    for (run_i = 0; run_i < nc * nruns; run_i = run_i + 1) begin
      c = order_c[run_i];
      case (($random(seed) & 32'h7FFFFFFF) % 4)
        0: stb = 41.3;
        1: stb = 97.1;
        2: stb = 173.9;
        default: stb = 297.7;
      endcase
      configure(c);
      // pretrigger and record length vary per run (words)
      pre_r = 24 + (($random(seed) & 32'h7FFFFFFF) % 3) * 12;
      tot_r = pre_r + 60;
      capture(c, pre_r, tot_r);
      #(200 + (($random(seed) & 32'h7FFFFFFF) % 5000));
      if (run_i % 20 == 19) $display("  %0d of %0d runs, t = %0d us", run_i + 1, nc * nruns, $time / 1000);
    end

    // summary per case
    $display("");
    $display("case  K FIR    D src runs stale | crossing - trigger point (samples) | legacy: core A word | ns (legacy: edge - core A sample of word pre; 16-bit: edge to memory write) | 16-bit interpolated crossing - pre");
    for (c = 0; c < nc; c = c + 1) begin
      is_legacy = (c_k[c] == 0);
      ext_case = (c_src[c] == 2);
      tw_ns = is_legacy ? 10 * (c_d[c] + 1) : 10 * ((c_d[c] / 2) + 1);
      if (is_legacy)
        $display("%4d  %0d  %0d  %4d %3s %4d %5d | %4d .. %4d | %4d .. %4d | %9.2f .. %9.2f (word %0d ns)",
                 c, c_k[c], c_fir[c], c_d[c], ext_case ? "EXT" : "CH1", s_runs[c], s_stale[c],
                 s_omin[c], s_omax[c], s_wmin[c], s_wmax[c], s_tmin[c], s_tmax[c], tw_ns);
      else
        $display("%4d  %0d  %0d  %4d %3s %4d %5d | %4d .. %4d |            | %9.2f .. %9.2f (sample %0d ns) | %7.3f .. %7.3f",
                 c, c_k[c], c_fir[c], c_d[c], ext_case ? "EXT" : "CH1", s_runs[c], s_stale[c],
                 s_omin[c], s_omax[c], s_tmin[c], s_tmax[c], tw_ns, s_fmin[c], s_fmax[c]);
      $fdisplay(logf, "CASE %0d K %0d fir %0d D %0d src %0d runs %0d stale %0d off %0d %0d word %0d %0d ns %0.2f %0.2f frac %0.3f %0.3f",
                c, c_k[c], c_fir[c], c_d[c], c_src[c], s_runs[c], s_stale[c], s_omin[c], s_omax[c], s_wmin[c], s_wmax[c],
                s_tmin[c], s_tmax[c], s_fmin[c], s_fmax[c]);
      // determinism criteria
      if (is_legacy) begin
        // the step's core A word is fixed for a level trigger; EXT: within one word. Both: within one word of time.
        ok_case = ext_case ? (s_wmax[c] - s_wmin[c] <= 1) : (s_wmax[c] == s_wmin[c]);
        span = s_tmax[c] - s_tmin[c];
        ok_case = ok_case && (span < tw_ns + 0.5) && (s_omax[c] - s_omin[c] <= (ext_case ? 2 : 1));
      end else begin
        ok_case = ext_case ? (s_omax[c] - s_omin[c] <= 1) : (s_omax[c] == s_omin[c]);
        span = s_tmax[c] - s_tmin[c];
        ok_case = ok_case && (span < tw_ns + 0.5) && (s_fmax[c] - s_fmin[c] < 1.0 + 0.001);
      end
      check(ok_case, "trigger position deterministic across runs (case in the summary above)");
    end

    //-------------------------------------------------------------------------------
    // Phase 2: configuration race at a slow legacy rate (D = 399: write clock period 4 us), fast strobes
    $display("");
    $display("phase 2: configuration written right after a write clock edge, released before the next one");
    stb = 41.3;
    wr1(8'h57, 8'h00);
    configure(6);                                       // legacy D = 399, CH1
    ext_en = 0;
    // R1: same capture at pre 30 and pre 50; the step's word relative to pre must not change
    for (k1 = 0; k1 < 2; k1 = k1 + 1) begin
      pre_r = k1 ? 50 : 30;
      tot_r = pre_r + 40;
      tw_ns = 4000;
      wr1(8'h17, 8'd128);
      arm_racing(pre_r, tot_r, 1'b0, 4'd0, 12'd0);
      fork
        begin #((pre_r + 4) * 4000 + 777.7) sig = 1; end
      join_none
      polls = 0; done = 0;
      while (!done && polls < 200000) begin rd1(8'h0A); done = rd_d[0]; polls = polls + 1; end
      wr1(8'h0F, 8'h01);
      rd2(8'h14);
      ta = rd16 & 8191;
      sig = 0;
      start = (ta - pre_r) & 8191;
      read_core(8'h20, start, tot_r, 1);
      cw = -1;
      for (i = 1; i < tot_r && cw < 0; i = i + 1) if (rb[i - 1] < 128 && rb[i] >= 128) cw = i;
      $display("R1 pre %0d: stale preload %0d, trigger_address %0d, step at record word %0d (pre + %0d)", pre_r, stale_pre, ta, cw, cw - pre_r);
      $fdisplay(logf, "R1 pre %0d stale %0d ta %0d cw %0d", pre_r, stale_pre, ta, cw);
      if (k1 == 0) ref_off = cw - pre_r;
      else check(cw - pre_r == ref_off, "R1: a pretrigger written just before the release moves the record start with it");
      #20000;
    end

    // R2: modes-off capture, then a segment run (2 segments, m = 6) configured inside the same window
    wr1(8'h57, 8'h00);
    arm_racing(20, 40, 1'b0, 4'd0, 12'd0);
    fork begin #(26 * 4000 + 333.3) sig = 1; end join_none
    polls = 0; done = 0;
    while (!done && polls < 200000) begin rd1(8'h0A); done = rd_d[0]; polls = polls + 1; end
    sig = 0;
    #20000;
    slot1 = 0;
    t_r2 = $realtime * 1000.0;
    fork
      begin
        arm_racing(10, 40, 1'b1, 4'd6, 12'd2);
      end
      begin
        // a step per segment, once the pretrigger of that record has filled
        wait (dut.sample_system_reset === 1'b1);
        wait (dut.sample_system_reset === 1'b0);
        #(14 * 4000 + 123.4) sig = 1;
        wait (dut.q_cnt == 1);
        sig = 0;
        #(14 * 4000 + 456.7) sig = 1;
        wait (dut.q_done == 1'b1);
        sig = 0;
      end
    join
    polls = 0; done = 0;
    while (!done && polls < 200000) begin rd1(8'h0A); done = rd_d[0]; polls = polls + 1; end
    for (i = 64; i < 128; i = i + 1) if (wt[i] > t_r2) slot1 = slot1 + 1;
    // table entry 1 over the bus (0x1F = 1, dummy strobe, one pass per byte)
    tab = 0;
    for (k1 = 0; k1 < 8; k1 = k1 + 1) begin
      read_core(8'h2B + k1, 1, 1, 1);
      tab[8 * k1 +: 8] = rb[0];
    end
    $display("R2 segment run after a modes-off arm: %0d words written in slot 1 since its step, table entry 1 = %h", slot1, tab);
    $fdisplay(logf, "R2 slot1 %0d entry1 %h", slot1, tab);
    check(done == 1, "R2: segment run completes");
    check(slot1 >= 30 && tab[63] == 1'b1 && tab[53] == 1'b1, "R2: the slot mask written just before the release applies to the run (segment 1 in slot 1)");
    wr1(8'h57, 8'h00);

    $fclose(logf);
    $display("RESULT: %0d checks, %0d failed -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #2_000_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
