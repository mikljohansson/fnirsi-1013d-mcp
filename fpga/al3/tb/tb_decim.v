// Testbench for the decimation filter (fpga/al3/src/zaklad.v, version 0x1534, 0x1535 with the FIR off).
// Run with fpga/al3/tb/run.sh (iverilog); the decimated outputs are checked by cic_model.py.
// Two instances share every input:
//   dut  = src/zaklad.v (module fnirsi_1013D)
//   refd = the 0x1533 zaklad.v from git (REF1533_REV), modules renamed with a _ref suffix
// Phase 1 (equivalence, K = 0): random captures with trigger source CH1/CH2/EXT, generator and
//   EXT activity, random values written to the new commands 0x53 (K = 0), 0x54 and 0x55.
//   Output pins, MCU bus reads, capture registers and the whole sample memory after every capture
//   must be identical to 0x1533.
// Phase 2 (decimated, K = 1..3): random ADC data; every write clock edge (pins in, memory write
//   out) is logged to +logfile for cic_model.py, which recomputes each stored sample with a
//   direct-form reference (boxcar sum for K = 1, N-tap boxcar convolved K times for CIC).
// Phase 3 (directed): version word, 0x29 geometry, MCU read-back of 16-bit samples, trigger
//   position on the 16-bit stream (rising/falling, CH1/CH2, several N) against the 8-bit
//   convention measured on K = 0, EXT trigger with K >= 1, auto time-out, return to K = 0.
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

  // 0x153A: the dut registers each core's pins on clk_200MHz at its own phase and hands them to the capture logic one
  // write clock later (adc*_w). The reference (which takes the pins on the write clock edge) gets exactly those values,
  // 1 ns after each write clock edge, so both capture the same samples and the comparison below stays exact.
  reg [7:0] r_adc1A = 0, r_adc1B = 0, r_adc2A = 0, r_adc2B = 0;
  always @(dut.adc1A_w) r_adc1A <= #1 dut.adc1A_w;
  always @(dut.adc1B_w) r_adc1B <= #1 dut.adc1B_w;
  always @(dut.adc2A_w) r_adc2A <= #1 dut.adc2A_w;
  always @(dut.adc2B_w) r_adc2B <= #1 dut.adc2B_w;

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
        if ((out_dut & out_cmp_mask) !== (out_ref & out_cmp_mask) || st_dut !== st_ref || pad_dut !== pad_ref) begin
          eq_mismatch = eq_mismatch + 1;
          if (eq_mismatch <= 30)
            $display("MISMATCH t=%0t out %h/%h st %h/%h pad %b/%b", $time, out_dut, out_ref, st_dut, st_ref, pad_dut, pad_ref);
        end
      end
    end
  end

  // 0x153B (ported from tb_fir.v for 0x153D): words the dut wrote while the reference had already stopped (a triggered
  // auto-mode capture the reference cut at its time-out, or a time-out capture whose stop the two designs place
  // differently) differ until both write them again; they are skipped by the memory compare and counted. With the DDS
  // gone an EXT capture with the pin driven never triggers and always runs to its time-out, which made these words show.
  reg     mem_dirty [0:8191];
  integer mdi, mem_dirty_n;
  initial for (mdi = 0; mdi < 8192; mdi = mdi + 1) mem_dirty[mdi] = 0;
  always @(posedge dut.sample_write_clock)
    if (dut.sample_write_enable && dut.swc_adv)
      mem_dirty[dut.sample_write_address_phys] <= !refd.sample_write_enable;

  // Whole-memory compare (K = 0 phase)
  integer mi, mem_diff;
  task compare_memory;
    begin
      mem_diff = 0; mem_dirty_n = 0;
      for (mi = 0; mi < 8192; mi = mi + 1)
        if (mem_dirty[mi]) mem_dirty_n = mem_dirty_n + 1;
        else if (dut.samples.mem[mi] !== refd.samples.mem[mi]) begin
          mem_diff = mem_diff + 1;
          // fpga-d2: the first differing words with both values and the capture's geometry, for the 0x153D decim question
          if (eq_mismatch < 30 && mem_diff <= 4)
            $display("  word %0d: dut %h ref %h (wa %0d/%0d, w_last %0d, ta %0d/%0d, trig %b/%b, swe %b/%b, pre %0d tot %0d)", mi,
                     dut.samples.mem[mi], refd.samples.mem[mi], dut.sample_write_address, refd.sample_write_address, dut.w_last,
                     dut.trigger_address, refd.trigger_address, dut.sampling_triggered, refd.sampling_triggered,
                     dut.sample_write_enable, refd.sample_write_enable, pre_r, tot_r);
        end
      eq_samples = eq_samples + 1;
      if (mem_diff != 0) begin
        eq_mismatch = eq_mismatch + 1;
        if (eq_mismatch <= 30) $display("MISMATCH t=%0t sample memory: %0d words differ", $time, mem_diff);
      end
    end
  endtask

  //---------------------------------------------------------------------------------
  // ADC stimulus, updated every 7 ns at times that never coincide with a clock edge
  // adc_mode 0 = triangles, 1 = random bytes, 2 = step (step_val selects step_lo / step_hi,
  // CH2 gets the complement step), 3 = flat

  integer    adc_mode = 0;
  reg        step_val = 0;
  reg  [7:0] step_lo = 50, step_hi = 200;
  reg  [7:0] flat_val = 0;
  integer    triA = 0, triB = 0, d1 = 3, d2 = -5;
  integer    seed;

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
        default: begin adc1A = flat_val; adc1B = flat_val; adc2A = flat_val; adc2B = flat_val; end
      endcase
    end
  end

  //---------------------------------------------------------------------------------
  // Write clock edge log for cic_model.py
  // E <edge> <1A> <1B> <2A> <2B> <wr> <addr> <dia> <n1A> <n1B> <n2A> <n2B>
  //   pins and memory write as seen at the rising edge, then the pins at the falling edge before it
  // C <K> <divider> <swap> <shift> <neg_a> <neg_b>    (configuration marker)

  integer logf = 0;
  reg     log_on = 0;
  integer edge_n = 0;
  reg [7:0] n1A = 0, n1B = 0, n2A = 0, n2B = 0;
  always @(negedge dut.sample_write_clock) begin
    n1A = dut.adc1A_w; n1B = dut.adc1B_w; n2A = dut.adc2A_w; n2B = dut.adc2B_w;   // 0x153A: the decimator's inputs
  end
  always @(posedge dut.sample_write_clock) begin
    edge_n = edge_n + 1;
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
  // 0x153C: the reads 0x05 / 0x0A / 0x18 load bit 0 only; bits 7..1 are what the last full-byte read left, which differs
  // by design after a time-out stop (0x14, 0x153B) and has no identical refill read any more (0x41 is gone), so those
  // three are compared on bit 0; every other read is compared whole
  wire [7:0] rd_mask = ((dut.mcu_command == 8'h05) || (dut.mcu_command == 8'h0A) || (dut.mcu_command == 8'h18)) ? 8'h01 : 8'hFF;
  task rd_byte;
    begin
      mcu_rws = 0; mcu_dcs = 0;
      mcu_strobe;
      rd_d = bus_dut; rd_r = bus_ref;
      if (eq_on && bus_cmp_on && !cut_ref) begin
        eq_samples = eq_samples + 1;
        if (((rd_d ^ rd_r) & rd_mask) !== 8'h00) begin
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

  // Decimation configuration and settling: (K + 3) output periods plus the pipeline
  reg [1:0] negab = 0;                                  // 0x53 bits 4..3 used by set_decim
  task set_decim(input [1:0] k, input swap, input [5:0] sh, input [31:0] divider);
    begin
      wr1(8'h53, {3'd0, negab, 1'b0, k});               // 0x153C: bit 2 (swap) is gone, written 0 (negab: bits 4..3, inert since 0x153A)
      wr1(8'h54, {2'd0, sh});
      wr4(8'h0D, divider);
      #((k + 3) * ((divider / 2) + 1) * 10 + 300);
    end
  endtask

  // Recommended shift: max(0, bitlen(255 * N^K) - 16)
  function [5:0] rec_shift(input [1:0] k, input [31:0] divider);
    reg [127:0] g;
    integer b, i;
    begin
      g = 255;
      for (i = 0; i < k; i = i + 1) g = g * (2 * ((divider / 2) + 1));
      b = 0;
      for (i = 0; i < 128; i = i + 1) if (g[i]) b = i + 1;
      rec_shift = (b > 16) ? b - 16 : 0;
    end
  endfunction

  function [15:0] mem_sample(input [12:0] addr, input ch, input dec);
    reg [31:0] w;
    begin
      w = dut.samples.mem[addr];
      if (dec) mem_sample = ch ? w[31:16] : w[15:0];
      else     mem_sample = ch ? {8'd0, w[23:16]} : {8'd0, w[7:0]};
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
  // Phase 1: random equivalence with 0x1533 at K = 0

  integer it, k, nread;
  reg [31:0] rnd, rnd_ptr;
  reg [12:0] pre_r, tot_r;
  reg gen_on;

  // fpga-d3c (0x153D): the K = 0 random phase at D > 0 as a model check (skip_mode = 1, after the last lockstep phase).
  // The reference gates its encodes at D > 0 and cannot follow the dut, so nothing is compared with it there; instead
  // every stored word must be the captured pair of its own write clock edge (adc*_w at that edge), consecutive words of
  // a record exactly D + 1 edges apart and the first word within D + 1 edges of the last reset edge, under the phase's
  // random trigger / EXT / generator settings (tb_skip.v has the directed D > 0 cases).
  reg        skip_mode = 0;
  reg        sm_on = 0;
  integer    sm_d = 0, sm_edge = 0, sm_last = -1, sm_rel = 0, sm_nwr = 0, sm_valbad = 0, sm_gapbad = 0, sm_firstbad = 0;
  integer    sm_caps = 0, sm_done = 0;
  always @(posedge dut.sample_write_clock) begin
    if (sm_on) begin
      if (dut.cap_rst) begin
        sm_last = -1; sm_rel = sm_edge;
      end else if (dut.sample_write_enable && dut.swc_adv) begin
        if (dut.sample_dia !== { dut.adc2B_w, dut.adc2A_w, dut.adc1B_w, dut.adc1A_w }) sm_valbad = sm_valbad + 1;
        if (sm_last >= 0) begin
          if (sm_edge - sm_last != sm_d + 1) sm_gapbad = sm_gapbad + 1;
        end else if (sm_edge - sm_rel > sm_d + 1)
          sm_firstbad = sm_firstbad + 1;
        sm_last = sm_edge; sm_nwr = sm_nwr + 1;
      end
      sm_edge = sm_edge + 1;
    end
  end

  task random_phase(input integer iterations);
    begin
      eq_on = !skip_mode; bus_cmp_on = !skip_mode;
      for (it = 0; it < iterations; it = it + 1) begin
        rnd = $random(seed);
        // new 0x1534/0x1535 commands with K = 0: must not change anything
        wr1(8'h53, 8'h00);                                // 0x153C: bits 7..2 of 0x53 are gone (bit 2, the swap, was random here)
        wr1(8'h54, $random(seed));
        wr1(8'h56, $random(seed) & 8'hFD);                // bit 1 is peak detect since 0x153C (not reserved)
        wr2(8'h55, $random(seed));
        wr1(8'h15, {6'd0, rnd[9:8]});                     // CH1, CH2, EXT, EXT (3 acts as EXT)
        wr1(8'h16, {7'd0, rnd[1]});
        wr1(8'h17, 8'd60 + (rnd[15:8] % 8'd130));
        wr1(8'h1A, {7'd0, rnd[2]});
        sm_d = skip_mode ? 1 + rnd[5:4] + (rnd[3] ? 5 : 0) : 0;   // fpga-d3c: D 1 .. 9 in skip_mode
        wr4(8'h0D, sm_d);                                 // 0x153D: K = 0 is compared with the reference at 200 MSa/s only (the
                                                          // dut stores every (D + 1)-th full-rate pair at D > 0, the reference
                                                          // gates its encodes: tb_skip.v checks D > 0 against a model); up to
                                                          // 0x153C rnd[5:4] + (rnd[10] ? 0 : 6) = write clock 100 .. 12.5 MHz
        pre_r = 13'd20 + (rnd[24:16] % 13'd280);
        tot_r = pre_r + 13'd20 + ($random(seed) & 13'h00FF);
        wr2(8'h0B, {3'd0, pre_r});
        wr2(8'h0C, {3'd0, tot_r});
        wr4(8'h0E, 32'd2000 + ($random(seed) & 32'h3FFF));
        wr2(8'h32, $random(seed)); wr2(8'h35, $random(seed));
        wr1(8'h33, $random(seed) % 6); wr1(8'h36, $random(seed) % 6);
        wr1(8'h34, $random(seed)); wr1(8'h37, $random(seed));
        wr2(8'h38, $random(seed));
        rnd_ptr = $random(seed);                          // 0x153C: was written to 0x3C and read back through 0x41 as the read pointer below
        gen_on = rnd[6];
        wr1(8'h50, {7'd0, gen_on});
        wr4(8'h51, 32'd85899346 + ($random(seed) & 32'h00FFFFFF));
        wr4(8'h52, $random(seed) & 32'd0);               // 0x153D: 0x52 retired; the reference's DDS stays low (= the pattern's idle level)
        d1 = 1 + (rnd[27:25]); d2 = -(1 + rnd[30:28]);
        adc_mode = rnd[11] ? 1 : 0;
        if (!gen_on) begin
          ext_en = 1;
          fork
            begin : ext_noise
              repeat (200) begin
                #(3 + ($random(seed) & 255));
                ext_val = ~ext_val;
              end
            end
          join_none
        end else
          ext_en = 0;
        sm_on = skip_mode;
        start_capture;
        wait_done(100000);                                // 0x153D: an EXT capture with the pin driven (pattern off) never
                                                          // triggers and runs to its time-out (up to 1.8 ms at divider 9)
        // 0x153B: a capture that ended on the auto time-out reports W - total + pretrigger (reference: pretrigger) and
        // never a trigger after the stop: these two reads are checked against the formula instead of the reference
        sm_on = 0;
        if (skip_mode) begin sm_caps = sm_caps + 1; sm_done = sm_done + done; end
        bus_cmp_on = rnd[2] & !skip_mode;
        rd2(8'h14);
        rd1(8'h18);
        if (!rnd[2] && !rd_d[0])
          check(rd16_d[12:0] == ((dut.w_last - tot_r + pre_r) & 13'h1FFF), "auto time-out: 0x14 = W - total + pretrigger");
        bus_cmp_on = !skip_mode;
        rd1(8'h28);
        wr2(8'h1F, {3'd0, rnd_ptr[12:0]});
        nread = 4 + (rnd[31:30] * 4);
        for (k = 0; k < nread; k = k + 1) rd1(8'h20 + (k % 4));
        rd1(8'h05);
        disable ext_noise;
        ext_en = 0;
        wr1(8'h01, 8'h00);
        #100;
        if (!skip_mode) compare_memory;
      end
      eq_on = 0; bus_cmp_on = 0;
      adc_mode = 0;
    end
  endtask

  //---------------------------------------------------------------------------------
  // Phase 2: logged decimated captures for the reference model

  task decim_run(input [1:0] kk, input swap, input [5:0] sh, input [31:0] divider, input [12:0] total);
    begin
      adc_mode = 1;
      set_decim(kk, swap, sh, divider);
      setup_capture(2'd0, 1'b0, 1'b0, divider, 13'd10, total);
      wr4(8'h0E, {19'd0, total});                        // auto mode: stops after about total samples
      // 0x153A: 0x53 bits 3 / 4 select nothing any more (the decimator gets registered samples), so the model
      // sees neg = 0 whatever was written; the negab runs below only prove the bits are inert
      $fdisplay(logf, "C %0d %0d %0d %0d 0 0", kk, divider, 1'b0, sh);   // 0x153C: no swap
      log_on = 1;
      start_capture;
      wait_done(400000);
      log_on = 0;
      check(done, "decimated logged capture completes");
    end
  endtask

  //---------------------------------------------------------------------------------
  // Main sequence

  integer delta0, delta_r, delta_f, kq, nq, e;
  integer divs [0:4];
  reg [12:0] ta, wa_at_edge;
  reg [15:0] lvl16, g16;
  reg [8*64-1:0] logname;
  reg [255:0] msg;
  integer ok_rb, total_w;

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    if (!$value$plusargs("logfile=%s", logname)) logname = "decim_edges.log";
    logf = $fopen(logname, "w");
    $display("tb_decim: seed %0d, log %0s", seed, logname);
    #200;
    //0x153D: the DUT has no DDS (its pin carries the pattern's idle level, 0, while driven); the reference's DDS output is
    //set low for good (0x52 duty 0, retired on the DUT) and a reset pulse clears ext_activity in both after its pin fell
    wr4(8'h52, 32'd0);
    wr1(8'h01, 8'h01); #200; wr1(8'h01, 8'h00); #200;
    // 0x153D: the dut's write clock toggles on every clk_200MHz edge from power-on, the reference's from its divider
    // counter's first count one cycle later, so their phases differ by an edge (the reference's A / B samples swapped
    // against the dut's): copy the dut's write clock and encode flops into the reference at divider 0, half a clock from
    // the toggling (negative) edges, as tb_decim.v does before its phase 4. Repeated wherever a K = 0 capture at D > 0
    // (the reference's gated encodes) precedes a lockstep phase.
    wr4(8'h0D, 32'd0); #200;
    @(posedge dut.clk_200MHz); #0.6;
    refd.sample_write_clock = dut.sample_write_clock;
    refd.adc1_encA = dut.adc1_encA; refd.adc1_encB = dut.adc1_encB;
    refd.adc2_encA = dut.adc2_encA; refd.adc2_encB = dut.adc2_encB;
    refd.sample_rate_counter = 32'd1;
    refd.swc_prev = dut.sample_write_clock;
    #500;

    //-------------------------------------------------------------------------------
    // Phase 1: K = 0 equivalence with 0x1533
    mm0 = eq_mismatch;
    random_phase(50);
    $display("phase 1: %0d equivalence samples, %0d mismatches, %0d captures the reference cut at its time-out", eq_samples, eq_mismatch, cut_caps);
    check(eq_mismatch == mm0, "K = 0: pins, bus reads, capture registers and memory identical to 0x1533");

    // Generator off and pin released for the rest of the run (EXT tests drive the pin)
    wr1(8'h50, 8'h00);
    wr1(8'h56, 8'h00);                                    // FIR off for the 0x1534 checks below
    ext_en = 0;

    //-------------------------------------------------------------------------------
    // Version word and geometry register
    rd2(8'h06);
    $display("version dut %h ref %h", rd16_d, rd16_r);
    check(rd16_d == 16'h153D, "cmd 0x06 reads 0x153D");
    check(rd16_r == 16'h1533, "reference cmd 0x06 reads 0x1533");
    wr1(8'h53, 8'h00); wr1(8'h54, 8'h00);
    rd2(8'h29);
    check(rd16_d == 16'h0000, "0x29 reads 0x00 0x00 at K = 0");
    wr1(8'h53, 8'h07); wr1(8'h54, 8'hED);
    rd2(8'h29);
    $display("0x29 after 0x53 = 0x07, 0x54 = 0xED: %h", rd16_d);
    check(rd16_d == 16'h832D, "0x29 = {bit7 16-bit, 000, K}, {shift[5:0]} (0x153C: bits 4..2 read 0)");
    wr1(8'h53, 8'h1A);
    rd2(8'h29);
    check(rd16_d[15:8] == 8'h82, "0x29 byte 0 bits 4..2 read 0 with 0x53 bits 4..2 written (0x153C: no swap / falling-edge bits)");
    wr1(8'h53, 8'h00);
    rd2(8'h29);
    check(rd16_d[15] == 1'b0, "0x29 bit7 clears at K = 0");

    //-------------------------------------------------------------------------------
    // Phase 2: decimated captures, logged for cic_model.py
    // dividers: N = 2, 4, 10, 20, 64 (even divider 63 -> N = 64), 200, plus a random odd one
    decim_run(2'd1, 1'b0, rec_shift(1, 1),  32'd1,   13'd200);
    decim_run(2'd1, 1'b0, rec_shift(1, 9),  32'd9,   13'd150);
    decim_run(2'd1, 1'b0, rec_shift(1, 399), 32'd399, 13'd40);
    decim_run(2'd1, 1'b0, 6'd2,            32'd1999, 13'd24);   // N = 2000, shift 2 (3 recommended): partly saturated
    decim_run(2'd2, 1'b0, rec_shift(2, 3),  32'd3,   13'd200);
    decim_run(2'd2, 1'b0, rec_shift(2, 19), 32'd19,  13'd120);
    decim_run(2'd2, 1'b0, rec_shift(2, 62), 32'd62,  13'd60);   // even divider: N = 64
    decim_run(2'd2, 1'b0, rec_shift(2, 199), 32'd199, 13'd30);
    decim_run(2'd3, 1'b0, rec_shift(3, 1),  32'd1,   13'd200);
    decim_run(2'd3, 1'b0, rec_shift(3, 9),  32'd9,   13'd150);
    decim_run(2'd3, 1'b0, rec_shift(3, 39), 32'd39,  13'd80);
    decim_run(2'd3, 1'b0, rec_shift(3, 199), 32'd199, 13'd30);
    decim_run(2'd3, 1'b0, rec_shift(3, 0),  32'd0,   13'd100);  // divider 0: N = 2
    negab = 2'b01; decim_run(2'd3, 1'b0, rec_shift(3, 9), 32'd9, 13'd100);   // A cores on the falling edge
    negab = 2'b10; decim_run(2'd2, 1'b0, rec_shift(2, 3), 32'd3, 13'd100);   // B cores on the falling edge
    negab = 2'b11; decim_run(2'd1, 1'b0, rec_shift(1, 1), 32'd1, 13'd100);   // both
    negab = 2'b00;
    decim_run(2'd3, 1'b0, 6'd11,           32'd99,  13'd40);    // shift 11 (12 recommended): partly saturated
    rnd = $random(seed);
    kq = (rnd[1:0] == 0) ? 2 : rnd[1:0];
    nq = rec_shift(kq, 32'd5 + 2 * (rnd[12:8]));
    if (rnd[3] && nq > 0) nq = nq - 1;                          // sometimes one below: partly saturated
    decim_run(kq, 1'b0, nq, 32'd5 + 2 * (rnd[12:8]), 13'd60);
    negab = 2'b00;
    $fdisplay(logf, "C 0 0 0 0 0 0");
    $fflush(logf);

    //-------------------------------------------------------------------------------
    // MCU read-back of 16-bit samples: 0x20/0x21 low/high CH1, 0x22/0x23 low/high CH2
    ok_rb = 1;
    wr2(8'h1F, 13'd5);
    for (k = 0; k < 8; k = k + 1) begin rd1(8'h20); g16[7:0] = rd8_d; if (g16[7:0] !== dut.samples.mem[5 + k][7:0]) ok_rb = 0; end
    wr2(8'h1F, 13'd5);
    for (k = 0; k < 8; k = k + 1) begin rd1(8'h21); if (rd8_d !== dut.samples.mem[5 + k][15:8]) ok_rb = 0; end
    wr2(8'h1F, 13'd5);
    for (k = 0; k < 8; k = k + 1) begin rd1(8'h22); if (rd8_d !== dut.samples.mem[5 + k][23:16]) ok_rb = 0; end
    wr2(8'h1F, 13'd5);
    for (k = 0; k < 8; k = k + 1) begin rd1(8'h23); if (rd8_d !== dut.samples.mem[5 + k][31:24]) ok_rb = 0; end
    check(ok_rb, "read-back: 0x20..0x23 return CH1 low/high, CH2 low/high of consecutive words");

    //-------------------------------------------------------------------------------
    // Trigger position. Reference: K = 0, step 50 -> 200 on CH1, level 128, normal mode.
    // delta = (index of the first stored sample past the level) - trigger_address
    adc_mode = 2; step_val = 0;
    set_decim(2'd0, 1'b0, 6'd0, 32'd9);
    wr1(8'h17, 8'd128);
    for (e = 0; e < 2; e = e + 1) begin
      step_val = e;                                            // rising: start low; falling: start high
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

    // Same step through the filter, K = 1..3, several N, both edges, CH1 and CH2.
    // CH2 sees the complement step (200 -> 50), so its rising test uses the falling edge of CH1.
    divs[0] = 1; divs[1] = 9; divs[2] = 19; divs[3] = 99; divs[4] = 62;
    for (kq = 1; kq <= 3; kq = kq + 1)
      for (nq = 0; nq < 5; nq = nq + 1)
        for (e = 0; e < 4; e = e + 1) begin
          set_decim(kq, 1'b0, rec_shift(kq, divs[nq]), divs[nq]);
          // level halfway between the filtered plateaus 50 * G and 200 * G (G = N^K >> shift)
          lvl16 = ((125 * ((2 * ((divs[nq] / 2) + 1)) ** kq)) >> rec_shift(kq, divs[nq]));
          wr2(8'h55, lvl16);
          step_val = (e == 1 || e == 2);                       // CH1 starts high for e = 1, CH2 starts high when CH1 low
          setup_capture(e[1] ? 2'd1 : 2'd0, e[0], 1'b1, divs[nq], 13'd60, 13'd200);
          #((kq + 3) * ((divs[nq] / 2) + 1) * 10 + 500);
          start_capture;
          wait (dut.sampling_half_way === 1'b1);
          #((kq + 3) * ((divs[nq] / 2) + 1) * 10 + 500);
          step_val = ~step_val;
          wait_done(200000);
          rd2(8'h14); ta = rd16_d[12:0];
          find_cross(e[1], 1'b1, e[0], lvl16, 200);
          $sformat(msg, "K %0d N %0d %0s %0s: delta %0d (K = 0: %0d)", kq, 2 * ((divs[nq] / 2) + 1),
                   e[1] ? "CH2" : "CH1", e[0] ? "falling" : "rising", cross_i - ta, delta0);
          if (cross_i - ta != delta0 || !done) $display("%0s", msg);
          check(done && cross_i >= 0 && (cross_i - ta) == delta0, "decimated trigger position matches the 8-bit convention");
        end

    //-------------------------------------------------------------------------------
    // 0x17 is ignored with K >= 1: level 0x17 = 0 would trigger at once on K = 0 data,
    // trigger_level16 above the plateau must keep it waiting
    set_decim(2'd1, 1'b0, rec_shift(1, 9), 32'd9);
    wr1(8'h17, 8'd0);
    wr2(8'h55, 16'hFFFF);
    adc_mode = 3; flat_val = 8'd100;
    setup_capture(2'd0, 1'b0, 1'b1, 32'd9, 13'd50, 13'd100);
    start_capture;
    #20000;
    rd1(8'h18);
    check(rd8_d[0] == 1'b0, "K >= 1: 0x17 ignored, 16-bit level above the data does not trigger");

    //-------------------------------------------------------------------------------
    // EXT with K >= 1: trigger_address = last sample written before the (synchronised) edge,
    // as for K = 0, at N = 20 (200 ns per output sample) and N = 200
    ext_en = 1; ext_val = 0;
    for (kq = 1; kq <= 3; kq = kq + 1)
      for (nq = 0; nq < 2; nq = nq + 1) begin
        set_decim(kq, 1'b0, rec_shift(kq, nq ? 199 : 19), nq ? 32'd199 : 32'd19);
        setup_capture(2'd2, 1'b0, 1'b1, nq ? 32'd199 : 32'd19, 13'd50, 13'd100);
        ext_val = 0;
        start_capture;
        wait (dut.sampling_half_way === 1'b1);
        #3000;
        // edge a quarter of an output period after a written sample
        @(posedge dut.sample_write_clock);
        while (!(dut.dec_wr === 1'b1)) @(posedge dut.sample_write_clock);
        #((nq ? 1000 : 100) / 4 + 0.3);                       // output period 100 ns or 1 us
        ext_val = 1;
        wa_at_edge = dut.sample_write_address;
        wait_done(200000);
        rd2(8'h14);
        $display("EXT K %0d N %0d: edge at write address %0d, trigger_address %0d", kq, nq ? 200 : 20, wa_at_edge, rd16_d[12:0]);
        check(done && rd16_d[12:0] == wa_at_edge - 13'd1, "EXT with K >= 1: trigger_address = last sample written before the edge");
      end
    ext_en = 0; ext_val = 0;

    //-------------------------------------------------------------------------------
    // Auto mode time-out with K >= 1 counts output samples: total and trigger_address = W - total + pretrigger
    set_decim(2'd2, 1'b0, rec_shift(2, 99), 32'd99);
    adc_mode = 3; flat_val = 8'd20;
    wr2(8'h55, 16'hFFFF);
    setup_capture(2'd0, 1'b0, 1'b0, 32'd99, 13'd40, 13'd80);
    wr4(8'h0E, 32'd100);
    start_capture;
    wait_done(200000);
    rd2(8'h14);
    total_w = dut.sample_write_address;
    $display("auto time-out K 2 N 100: trigger_address %0d, write address at stop %0d", rd16_d[12:0], total_w);
    check(done && rd16_d[12:0] == dut.w_last - 13'd80 + 13'd40, "K >= 1 auto time-out: trigger_address = W - total + pretrigger (0x153B)");
    check(total_w >= 100 && total_w <= 104, "K >= 1 auto time-out after time_base_set output samples");

    //-------------------------------------------------------------------------------
    // Back to K = 0: realign with the reference (one full-memory flat capture) and rerun equivalence.
    // With K >= 1 the dut's encoders and write clock ran at full rate while the reference ran at
    // the programmed divider, so their toggle phases (which flip-flop state the free-running
    // divide-by-2 is in) differ. That phase is arbitrary history in 0x1533 as well (it depends on
    // every divider ever used), so it is copied from the dut to the reference here, at divider 0
    // where both enables are constant, half a clock away from the toggling (negative) edges.
    set_decim(2'd0, 1'b0, 6'd0, 32'd0);
    #200;
    @(posedge dut.clk_200MHz); #0.6;
    refd.sample_write_clock = dut.sample_write_clock;
    refd.adc1_encA = dut.adc1_encA; refd.adc1_encB = dut.adc1_encB;
    refd.adc2_encA = dut.adc2_encA; refd.adc2_encB = dut.adc2_encB;
    refd.sample_rate_counter = 20'd1;                     // 0x153D: the dut has no divider counter (the write clock toggles on
                                                          // every edge); at divider 0 the reference's enable is 1 from counter 1 on
    refd.swc_prev = dut.sample_write_clock;               // 0x1539 has no swc_prev: it held the write clock of this edge
    adc_mode = 3; flat_val = 8'd20;
    wr1(8'h15, 8'h00);
    wr1(8'h17, 8'd128);
    setup_capture(2'd0, 1'b0, 1'b0, 32'd0, 13'd100, 13'd200);
    wr4(8'h0E, 32'd9000);
    start_capture;
    wait_done(20000);
    check(done && dut.sampling_triggered == 1'b0, "resync capture ran to the time-out");
    adc_mode = 0;
    mm0 = eq_mismatch;
    random_phase(10);
    $display("phase 4: total %0d equivalence samples, %0d mismatches, %0d captures the reference cut at its time-out", eq_samples, eq_mismatch, cut_caps);
    check(eq_mismatch == mm0, "after K >= 1 use, K = 0 again identical to 0x1533");

    $fclose(logf);
    // fpga-d3c: K = 0 at D = 1 .. 9 under the random settings, against the model of the dut's own pair stream (the
    // reference is out of step from here on, so this runs last)
    skip_mode = 1;
    random_phase(12);
    skip_mode = 0;
    $display("skip phase: %0d captures (%0d done), %0d stored words, %0d not the pair of their edge, %0d gaps other than D + 1 edges, %0d first words late",
             sm_caps, sm_done, sm_nwr, sm_valbad, sm_gapbad, sm_firstbad);
    check(sm_done == sm_caps && sm_nwr > 0 && sm_valbad == 0, "K = 0, D > 0 (random settings): every stored word is the captured pair of its write clock edge");
    check(sm_gapbad == 0 && sm_firstbad == 0, "K = 0, D > 0 (random settings): words D + 1 edges apart, the first within D + 1 edges of the release");

    $display("RESULT: %0d checks, %0d failed -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  initial begin
    #400_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
