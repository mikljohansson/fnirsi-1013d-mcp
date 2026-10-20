// Testbench for the CIC compensation FIR (fpga/al3/src/zaklad.v, version 0x1535).
// Run with fpga/al3/tb/run.sh (iverilog); the FIR outputs are checked by cic_model.py.
// Two instances share every input:
//   dut  = src/zaklad.v (module fnirsi_1013D)
//   refd = the 0x1534 zaklad.v from git (REF1534_REV), modules renamed with a _ref suffix
// Phase 1 (equivalence, FIR off or not eligible): random captures with random K = 0..3, core
//   order and capture edges, shift, 16-bit level, dividers, trigger source CH1/CH2/EXT, generator
//   and EXT activity. 0x56 gets random bytes; when bit 0 is set the divider is one the FIR does not
//   engage at (N < 16 or N not a multiple of 4). Output pins, capture registers, the decimator
//   outputs and strobes, every MCU bus read and the whole sample memory after every capture must be
//   identical to 0x1534: the bypass is exact.
// Phase 2 (FIR, K = 1..3): random ADC data (and a telegraph signal for saturation and the clamp
//   at 0); every write clock edge is logged to +logfile for cic_model.py, which recomputes each
//   stored sample (CIC by N/2, 16-bit saturation, 15-tap FIR, rounding, clamp).
// Phase 3 (directed): version word, 0x29 FIR bits, trigger position on the FIR stream against the
//   K = 0 convention, EXT with the FIR, EXT data lag measured against the formula, auto time-out.
// Phase 4: FIR off again, memory resynchronised, random equivalence with 0x1534 again.
`timescale 1ns / 1ps

module tb;

  //---------------------------------------------------------------------------------
  // Shared stimulus

  reg i_xtal = 0;
  always #10 i_xtal = ~i_xtal;

  reg       mcu_clk  = 1;
  reg       mcu_rws  = 1;
  reg       mcu_dcs  = 0;
  reg [7:0] mcu_dout = 0;

  reg [7:0] adc1A = 0, adc1B = 0, adc2A = 0, adc2B = 0;

  wire [7:0] bus_dut, bus_ref;
  assign bus_dut = mcu_rws ? mcu_dout : 8'bz;
  assign bus_ref = mcu_rws ? mcu_dout : 8'bz;

  reg  ext_en  = 0;
  reg  ext_val = 0;
  wire pad_dut, pad_ref;
  assign pad_dut = ext_en ? ext_val : 1'bz;
  assign pad_ref = ext_en ? ext_val : 1'bz;
  assign (weak0, weak1) pad_dut = 1'b0;
  assign (weak0, weak1) pad_ref = 1'b0;

  //---------------------------------------------------------------------------------
  // Instances

  wire d_e1A, d_e1B, d_e2A, d_e2B, d_o1, d_r11, d_r12, d_r13, d_ac1, d_o2, d_r21, d_r22, d_r23, d_ac2, d_pwm;
  wire r_e1A, r_e1B, r_e2A, r_e2B, r_o1, r_r11, r_r12, r_r13, r_ac1, r_o2, r_r21, r_r22, r_r23, r_ac2, r_pwm;

  fnirsi_1013D dut (
    .i_xtal(i_xtal), .i_mcu_clk(mcu_clk), .i_mcu_rws(mcu_rws), .i_mcu_dcs(mcu_dcs),
    .i_adc1A_d(adc1A), .i_adc1B_d(adc1B), .i_adc2A_d(adc2A), .i_adc2B_d(adc2B),
    .io_mcu_data(bus_dut), .o_1khz_calib(pad_dut),
    .o_adc1_encA(d_e1A), .o_adc1_encB(d_e1B), .o_adc2_encA(d_e2A), .o_adc2_encB(d_e2B),
    .o_offset_1(d_o1), .o_relay1_1(d_r11), .o_relay1_2(d_r12), .o_relay1_3(d_r13), .o_ac_dc_1(d_ac1),
    .o_offset_2(d_o2), .o_relay2_1(d_r21), .o_relay2_2(d_r22), .o_relay2_3(d_r23), .o_ac_dc_2(d_ac2),
    .o_pwm_display(d_pwm));

  // 0x153A: the dut registers each core's pins on clk_200MHz at its own phase and hands them to the capture logic as
  // adc*_w, loaded on the rising write clock edge. The reference (which takes the pins on that edge) gets exactly those
  // values 0.2 ns after each edge (shorter than the 0.37 ns phase of the equivalence sampler below), so both capture the
  // same samples on the same edges and the comparison stays exact.
  reg [7:0] r_adc1A = 0, r_adc1B = 0, r_adc2A = 0, r_adc2B = 0;
  always @(dut.adc1A_w) r_adc1A <= #0.2 dut.adc1A_w;
  always @(dut.adc1B_w) r_adc1B <= #0.2 dut.adc1B_w;
  always @(dut.adc2A_w) r_adc2A <= #0.2 dut.adc2A_w;
  always @(dut.adc2B_w) r_adc2B <= #0.2 dut.adc2B_w;

  fnirsi_1013D_ref refd (
    .i_xtal(i_xtal), .i_mcu_clk(mcu_clk), .i_mcu_rws(mcu_rws), .i_mcu_dcs(mcu_dcs),
    .i_adc1A_d(r_adc1A), .i_adc1B_d(r_adc1B), .i_adc2A_d(r_adc2A), .i_adc2B_d(r_adc2B),
    .io_mcu_data(bus_ref), .o_1khz_calib(pad_ref),
    .o_adc1_encA(r_e1A), .o_adc1_encB(r_e1B), .o_adc2_encA(r_e2A), .o_adc2_encB(r_e2B),
    .o_offset_1(r_o1), .o_relay1_1(r_r11), .o_relay1_2(r_r12), .o_relay1_3(r_r13), .o_ac_dc_1(r_ac1),
    .o_offset_2(r_o2), .o_relay2_1(r_r21), .o_relay2_2(r_r22), .o_relay2_3(r_r23), .o_ac_dc_2(r_ac2),
    .o_pwm_display(r_pwm));

  wire [14:0] out_dut = {d_e1A, d_e1B, d_e2A, d_e2B, d_o1, d_r11, d_r12, d_r13, d_ac1, d_o2, d_r21, d_r22, d_r23, d_ac2, d_pwm};
  wire [14:0] out_ref = {r_e1A, r_e1B, r_e2A, r_e2B, r_o1, r_r11, r_r12, r_r13, r_ac1, r_o2, r_r21, r_r22, r_r23, r_ac2, r_pwm};
  // 0x1537: the four encode pins are not compared. The references flip their A/B encode polarity when an
  // asynchronous 0x0D / 0x53 write lands between the two enable samples; 0x1537 repairs it at the next enable
  // (README "Changes" section 7). The ADC data here do not depend on the encodes; tb_trigpos.v checks the order.
  wire [14:0] out_cmp_mask = 15'h07FF;

  // 0x153B: after the stop the dut's trigger_address (time-out: W - total + pretrigger) and sampling_triggered (no late
  // trigger) differ from the references by design, and trigger_read_address is frozen at the stop (the references run it on
  // after a 0x0E or 0x1A write); the three are compared while the write runs (tb_timeout.v has the rest).
  // fpga-gen12: ext_pending too. The gate on trigger_check_enable also stops the EXT edge latch, so an EXT edge after the
  // stop sets the reference's ext_pending (and then its sampling_triggered) and not the dut's, until the next capture
  // reset clears both. Its only consumer is sampling_triggered (trigger_detect_enable feeds nothing that is used) and it
  // is not on the bus (0x28 reads ext_activity, unchanged), so after the stop it has no effect that is not already covered.
  wire [12:0] ta_cmp_dut = dut.sample_write_enable ? dut.trigger_address : 13'd0;
  wire [12:0] ta_cmp_ref = dut.sample_write_enable ? refd.trigger_address : 13'd0;
  // fpga-gen12: sampling_triggered (and ext_pending) also not during the reset: a post-stop trigger of the reference is
  // cleared by cap_rst200 on the clk_200MHz edge after the write clock edge that raised sample_write_enable (2.5 ns)
  wire        tr_cmp_dut = (dut.sample_write_enable & ~dut.cap_rst200) ? dut.sampling_triggered : 1'b0;
  wire        tr_cmp_ref = (dut.sample_write_enable & ~dut.cap_rst200) ? refd.sampling_triggered : 1'b0;
  wire [12:0] ra_cmp_dut = dut.sample_write_enable ? dut.trigger_read_address : 13'd0;
  wire [12:0] ra_cmp_ref = dut.sample_write_enable ? refd.trigger_read_address : 13'd0;
  wire        ep_cmp_dut = (dut.sample_write_enable & ~dut.cap_rst200) ? dut.ext_pending : 1'b0;
  wire        ep_cmp_ref = (dut.sample_write_enable & ~dut.cap_rst200) ? refd.ext_pending : 1'b0;
  wire [71:0] st_dut = {ta_cmp_dut, ra_cmp_dut, dut.sample_write_address,
                        tr_cmp_dut, dut.sample_write_enable, dut.sampling_half_way,
                        dut.sample_system_state, dut.sample_read_offset, dut.sample_write_clock,
                        dut.ext_activity, ep_cmp_dut, dut.ext_level, 12'd0};
  wire [71:0] st_ref = {ta_cmp_ref, ra_cmp_ref, refd.sample_write_address,
                        tr_cmp_ref, refd.sample_write_enable, refd.sampling_half_way,
                        refd.sample_system_state, refd.sample_read_offset, refd.sample_write_clock,
                        refd.ext_activity, ep_cmp_ref, refd.ext_level, 12'd0};
  // decimator: memory data, write strobe, capture advance, trigger compare data
  wire [50:0] dc_dut = {dut.dec_out2, dut.dec_out1, dut.dec_wr, dut.swc_adv, dut.swc_adv_q, dut.trigger_data,
                        dut.samples.dia[7:0]};
  wire [50:0] dc_ref = {refd.dec_out2, refd.dec_out1, refd.dec_wr, refd.swc_adv, refd.swc_adv_q, refd.trigger_data,
                        refd.samples.dia[7:0]};

  //---------------------------------------------------------------------------------
  // Bookkeeping

  integer errors = 0;
  integer checks = 0;
  integer eq_mismatch = 0;
  integer eq_samples = 0;
  reg     eq_on = 0;
  // 0x153B: a reference ends a triggered auto-mode capture at its time-out, the dut completes the post-trigger words
  // (README section 11 change 2); from the edge the reference stops on while the dut still writes, that capture is
  // not compared (state, outputs, bus reads). Counted per phase.
  reg     cut_ref = 0;
  integer cut_caps = 0;

  task check(input cond, input [8*100-1:0] what);
    begin
      checks = checks + 1;
      if (!cond) begin
        errors = errors + 1;
        $display("FAIL  t=%0t  %0s", $time, what);
      end
    end
  endtask

  integer mm0;
  initial begin
    #0.37;
    forever begin
      #1;
      if (eq_on && !cut_ref && dut.sampling_triggered && dut.sample_write_enable && !refd.sample_write_enable) begin
        cut_ref  = 1;
        cut_caps = cut_caps + 1;
      end
      // 0x153B: a 0x1A = 0 written while a capture runs (the testbench reconfigures after a capture that never triggered)
      // stops the reference on the write and the dut two write clocks later (synchroniser): not compared in between
      if (eq_on && !cut_ref && !(dut.sample_write_enable && !refd.sample_write_enable && !dut.sampling_triggered &&
                                  dut.time_base_timeout && !dut.trigger_mode)) begin
        eq_samples = eq_samples + 1;
        if ((out_dut & out_cmp_mask) !== (out_ref & out_cmp_mask) || st_dut !== st_ref || pad_dut !== pad_ref || dc_dut !== dc_ref) begin
          eq_mismatch = eq_mismatch + 1;
          if (eq_mismatch <= 30)
            $display("MISMATCH t=%0t out %h/%h st %h/%h dc %h/%h pad %b/%b  mode %b/%b mode_s %b tmo %b/%b cnt %0d/%0d set %0d/%0d trig %b/%b en %b/%b swe %b/%b ta %0d/%0d", $time, out_dut, out_ref, st_dut, st_ref,
                     dc_dut, dc_ref, pad_dut, pad_ref,
                     dut.trigger_mode, refd.trigger_mode, dut.trigger_mode_s, dut.time_base_timeout, refd.time_base_timeout, dut.time_base_cnt, refd.time_base_cnt,
                     dut.time_base_set, refd.time_base_set, dut.sampling_triggered, refd.sampling_triggered, dut.sampling_enable, refd.sampling_enable,
                     dut.sample_write_enable, refd.sample_write_enable, dut.trigger_address, refd.trigger_address);
        end
      end
    end
  end

  // 0x153B: words the dut wrote while the reference had already stopped (a triggered auto-mode capture the reference
  // cut at its time-out) differ until both write them again; they are skipped by the memory compare and counted
  reg     mem_dirty [0:8191];
  integer mdi, mem_dirty_n;
  initial for (mdi = 0; mdi < 8192; mdi = mdi + 1) mem_dirty[mdi] = 0;
  always @(posedge dut.sample_write_clock)
    if (dut.sample_write_enable && dut.swc_adv)
      mem_dirty[dut.sample_write_address_phys] <= !refd.sample_write_enable;

  integer mi, mem_diff;
  task compare_memory;
    begin
      mem_diff = 0; mem_dirty_n = 0;
      for (mi = 0; mi < 8192; mi = mi + 1)
        if (mem_dirty[mi]) mem_dirty_n = mem_dirty_n + 1;
        else if (dut.samples.mem[mi] !== refd.samples.mem[mi]) mem_diff = mem_diff + 1;
      eq_samples = eq_samples + 1;
      if (mem_diff != 0) begin
        eq_mismatch = eq_mismatch + 1;
        if (eq_mismatch <= 30) $display("MISMATCH t=%0t sample memory: %0d words differ (%0d skipped as the dut's post-time-out words)", $time, mem_diff, mem_dirty_n);
      end
    end
  endtask

  //---------------------------------------------------------------------------------
  // ADC stimulus, updated every 7 ns at times that never coincide with a clock edge
  // adc_mode 0 = triangles, 1 = random bytes, 2 = step (step_val selects step_lo / step_hi,
  // CH2 gets the complement step), 3 = flat, 4 = telegraph (0 / 255, random switching; CH2 inverted),
  // 5 = CH1 ramp from ramp_lo (ramp_slope LSB/ns from ramp_t0 while ramp_on, rounded, up to 240), CH2 flat

  integer    adc_mode = 0;
  reg        step_val = 0;
  reg  [7:0] step_lo = 50, step_hi = 200;
  reg  [7:0] flat_val = 0;
  reg        tele = 0;
  reg        ramp_on = 0;
  real       ramp_lo = 40.0, ramp_slope = 0.0, ramp_t0 = 0.0, ramp_v;
  integer    triA = 0, triB = 0, d1 = 3, d2 = -5;
  integer    seed;
  reg [31:0] r7;

  initial begin
    #0.3;
    forever begin
      #7;
      triA = triA + d1; if (triA > 250) begin triA = 250; d1 = -d1; end if (triA < 5) begin triA = 5; d1 = -d1; end
      triB = triB + d2; if (triB > 250) begin triB = 250; d2 = -d2; end if (triB < 5) begin triB = 5; d2 = -d2; end
      case (adc_mode)
        0: begin adc1A = triA; adc1B = triA + 1; adc2A = triB; adc2B = triB + 2; end
        1: begin adc1A = $random(seed); adc1B = $random(seed); adc2A = $random(seed); adc2B = $random(seed); end
        2: begin
             adc1A = step_val ? step_hi : step_lo; adc1B = adc1A;
             adc2A = step_val ? step_lo : step_hi; adc2B = adc2A;
           end
        4: begin
             r7 = $random(seed);
             if (r7[5:0] == 0) tele = ~tele;
             adc1A = tele ? 8'd255 : 8'd0; adc1B = adc1A;
             adc2A = ~adc1A; adc2B = adc2A;
           end
        5: begin
             ramp_v = ramp_on ? ramp_lo + ($realtime - ramp_t0) * ramp_slope : ramp_lo;
             if (ramp_v > 240.0) ramp_v = 240.0;
             adc1A = $rtoi(ramp_v + 0.5); adc1B = adc1A;
             adc2A = 8'd128; adc2B = 8'd128;
           end
        default: begin adc1A = flat_val; adc1B = flat_val; adc2A = flat_val; adc2B = flat_val; end
      endcase
    end
  end

  //---------------------------------------------------------------------------------
  // Write clock edge log for cic_model.py (format as tb_decim.v, marker with the FIR field)
  // E <edge> <1A> <1B> <2A> <2B> <wr> <addr> <dia> <n1A> <n1B> <n2A> <n2B>
  // C <K> <divider> <swap> <shift> <neg_a> <neg_b> <fir>

  integer logf = 0;
  reg     log_on = 0;
  integer edge_n = 0;
  reg [7:0] n1A = 0, n1B = 0, n2A = 0, n2B = 0;
  realtime wtime [0:8191];                              // time of the last write per address
  always @(negedge dut.sample_write_clock) begin
    n1A = dut.adc1A_w; n1B = dut.adc1B_w; n2A = dut.adc2A_w; n2B = dut.adc2B_w;   // 0x153A: the decimator's inputs
  end
  always @(posedge dut.sample_write_clock) begin
    edge_n = edge_n + 1;
    if (dut.sample_write_enable & dut.swc_adv) wtime[dut.sample_write_address] = $realtime;
    if (log_on)
      $fdisplay(logf, "E %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d", edge_n, dut.adc1A_w, dut.adc1B_w, dut.adc2A_w, dut.adc2B_w,
                dut.sample_write_enable & dut.swc_adv, dut.sample_write_address, dut.samples.dia,
                n1A, n1B, n2A, n2B);
  end

  //---------------------------------------------------------------------------------
  // MCU bus

  task mcu_strobe;
    begin
      #13.1 mcu_clk = 0;
      #17   mcu_clk = 1;
      #13;
    end
  endtask

  task wr_cmd(input [7:0] c);
    begin
      mcu_rws = 1; mcu_dcs = 1; mcu_dout = c;
      mcu_strobe;
      mcu_dcs = 0;
    end
  endtask

  task wr_byte(input [7:0] d);
    begin
      mcu_rws = 1; mcu_dcs = 0; mcu_dout = d;
      mcu_strobe;
    end
  endtask

  reg [7:0] rd_d, rd_r;
  integer   bus_cmp_on = 0;
  task rd_byte;
    begin
      mcu_rws = 0; mcu_dcs = 0;
      mcu_strobe;
      rd_d = bus_dut; rd_r = bus_ref;
      if (eq_on && bus_cmp_on && !cut_ref) begin
        eq_samples = eq_samples + 1;
        if (rd_d !== rd_r) begin
          eq_mismatch = eq_mismatch + 1;
          if (eq_mismatch <= 30) $display("MISMATCH t=%0t bus read %h/%h cmd %h/%h", $time, rd_d, rd_r, dut.mcu_command, refd.mcu_command);
        end
      end
      mcu_rws = 1;
      #5;
    end
  endtask

  task wr1(input [7:0] c, input [7:0] d);
    begin wr_cmd(c); wr_byte(d); end
  endtask

  task wr2(input [7:0] c, input [15:0] v);
    begin wr_cmd(c); wr_byte(v[15:8]); wr_byte(v[7:0]); end
  endtask

  task wr4(input [7:0] c, input [31:0] v);
    begin wr_cmd(c); wr_byte(v[31:24]); wr_byte(v[23:16]); wr_byte(v[15:8]); wr_byte(v[7:0]); end
  endtask

  reg [15:0] rd16_d, rd16_r;
  task rd2(input [7:0] c);
    begin
      wr_cmd(c);
      rd_byte; rd16_d[15:8] = rd_d; rd16_r[15:8] = rd_r;
      rd_byte; rd16_d[7:0]  = rd_d; rd16_r[7:0]  = rd_r;
    end
  endtask

  reg [7:0] rd8_d, rd8_r;
  task rd1(input [7:0] c);
    begin wr_cmd(c); rd_byte; rd8_d = rd_d; rd8_r = rd_r; end
  endtask

  //---------------------------------------------------------------------------------
  // Capture helpers

  integer polls;
  task start_capture;
    begin
      cut_ref = 0;
      wr1(8'h01, 8'h01);
      polls = 0;
      rd1(8'h05);
      while (rd8_d[0] !== 1'b1 && polls < 200000) begin rd1(8'h05); polls = polls + 1; end
      wr1(8'h01, 8'h00);
    end
  endtask

  reg done;
  task wait_done(input integer max_polls);
    begin
      polls = 0; done = 0;
      rd1(8'h0A);
      while (rd8_d[0] !== 1'b1 && polls < max_polls) begin rd1(8'h0A); polls = polls + 1; end
      done = (rd8_d[0] === 1'b1);
    end
  endtask

  task setup_capture(input [1:0] src, input edg, input mode, input [31:0] divider,
                     input [12:0] pre, input [12:0] total);
    begin
      wr1(8'h15, {6'd0, src});
      wr1(8'h16, {7'd0, edg});
      wr1(8'h1A, {7'd0, mode});
      wr4(8'h0D, divider);
      wr2(8'h0B, {3'd0, pre});
      wr2(8'h0C, {3'd0, total});
      wr4(8'h0E, 32'h00FFFFFF);
    end
  endtask

  // Decimation and FIR configuration, then settling: (K + 10) output periods plus the pipeline
  reg [1:0] negab = 0;                                  // 0x53 bits 4..3 used by set_fir
  task set_fir(input [1:0] k, input swap, input [5:0] sh, input [31:0] divider, input fir);
    begin
      wr1(8'h56, {7'd0, fir});
      wr1(8'h53, {3'd0, negab, swap, k});
      wr1(8'h54, {2'd0, sh});
      wr4(8'h0D, divider);
      #((k + 10) * ((divider / 2) + 1) * 10 + 500);
    end
  endtask

  // Recommended shift for a CIC gain of m^K: max(0, bitlen(255 * m^K) - 16)
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

  // CIC path (0x1534): gain N^K. FIR path: gain (N/2)^K, firmware rule shift + 1.
  function [5:0] rec_shift(input [1:0] k, input [31:0] divider);
    rec_shift = shift_for(k, 2 * ((divider / 2) + 1));
  endfunction
  function [5:0] rec_shift_fir(input [1:0] k, input [31:0] divider);
    rec_shift_fir = shift_for(k, (divider / 2) + 1) + 1;
  endfunction

  // the FIR engages for K >= 1 at divider >= 14 with bit 1 set (N = 2P, P even, P >= 8)
  function eligible(input [31:0] divider);
    eligible = (divider >= 14) && divider[1];
  endfunction

  function [15:0] mem_sample(input [12:0] addr, input ch, input dec);
    reg [31:0] w;
    begin
      w = dut.samples.mem[addr];
      if (dec) mem_sample = ch ? w[31:16] : w[15:0];
      else     mem_sample = ch ? {8'd0, w[23:16]} : {8'd0, w[7:0]};
    end
  endfunction

  // Main sequence variables
  integer delta0, delta_r, delta_f, kq, nq, e, ok29;
  integer divs [0:4];
  reg [12:0] ta, wa_at_edge;
  reg [15:0] lvl16;
  reg [8*64-1:0] logname;
  reg [255:0] msg;
  integer total_w;
  integer p1n;

  // 16-bit level halfway between the plateaus 50 * G and 200 * G, G = m^K >> shift (m = N or N/2)
  function [15:0] lvl_for(input [1:0] k, input [31:0] m, input [5:0] sh);
    reg [127:0] g;
    integer i;
    begin
      g = 125;
      for (i = 0; i < k; i = i + 1) g = g * m;
      lvl_for = g >> sh;
    end
  endfunction

  // First crossing in the stored record [1, total): rising = prev < L <= cur, falling = prev >= L > cur
  integer cross_i;
  task find_cross(input ch, input dec, input edg, input [15:0] lvl, input integer total);
    integer i;
    reg [15:0] a, b;
    begin
      cross_i = -1;
      for (i = 1; i < total && cross_i < 0; i = i + 1) begin
        a = mem_sample(i - 1, ch, dec);
        b = mem_sample(i, ch, dec);
        if (!edg && a < lvl && b >= lvl) cross_i = i;
        if ( edg && a >= lvl && b < lvl) cross_i = i;
      end
    end
  endtask

  //---------------------------------------------------------------------------------
  // Phase 1 / 4: random equivalence with 0x1534 (FIR off, or on at a divider it does not engage at)

  integer it, k, nread;
  reg [31:0] rnd, rnd2, dv;
  reg [12:0] pre_r, tot_r;
  reg [1:0]  kr;
  reg [5:0]  shr;
  reg gen_on;
  integer fir_set_cnt = 0;

  // dividers: the FIR-eligible ones are used only with 0x56 bit 0 = 0
  function [31:0] pick_div(input [31:0] r, input fir_bit, input [1:0] kk);
    reg [31:0] d;
    begin
      if (kk == 0)
        d = r[1:0] + (r[2] ? 32'd0 : 32'd6);              // write clock 100 .. 12.5 MHz, as tb_decim
      else if (r[3])
        d = r[7:4];                                        // 0 .. 15
      else
        d = 32'd14 + r[8:4];                               // 14 .. 45
      if (fir_bit && kk != 0 && eligible(d))
        d = d & ~32'd2;                                    // clear bit 1: N not a multiple of 4
      pick_div = d;
    end
  endfunction

  task random_phase(input integer iterations);
    begin
      eq_on = 1; bus_cmp_on = 1;
      for (it = 0; it < iterations; it = it + 1) begin
        rnd = $random(seed); rnd2 = $random(seed);
        kr = rnd2[1:0];
        dv = pick_div($random(seed), rnd[12], kr);
        if (rnd[12] && kr != 0) fir_set_cnt = fir_set_cnt + 1;
        shr = (kr == 0) ? rnd2[13:8] : rec_shift(kr, dv) - ((rnd2[3] && rec_shift(kr, dv) > 0) ? 6'd1 : 6'd0);
        wr1(8'h56, 8'h00);                                  // off while K and the divider change
        wr1(8'h53, {3'd0, 2'b00, rnd[7], kr});        // 0x153A: bits 3 / 4 are inert in the dut, keep the reference equal
        wr1(8'h54, shr);
        wr2(8'h55, (kr == 0) ? $random(seed) : (16'd128 << (rnd2[7:6])) + rnd2[31:20]);
        wr1(8'h15, {6'd0, rnd[9:8]});                     // CH1, CH2, EXT, EXT
        wr1(8'h16, {7'd0, rnd[1]});
        wr1(8'h17, 8'd60 + (rnd[15:8] % 8'd130));
        wr1(8'h1A, {7'd0, rnd[2]});
        wr1(8'h0F, {7'd0, rnd[3]});
        wr4(8'h0D, dv);
        wr1(8'h56, {rnd[20:14], rnd[12]});                  // bits 7..1 random (reserved, ignored)
        pre_r = 13'd20 + (rnd[24:16] % 13'd200);
        tot_r = pre_r + 13'd20 + ($random(seed) & 13'h00FF);
        wr2(8'h0B, {3'd0, pre_r});
        wr2(8'h0C, {3'd0, tot_r});
        wr4(8'h0E, (kr == 0) ? 32'd2000 + ($random(seed) & 32'h3FFF) : {19'd0, tot_r} + ($random(seed) & 32'h00FF));
        wr2(8'h32, $random(seed)); wr2(8'h35, $random(seed));
        wr1(8'h33, $random(seed) % 6); wr1(8'h36, $random(seed) % 6);
        wr1(8'h34, $random(seed)); wr1(8'h37, $random(seed));
        wr2(8'h38, $random(seed));
        wr2(8'h3C, $random(seed));
        gen_on = rnd[6];
        wr1(8'h50, {7'd0, gen_on});
        wr4(8'h51, 32'd85899346 + ($random(seed) & 32'h00FFFFFF));
        wr4(8'h52, $random(seed));
        d1 = 1 + (rnd[27:25]); d2 = -(1 + rnd[30:28]);
        adc_mode = rnd[11] ? 1 : 0;
        if (!gen_on) begin
          ext_en = 1;
          fork
            begin : ext_noise
              repeat (200) begin
                #(3 + ($random(seed) & 1023));
                ext_val = ~ext_val;
              end
            end
          join_none
        end else
          ext_en = 0;
        start_capture;
        wait_done(100000);
        // 0x153B: a capture that ended on the auto time-out reports W - total + pretrigger (reference: pretrigger) and
        // never a trigger after the stop: these two reads are checked against the formula instead of the reference
        bus_cmp_on = rnd[2];
        rd2(8'h14);
        rd1(8'h18);
        if (!rnd[2] && !rd_d[0])
          check(rd16_d[12:0] == ((dut.w_last - tot_r + pre_r) & 13'h1FFF), "auto time-out: 0x14 = W - total + pretrigger");
        rd2(8'h41);                                       // refills the read register's stale bits identically in both
        bus_cmp_on = 1;
        rd1(8'h28);
        rd2(8'h41);
        wr2(8'h1F, {3'd0, rd16_d[12:0]});
        nread = 4 + (rnd[31:30] * 4);
        for (k = 0; k < nread; k = k + 1) rd1(8'h20 + (k % 4));
        // live ADC pins: the dut returns its IOB registers, the reference its (delayed) pins: not compared; 0x41 then
        // refills all eight bits of the read register (0x05 / 0x0A / 0x18 keep the others)
        bus_cmp_on = 0;
        for (k = 0; k < 4; k = k + 1) rd1(8'h24 + k);
        bus_cmp_on = 1;
        rd2(8'h41);
        rd1(8'h05);
        disable ext_noise;
        ext_en = 0;
        wr1(8'h01, 8'h00);
        #100;
        compare_memory;
      end
      eq_on = 0; bus_cmp_on = 0;
      adc_mode = 0;
    end
  endtask

  //---------------------------------------------------------------------------------
  // Phase 2: logged FIR captures for the reference model

  task fir_run(input [1:0] kk, input swap, input [5:0] sh, input [31:0] divider, input [12:0] total);
    begin
      set_fir(kk, swap, sh, divider, 1'b1);
      setup_capture(2'd0, 1'b0, 1'b0, divider, 13'd10, total);
      wr4(8'h0E, {19'd0, total});                        // auto mode: stops after about total samples
      $fdisplay(logf, "C %0d %0d %0d %0d 0 0 1", kk, divider, swap, sh);   // 0x153A: 0x53 bits 3 / 4 are inert
      check(dut.fir_act === 1'b1, "FIR engaged for the logged capture");
      log_on = 1;
      start_capture;
      wait_done(400000);
      log_on = 0;
      check(done, "FIR logged capture completes");
    end
  endtask

  //---------------------------------------------------------------------------------
  // EXT data lag. At t0 the EXT pin rises and CH1 starts a ramp from ramp_lo to 240 (adc_mode 5).
  // A linear-phase filter with DC gain 1 turns a ramp into the same ramp delayed by its group delay,
  // so every stored sample whose filter span lies on the ramp gives the lag directly:
  //   lag = w - t0 - (y / G - ramp_lo) / slope,  w = write time, G = 16-bit gain (m^K / 2^shift).
  // Predicted lag: CIC path 120 + 2.5 K (N - 1) ns, FIR path 250 + 17.5 N + 1.25 K (N - 2) ns
  // (pipeline in write clocks plus the impulse-response centre), plus the 0x153A capture: the decimator
  // takes adc*_w one write clock after it loaded, and adc*_w holds the pins from 1.5 T + 5 + 2.5 k (core
  // A, k = 4: 30 ns) and 2 T + 5 + 2.5 k (core B, k = 4: 35 ns) before that edge, so the pair centre is
  // 42.5 ns earlier than the pins the 0x1535 model took at the edge (+42.5 in the formula). Stimulus
  // artefacts: the pins hold each value for 7 ns (+3.5 ns) and the two cores of a pair are 5 ns apart as on
  // the hardware; the measurement reads about +1 .. +3 ns above the formula (0x1535: about +1 ns).
  // Also reported: where the EXT edge lies in the record, trigger_address + (t0 - w_ta + lag) / Tout,
  // in output samples; t0 - w_ta is the edge phase after the last write (0 .. 1 output periods).

  real t_e, lag_meas, lag_pred, tout, pos, slope, g16, t_end, est, wsum;
  integer nest;
  task ext_lag(input [1:0] kk, input [31:0] divider, input fir, input real phase);
    integer i, n_out;
    reg [5:0] sh;
    begin
      sh = fir ? rec_shift_fir(kk, divider) : rec_shift(kk, divider);
      adc_mode = 5; ramp_on = 0;
      set_fir(kk, 1'b0, sh, divider, fir);
      n_out = 2 * ((divider / 2) + 1);
      tout = 5.0 * n_out;
      lag_pred = (fir ? 250.0 + 17.5 * n_out + 1.25 * kk * (n_out - 2) : 120.0 + 2.5 * kk * (n_out - 1)) + 42.5;
      g16 = 1.0;
      for (i = 0; i < kk; i = i + 1) g16 = g16 * (fir ? (divider / 2) + 1 : n_out);
      g16 = g16 / (2.0 ** sh);
      slope = (240.0 - ramp_lo) / (2.0 * lag_pred + 30.0 * tout);    // ADC LSB per ns
      ramp_slope = slope;
      ext_en = 1; ext_val = 0;
      setup_capture(2'd2, 1'b0, 1'b1, divider, 13'd60, 13'd160);
      start_capture;
      wait (dut.sampling_half_way === 1'b1);
      #((kk + 12) * tout + 500);
      @(posedge dut.sample_write_clock);
      while (!(dut.dec_wr === 1'b1)) @(posedge dut.sample_write_clock);
      #(phase * tout + 0.3);
      ramp_t0 = $realtime; ramp_on = 1;
      ext_val = 1;
      t_e = $realtime;
      wait_done(400000);
      rd2(8'h14); ta = rd16_d[12:0];
      t_end = t_e + (240.0 - ramp_lo) / slope;
      wsum = 0; nest = 0;
      for (i = 0; i < 160; i = i + 1)
        if (wtime[i] > t_e + 2.0 * lag_pred + tout && wtime[i] < t_end - tout) begin
          est = wtime[i] - t_e - (mem_sample(i, 1'b0, 1'b1) / g16 - ramp_lo) / slope;
          wsum = wsum + est; nest = nest + 1;
        end
      lag_meas = (nest > 0) ? wsum / nest : 0.0;
      pos = ta + 0.0;
      pos = (t_e - wtime[ta] + lag_meas) / tout;
      $display("EXT lag %0s K %0d N %0d: lag %.1f ns from %0d samples (formula %.1f, +1 expected), EXT edge at trigger_address + %.2f output samples (edge %.2f after the last write)",
               fir ? "FIR" : "CIC", kk, n_out, lag_meas, nest, lag_pred, pos, (t_e - wtime[ta]) / tout);
      check(done && nest >= 10, "EXT lag capture completes with at least 10 samples on the ramp");
      check(lag_meas - lag_pred - 1.0 < 2.0 + 0.002 * tout && lag_pred + 1.0 - lag_meas < 2.0 + 0.002 * tout,
            "EXT lag: measured data lag matches the formula");
      ext_en = 0; ext_val = 0; ramp_on = 0;
    end
  endtask

  //---------------------------------------------------------------------------------
  // Main sequence

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    if (!$value$plusargs("logfile=%s", logname)) logname = "fir_edges.log";
    if (!$value$plusargs("p1=%d", p1n)) p1n = 40;           // phase 1 iterations (quick runs: +p1=3)
    logf = $fopen(logname, "w");
    $display("tb_fir: seed %0d, log %0s", seed, logname);
    #200;

    //-------------------------------------------------------------------------------
    // Phase 1: equivalence with 0x1534
    mm0 = eq_mismatch;
    random_phase(p1n);
    $display("phase 1: %0d equivalence samples, %0d mismatches (%0d captures the reference cut at its time-out), %0d captures with the FIR bit set at K >= 1",
             eq_samples, eq_mismatch, cut_caps, fir_set_cnt);
    check(eq_mismatch == mm0, "FIR off/not eligible: pins, decimator, bus reads, capture regs, memory identical to 0x1534");
    check(fir_set_cnt >= 5 || p1n < 40, "phase 1 covered FIR bit set at ineligible dividers");

    wr1(8'h50, 8'h00);
    ext_en = 0;

    //-------------------------------------------------------------------------------
    // Version word and the 0x29 FIR bits (byte 0 bit 6 = 0x56 bit 0, bit 5 = FIR active)
    rd2(8'h06);
    $display("version dut %h ref %h", rd16_d, rd16_r);
    check(rd16_d == 16'h153B, "cmd 0x06 reads 0x153B");
    check(rd16_r == 16'h1534, "reference cmd 0x06 reads 0x1534");
    ok29 = 1;
    wr1(8'h56, 8'h00); wr1(8'h53, 8'h07); wr1(8'h54, 8'h0C); wr4(8'h0D, 32'd14); #200;
    rd2(8'h29); if (rd16_d !== 16'h870C || rd16_r !== 16'h870C) begin ok29 = 0; $display("0x29 FIR off: %h", rd16_d); end
    wr1(8'h56, 8'hFF); #200;
    rd2(8'h29); if (rd16_d !== 16'hE70C) begin ok29 = 0; $display("0x29 FIR on, D 14: %h", rd16_d); end
    wr4(8'h0D, 32'd15); #200;
    rd2(8'h29); if (rd16_d !== 16'hE70C) begin ok29 = 0; $display("0x29 FIR on, D 15: %h", rd16_d); end
    wr4(8'h0D, 32'd13); #200;
    rd2(8'h29); if (rd16_d !== 16'hC70C) begin ok29 = 0; $display("0x29 FIR on, D 13: %h", rd16_d); end
    wr4(8'h0D, 32'd16); #200;
    rd2(8'h29); if (rd16_d !== 16'hC70C) begin ok29 = 0; $display("0x29 FIR on, D 16: %h", rd16_d); end
    wr4(8'h0D, 32'd10); #200;
    rd2(8'h29); if (rd16_d !== 16'hC70C) begin ok29 = 0; $display("0x29 FIR on, D 10: %h", rd16_d); end
    wr4(8'h0D, 32'h40000002); #200;
    rd2(8'h29); if (rd16_d !== 16'hE70C) begin ok29 = 0; $display("0x29 FIR on, D 0x40000002: %h", rd16_d); end
    wr4(8'h0D, 32'd14); wr1(8'h53, 8'h00); #200;
    rd2(8'h29); if (rd16_d !== 16'h400C) begin ok29 = 0; $display("0x29 FIR on, K 0: %h", rd16_d); end
    wr1(8'h56, 8'hFE); wr1(8'h53, 8'h01); #200;
    rd2(8'h29); if (rd16_d !== 16'h810C) begin ok29 = 0; $display("0x29 0x56 = 0xFE: %h", rd16_d); end
    check(ok29, "0x29 byte 0 bit 6 = 0x56 bit 0, bit 5 = FIR active (K >= 1, divider >= 14, bit 1 set)");

    //-------------------------------------------------------------------------------
    // Phase 2: FIR captures, logged for cic_model.py
    adc_mode = 1;
    fir_run(2'd1, 1'b0, rec_shift_fir(1, 14),  32'd14,  13'd120);   // N = 16
    fir_run(2'd1, 1'b1, rec_shift_fir(1, 15),  32'd15,  13'd100);   // N = 16, odd divider
    fir_run(2'd1, 1'b0, rec_shift_fir(1, 19),  32'd19,  13'd80);    // N = 20
    fir_run(2'd1, 1'b0, rec_shift_fir(1, 62),  32'd62,  13'd50);    // N = 64
    fir_run(2'd1, 1'b0, rec_shift_fir(1, 1999), 32'd1999, 13'd30);  // N = 2000
    fir_run(2'd2, 1'b0, rec_shift_fir(2, 14),  32'd14,  13'd120);
    fir_run(2'd2, 1'b1, rec_shift_fir(2, 18),  32'd18,  13'd80);    // N = 20
    fir_run(2'd2, 1'b0, rec_shift_fir(2, 63),  32'd63,  13'd50);    // N = 64
    fir_run(2'd2, 1'b0, rec_shift_fir(2, 198), 32'd198, 13'd30);    // N = 200
    fir_run(2'd3, 1'b0, rec_shift_fir(3, 14),  32'd14,  13'd120);
    fir_run(2'd3, 1'b0, rec_shift_fir(3, 22),  32'd22,  13'd80);    // N = 24
    fir_run(2'd3, 1'b1, rec_shift_fir(3, 30),  32'd30,  13'd60);    // N = 32
    fir_run(2'd3, 1'b0, rec_shift_fir(3, 99),  32'd99,  13'd40);    // N = 100
    fir_run(2'd3, 1'b0, rec_shift_fir(3, 398), 32'd398, 13'd30);    // N = 400
    negab = 2'b01; fir_run(2'd3, 1'b0, rec_shift_fir(3, 14), 32'd14, 13'd60);
    negab = 2'b10; fir_run(2'd2, 1'b1, rec_shift_fir(2, 18), 32'd18, 13'd60);
    negab = 2'b11; fir_run(2'd1, 1'b0, rec_shift_fir(1, 15), 32'd15, 13'd60);
    negab = 2'b00;
    // telegraph 0 / 255: the step overshoot with the recommended shift (no + 1) saturates the CIC
    // samples and the FIR output, and the undershoot clamps at 0
    adc_mode = 4;
    fir_run(2'd2, 1'b0, rec_shift_fir(2, 30) - 6'd1, 32'd30, 13'd150);
    fir_run(2'd1, 1'b0, rec_shift_fir(1, 14) - 6'd1, 32'd14, 13'd150);
    fir_run(2'd3, 1'b0, rec_shift_fir(3, 14),        32'd14, 13'd150);  // with + 1: no saturation
    adc_mode = 1;
    rnd = $random(seed);
    kq = (rnd[1:0] == 0) ? 2 : rnd[1:0];
    dv = 32'd14 + 4 * rnd[11:4] + rnd[2];
    fir_run(kq, rnd[3], rec_shift_fir(kq, dv), dv, 13'd40);
    $fdisplay(logf, "C 0 0 0 0 0 0 0");
    $fflush(logf);

    //-------------------------------------------------------------------------------
    // Trigger position. Reference: K = 0, step 50 -> 200 on CH1, level 128, normal mode.
    adc_mode = 2; step_val = 0;
    set_fir(2'd0, 1'b0, 6'd0, 32'd9, 1'b0);
    wr1(8'h17, 8'd128);
    for (e = 0; e < 2; e = e + 1) begin
      step_val = e;
      setup_capture(2'd0, e[0], 1'b1, 32'd9, 13'd100, 13'd300);
      #500;
      start_capture;
      wait (dut.sampling_half_way === 1'b1);
      #2000;
      step_val = ~e[0];
      wait_done(20000);
      rd2(8'h14); ta = rd16_d[12:0];
      find_cross(1'b0, 1'b0, e[0], 16'd128, 300);
      if (e == 0) delta_r = cross_i - ta; else delta_f = cross_i - ta;
      $display("K = 0 %0s: trigger_address %0d, first sample past level at %0d (delta %0d)",
               e ? "falling" : "rising", ta, cross_i, cross_i - ta);
    end
    check(delta_r == delta_f, "K = 0: rising and falling give the same delta");
    delta0 = delta_r;

    // FIR stream, K = 1..3, N = 16, 20, 64, 100, 200, both edges, CH1 and CH2
    divs[0] = 14; divs[1] = 19; divs[2] = 62; divs[3] = 99; divs[4] = 198;
    for (kq = 1; kq <= 3; kq = kq + 1)
      for (nq = 0; nq < 5; nq = nq + 1)
        for (e = 0; e < 4; e = e + 1) begin
          set_fir(kq, 1'b0, rec_shift_fir(kq, divs[nq]), divs[nq], 1'b1);
          lvl16 = lvl_for(kq, (divs[nq] / 2) + 1, rec_shift_fir(kq, divs[nq]));
          wr2(8'h55, lvl16);
          step_val = (e == 1 || e == 2);
          setup_capture(e[1] ? 2'd1 : 2'd0, e[0], 1'b1, divs[nq], 13'd60, 13'd200);
          #((kq + 12) * ((divs[nq] / 2) + 1) * 10 + 500);
          start_capture;
          wait (dut.sampling_half_way === 1'b1);
          #((kq + 12) * ((divs[nq] / 2) + 1) * 10 + 500);
          step_val = ~step_val;
          wait_done(200000);
          rd2(8'h14); ta = rd16_d[12:0];
          find_cross(e[1], 1'b1, e[0], lvl16, 200);
          $sformat(msg, "FIR K %0d N %0d %0s %0s: delta %0d (K = 0: %0d)", kq, 2 * ((divs[nq] / 2) + 1),
                   e[1] ? "CH2" : "CH1", e[0] ? "falling" : "rising", cross_i - ta, delta0);
          if (cross_i - ta != delta0 || !done) $display("%0s", msg);
          check(done && cross_i >= 0 && (cross_i - ta) == delta0, "FIR trigger position matches the 8-bit convention");
        end

    //-------------------------------------------------------------------------------
    // EXT with the FIR: trigger_address = last sample written before the edge
    ext_en = 1; ext_val = 0;
    for (kq = 1; kq <= 3; kq = kq + 1)
      for (nq = 0; nq < 2; nq = nq + 1) begin
        set_fir(kq, 1'b0, rec_shift_fir(kq, nq ? 199 : 19), nq ? 32'd199 : 32'd19, 1'b1);
        setup_capture(2'd2, 1'b0, 1'b1, nq ? 32'd199 : 32'd19, 13'd50, 13'd100);
        ext_val = 0;
        start_capture;
        wait (dut.sampling_half_way === 1'b1);
        #3000;
        @(posedge dut.sample_write_clock);
        while (!(dut.dec_wr === 1'b1)) @(posedge dut.sample_write_clock);
        #((nq ? 1000 : 100) / 4 + 0.3);
        ext_val = 1;
        wa_at_edge = dut.sample_write_address;
        wait_done(200000);
        rd2(8'h14);
        $display("EXT FIR K %0d N %0d: edge at write address %0d, trigger_address %0d", kq, nq ? 200 : 20, wa_at_edge, rd16_d[12:0]);
        check(done && rd16_d[12:0] == wa_at_edge - 13'd1, "EXT with the FIR: trigger_address = last sample written before the edge");
      end
    ext_en = 0; ext_val = 0;

    //-------------------------------------------------------------------------------
    // EXT data lag, FIR and CIC path
    for (kq = 1; kq <= 3; kq = kq + 1) begin
      ext_lag(kq, 32'd19,  1'b1, 0.25);
      ext_lag(kq, 32'd99,  1'b1, 0.6);
      ext_lag(kq, 32'd399, 1'b1, 0.1);
      ext_lag(kq, 32'd19,  1'b0, 0.25);
      ext_lag(kq, 32'd99,  1'b0, 0.6);
      ext_lag(kq, 32'd399, 1'b0, 0.1);
    end

    //-------------------------------------------------------------------------------
    // Auto time-out with the FIR counts output samples
    set_fir(2'd2, 1'b0, rec_shift_fir(2, 99), 32'd99, 1'b1);
    adc_mode = 3; flat_val = 8'd20;
    wr2(8'h55, 16'hFFFF);
    setup_capture(2'd0, 1'b0, 1'b0, 32'd99, 13'd40, 13'd80);
    wr4(8'h0E, 32'd100);
    start_capture;
    wait_done(200000);
    rd2(8'h14);
    total_w = dut.sample_write_address;
    $display("auto time-out FIR K 2 N 100: trigger_address %0d, write address at stop %0d", rd16_d[12:0], total_w);
    check(done && rd16_d[12:0] == dut.w_last - 13'd80 + 13'd40, "FIR auto time-out: trigger_address = W - total + pretrigger (0x153B)");
    check(total_w >= 100 && total_w <= 104, "FIR auto time-out after time_base_set output samples");

    //-------------------------------------------------------------------------------
    // Phase 4: FIR off. One full-memory flat capture at K = 1 rewrites all 8192 words in both
    // instances (the FIR captures left different data), then random equivalence again.
    set_fir(2'd1, 1'b0, 6'd0, 32'd1, 1'b0);
    adc_mode = 3; flat_val = 8'd20;
    wr1(8'h15, 8'h00);
    wr2(8'h55, 16'hFFFF);
    setup_capture(2'd0, 1'b0, 1'b0, 32'd1, 13'd100, 13'd200);
    wr4(8'h0E, 32'd9000);
    start_capture;
    wait_done(40000);
    check(done && dut.sampling_triggered == 1'b0, "resync capture ran to the time-out");
    adc_mode = 0;
    rd2(8'h41);                                          // fpga-gen12: the read register's stale bits equal again
                                                         // (the last full-byte read was a 0x14 that differs by design)
    mm0 = eq_mismatch;
    compare_memory;
    random_phase(15);
    $display("phase 4: total %0d equivalence samples, %0d mismatches, %0d captures the reference cut at its time-out", eq_samples, eq_mismatch, cut_caps);
    check(eq_mismatch == mm0, "after FIR use, FIR off again identical to 0x1534");

    $fclose(logf);
    $display("RESULT: %0d checks, %0d failed -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #900_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
