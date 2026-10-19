// Testbench for segmented capture and hardware averaging (fpga/al3/src/zaklad.v, version 0x1536).
// Run with fpga/al3/tb/run.sh (iverilog, ONLY=seg); seg_model.py checks the logged runs.
// Two instances share every input:
//   dut  = src/zaklad.v (module fnirsi_1013D)
//   refd = the 0x1535 zaklad.v from git (REF1535_REV), modules renamed with a _ref suffix
// Phase 1 (modes off): random captures with K = 0..3, FIR eligible or not, trigger source CH1/CH2/EXT,
//   generator and EXT noise, and random writes of 0x57 (mode 0 or 3, random flag bits), 0x58..0x5D.
//   Output pins, capture registers, decimator, every MCU bus read (0x0A included) and the whole sample
//   memory after each capture must be identical to 0x1535, and the sequencer must stay idle.
// Phase 2 (segments): EXT and CH1 triggered segment runs, legacy and 16-bit (CIC, FIR), auto time-out,
//   hold-off, circular overflow, stop. Logged for seg_model.py: every memory write with its virtual and
//   physical address, the restarts, the EXT edges, the re-arm instants, the table read over the bus and
//   a dump of the slots. Directed checks: status, counts, CH1 trigger position in every segment.
// Phase 3 (averaging): N = 1, random records in legacy and 16-bit format, positions, saturation, shift,
//   clamp and raw readout, skipped untriggered records, back-to-back runs, trigger position, stop. Each
//   record is logged from the sample memory when the post-pass starts, the accumulator is read over the
//   bus; seg_model.py recomputes it.
// Phase 4: modes off again, random equivalence with 0x1535.
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

  fnirsi_1013D_ref refd (
    .i_xtal(i_xtal), .i_mcu_clk(mcu_clk), .i_mcu_rws(mcu_rws), .i_mcu_dcs(mcu_dcs),
    .i_adc1A_d(adc1A), .i_adc1B_d(adc1B), .i_adc2A_d(adc2A), .i_adc2B_d(adc2B),
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

  wire [71:0] st_dut = {dut.trigger_address, dut.trigger_read_address, dut.sample_write_address,
                        dut.sampling_triggered, dut.sample_write_enable, dut.sampling_half_way,
                        dut.sample_system_state, dut.sample_read_offset, dut.sample_write_clock,
                        dut.ext_activity, dut.ext_pending, dut.ext_level, 12'd0};
  wire [71:0] st_ref = {refd.trigger_address, refd.trigger_read_address, refd.sample_write_address,
                        refd.sampling_triggered, refd.sample_write_enable, refd.sampling_half_way,
                        refd.sample_system_state, refd.sample_read_offset, refd.sample_write_clock,
                        refd.ext_activity, refd.ext_pending, refd.ext_level, 12'd0};
  // decimator, memory write port (data, address, enable), trigger compare data
  wire [65:0] dc_dut = {dut.dec_out2, dut.dec_out1, dut.dec_wr, dut.swc_adv, dut.swc_adv_q, dut.trigger_data,
                        dut.samples.dia[7:0], dut.samples.addra, dut.samples.cea};
  wire [65:0] dc_ref = {refd.dec_out2, refd.dec_out1, refd.dec_wr, refd.swc_adv, refd.swc_adv_q, refd.trigger_data,
                        refd.samples.dia[7:0], refd.samples.addra, refd.samples.cea};

  //---------------------------------------------------------------------------------
  // Bookkeeping

  integer errors = 0;
  integer checks = 0;
  integer eq_mismatch = 0;
  integer eq_samples = 0;
  reg     eq_on = 0;

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
      if (eq_on) begin
        eq_samples = eq_samples + 1;
        if ((out_dut & out_cmp_mask) !== (out_ref & out_cmp_mask) || st_dut !== st_ref || pad_dut !== pad_ref || dc_dut !== dc_ref
            || dut.q_active !== 1'b0 || dut.seq_rst_c !== 1'b0) begin
          eq_mismatch = eq_mismatch + 1;
          if (eq_mismatch <= 10)
            $display("MISMATCH t=%0t out %h/%h st %h/%h dc %h/%h pad %b/%b seq %b%b", $time, out_dut, out_ref,
                     st_dut, st_ref, dc_dut, dc_ref, pad_dut, pad_ref, dut.q_active, dut.seq_rst_c);
        end
      end
    end
  end

  integer mi, mem_diff;
  task compare_memory;
    begin
      mem_diff = 0;
      for (mi = 0; mi < 8192; mi = mi + 1)
        if (dut.samples.mem[mi] !== refd.samples.mem[mi]) mem_diff = mem_diff + 1;
      eq_samples = eq_samples + 1;
      if (mem_diff != 0) begin
        eq_mismatch = eq_mismatch + 1;
        if (eq_mismatch <= 10) $display("MISMATCH t=%0t sample memory: %0d words differ", $time, mem_diff);
      end
    end
  endtask

  //---------------------------------------------------------------------------------
  // ADC stimulus, updated every 7 ns at times that never coincide with a clock edge
  // adc_mode 0 = triangles, 1 = random bytes, 2 = step (step_val selects step_lo / step_hi, CH2 gets
  // the complement), 3 = flat (flat_val), 4 = telegraph

  integer    adc_mode = 0;
  reg        step_val = 0;
  reg  [7:0] step_lo = 50, step_hi = 200;
  reg  [7:0] flat_val = 0;
  reg        tele = 0;
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
        default: begin adc1A = flat_val; adc1B = flat_val; adc2A = flat_val; adc2B = flat_val; end
      endcase
    end
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
      if (eq_on && bus_cmp_on) begin
        eq_samples = eq_samples + 1;
        if (rd_d !== rd_r) begin
          eq_mismatch = eq_mismatch + 1;
          if (eq_mismatch <= 10) $display("MISMATCH t=%0t bus read %h/%h cmd %h/%h", $time, rd_d, rd_r, dut.mcu_command, refd.mcu_command);
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
  reg     rel_mark = 0;                                 // set at the release of 0x01 for the Z log line
  reg     seg_log_arm = 0;                              // start the segment write log once 0x01 = 1
  task start_capture;
    begin
      wr1(8'h01, 8'h01);
      if (seg_log_arm) begin
        @(posedge dut.sample_write_clock);              // the reset acts from this edge on
        #1 seg_log = 1; seg_log_arm = 0;
      end
      polls = 0;
      rd1(8'h05);
      while (rd8_d[0] !== 1'b1 && polls < 200000) begin rd1(8'h05); polls = polls + 1; end
      rel_mark = 1;
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

  reg [1:0] negab = 0;
  task set_fir(input [1:0] k, input swap, input [5:0] sh, input [31:0] divider, input fir);
    begin
      wr1(8'h56, {7'd0, fir});
      wr1(8'h53, {3'd0, negab, swap, k});
      wr1(8'h54, {2'd0, sh});
      wr4(8'h0D, divider);
      #((k + 10) * ((divider / 2) + 1) * 10 + 500);
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
  function [5:0] rec_shift(input [1:0] k, input [31:0] divider);
    rec_shift = shift_for(k, 2 * ((divider / 2) + 1));
  endfunction
  function [5:0] rec_shift_fir(input [1:0] k, input [31:0] divider);
    rec_shift_fir = shift_for(k, (divider / 2) + 1) + 1;
  endfunction

  //---------------------------------------------------------------------------------
  // Sequencer helpers

  integer run_id = 0;
  integer logf = 0;
  reg     seg_log = 0;                                  // log writes / restarts / re-arms of a segment run

  // configure 0x57..0x5C (written before the arm; latched while 0x01 = 1)
  task seq_cfg(input [1:0] mode, input circ, input raw, input [11:0] count, input [3:0] m, input [15:0] n,
               input [4:0] sh, input [31:0] hold);
    begin
      wr1(8'h57, {4'd0, raw, circ, mode});
      wr2(8'h58, {4'd0, count});
      wr1(8'h59, {4'd0, m});
      wr2(8'h5A, n);
      wr1(8'h5B, {3'd0, sh});
      wr4(8'h5C, hold);
    end
  endtask

  reg [7:0] stb [0:7];
  integer   st_cnt, st_untrig, st_idx, st_q;
  reg [7:0] st_flags;
  task rd_status;
    integer i;
    begin
      wr_cmd(8'h2A);
      for (i = 0; i < 8; i = i + 1) begin rd_byte; stb[i] = rd_d; end
      st_flags  = stb[0];
      st_cnt    = {stb[1], stb[2], stb[3]};
      st_untrig = {stb[4], stb[5]};
      st_q      = stb[6][7:5];
      st_idx    = {stb[6][3:0], stb[7]};
    end
  endtask

  // accumulator / table words first .. first + n - 1 over the bus: per byte one pass from 0x1F with one
  // dummy strobe (the 0x20..0x23 read pipeline)
  reg [63:0] accw [0:2047];
  task rd_acc(input integer first, input integer n);
    integer b, i;
    begin
      for (b = 0; b < 8; b = b + 1) begin
        wr2(8'h1F, first);
        wr_cmd(8'h2B + b);
        rd_byte;
        for (i = 0; i < n; i = i + 1) begin
          rd_byte;
          accw[i][8 * b +: 8] = rd_d;
        end
      end
    end
  endtask

  task wait_seq(input integer max_polls);
    begin
      wait_done(max_polls);
    end
  endtask

  // run header for seg_model.py: R(un) id mode fmt16 count n total pre m circ word_ns
  task log_run(input [1:0] mode, input integer count, input integer n, input integer total, input integer pre,
               input integer m, input circ, input integer word_ns, input integer inc, input integer hold);
    begin
      run_id = run_id + 1;
      $display("run %0d mode %0d at %0d us", run_id, mode, $time / 1000);
      $fdisplay(logf, "RUN %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d", run_id, mode, dut.dec_on, count, n, total,
                pre, m, circ, word_ns, inc, hold);
    end
  endtask

  task log_acc(input integer n, input [4:0] sh, input raw);
    integer i;
    begin
      for (i = 0; i < n; i = i + 1)
        $fdisplay(logf, "O %0d %0d %0d %0d %h", run_id, sh, raw, i, accw[i]);
    end
  endtask

  task log_table(input integer n);
    integer i;
    begin
      for (i = 0; i < n; i = i + 1)
        $fdisplay(logf, "T %0d %0d %h", run_id, i, accw[i]);
    end
  endtask

  task log_slots(input integer words);
    integer i;
    begin
      for (i = 0; i < words; i = i + 1)
        $fdisplay(logf, "M %0d %0d %h", run_id, i, dut.samples.mem[i]);
    end
  endtask

  task log_status;
    begin
      $fdisplay(logf, "S %0d %h %0d %0d %0d", run_id, st_flags, st_cnt, st_untrig, st_idx);
    end
  endtask

  //---------------------------------------------------------------------------------
  // Monitors for the log

  // Z: first write clock edge after the release of 0x01 (ts origin)
  always @(posedge dut.sample_write_clock) begin
    if (rel_mark && dut.sample_system_reset === 1'b0) begin
      rel_mark = 0;
      if (logf) $fdisplay(logf, "Z %0d %0d", run_id, $realtime * 1000);
    end
    if (seg_log) begin
      if (dut.seq_rst_c) $fdisplay(logf, "B %0d %0d", run_id, $realtime * 1000);
      if (dut.sample_write_enable & dut.swc_adv)
        $fdisplay(logf, "W %0d %0d %0d %h %0d", run_id, dut.sample_write_address, dut.sample_write_address_phys,
                  dut.sample_dia, $realtime * 1000);
    end
  end

  // re-arm (trigger enabled) and record end instants
  reg hw_prev = 0, swe_prev = 0;
  reg rearm_log = 0;                                    // H / D lines in averaging runs too
  always @(posedge dut.clk_200MHz) begin
    if ((seg_log | rearm_log) && dut.sampling_half_way && !hw_prev) $fdisplay(logf, "H %0d %0d", run_id, $realtime * 1000);
    if ((seg_log | rearm_log) && !dut.sample_write_enable && swe_prev) $fdisplay(logf, "D %0d %0d", run_id, $realtime * 1000);
    hw_prev = dut.sampling_half_way;
    swe_prev = dut.sample_write_enable;
  end

  // segment end as the sequencer sees it (entry fields), and each averaged record from the sample memory
  integer rj, rstart;
  always @(posedge dut.clk_200MHz) begin
    if (logf && dut.q_st == 3'd4)
      $fdisplay(logf, "E %0d %0d %0d %0d %0d", run_id, dut.q_idx, dut.sn_ta, dut.sn_trig, dut.sn_ts);
  end
  always @(posedge dut.q_post) begin
    rstart = (dut.sn_ta - dut.q_pre) & 8191;
    $fwrite(logf, "R %0d %0d %0d %0d", run_id, dut.q_idx, dut.q_base, rstart);
    for (rj = 0; rj < dut.q_total; rj = rj + 1)
      $fwrite(logf, " %h", dut.samples.mem[(rstart + rj) & 8191]);
    $fwrite(logf, "\n");
  end

  // EXT pulse train: count rising edges, gaps gap_min + random(0 .. gap_rnd) ns, high for width ns.
  // X lines: time of the rising edge and the virtual write address at that moment.
  reg pulser_busy = 0;
  reg stop_on_done = 0;
  task ext_pulses(input integer count, input integer gap_min, input integer gap_rnd, input integer width);
    integer i;
    begin
      pulser_busy = 1;
      for (i = 0; i < count && !(stop_on_done && dut.q_done); i = i + 1) begin
        #(gap_min + (($random(seed) & 32'h7FFFFFFF) % (gap_rnd + 1)) + 0.3);
        ext_val = 1;
        $fdisplay(logf, "X %0d %0d %0d", run_id, $realtime * 1000, dut.sample_write_address);
        #(width);
        ext_val = 0;
      end
      pulser_busy = 0;
    end
  endtask

  // CH1 step pulses (adc_mode 2): step_val high for width ns
  task step_pulses(input integer count, input integer gap_min, input integer gap_rnd, input integer width);
    integer i;
    begin
      for (i = 0; i < count; i = i + 1) begin
        #(gap_min + (($random(seed) & 32'h7FFFFFFF) % (gap_rnd + 1)) + 0.3);
        step_val = 1;
        #(width);
        step_val = 0;
      end
    end
  endtask

  //---------------------------------------------------------------------------------
  // Phase 1 / 4: random equivalence with 0x1535, sequencer off

  integer it, k, nread;
  reg [31:0] rnd, rnd2, dv;
  reg [12:0] pre_r, tot_r;
  reg [1:0]  kr;
  reg [5:0]  shr;
  reg gen_on;

  task random_phase(input integer iterations);
    begin
      eq_on = 1; bus_cmp_on = 1;
      for (it = 0; it < iterations; it = it + 1) begin
        rnd = $random(seed); rnd2 = $random(seed);
        kr = rnd2[1:0];
        if (kr == 0) dv = rnd[17:16] + (rnd[18] ? 32'd0 : 32'd6);
        else         dv = rnd[19] ? rnd[23:20] : 32'd14 + rnd[28:24];
        shr = (kr == 0) ? rnd2[13:8] : (rnd[12] ? rec_shift_fir(kr, dv) : rec_shift(kr, dv));
        // sequencer registers: mode 0 or 3 (off), random flag bits and values
        wr1(8'h57, {rnd2[31:26], rnd2[25] ? 2'd3 : 2'd0});
        wr2(8'h58, $random(seed));
        wr1(8'h59, $random(seed));
        wr2(8'h5A, $random(seed));
        wr1(8'h5B, $random(seed));
        wr4(8'h5C, $random(seed) & 32'h000003FF);
        wr1(8'h5D, $random(seed));
        wr1(8'h56, 8'h00);
        wr1(8'h53, {3'd0, rnd2[5:4], rnd[7], kr});
        wr1(8'h54, shr);
        wr2(8'h55, (kr == 0) ? $random(seed) : (16'd128 << (rnd2[7:6])) + rnd2[31:20]);
        wr1(8'h15, {6'd0, rnd[9:8]});
        wr1(8'h16, {7'd0, rnd[1]});
        wr1(8'h17, 8'd60 + (rnd[15:8] % 8'd130));
        wr1(8'h1A, {7'd0, rnd[2]});
        wr1(8'h0F, {7'd0, rnd[3]});
        wr4(8'h0D, dv);
        wr1(8'h56, {rnd2[24:18], rnd[12]});
        pre_r = 13'd20 + (rnd[24:16] % 13'd200);
        tot_r = pre_r + 13'd20 + ($random(seed) & 13'h00FF);
        wr2(8'h0B, {3'd0, pre_r});
        wr2(8'h0C, {3'd0, tot_r});
        wr4(8'h0E, (kr == 0) ? 32'd2000 + ($random(seed) & 32'h3FFF) : {19'd0, tot_r} + ($random(seed) & 32'h00FF));
        wr2(8'h32, $random(seed)); wr2(8'h35, $random(seed));
        wr1(8'h33, $random(seed) % 6); wr1(8'h36, $random(seed) % 6);
        wr1(8'h34, $random(seed)); wr1(8'h37, $random(seed));
        wr2(8'h38, $random(seed));
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
        // FIR settling when it engages (the reference sees the same writes; only the data delay)
        #((kr + 10) * ((dv / 2) + 1) * 10 + 500);
        start_capture;
        wait_done(100000);
        rd2(8'h14);
        rd1(8'h18);
        rd1(8'h28);
        rd2(8'h29);
        // 0x2A exists only in 0x1536: not compared, must not disturb anything
        bus_cmp_on = 0;
        wr_cmd(8'h2A); rd_byte; rd_byte;
        bus_cmp_on = 1;
        wr2(8'h1F, {3'd0, rd16_d[12:0]});
        nread = 4 + (rnd[31:30] * 4);
        for (k = 0; k < nread; k = k + 1) rd1(8'h20 + (k % 4));
        for (k = 0; k < 4; k = k + 1) rd1(8'h24 + k);
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

  // one full-memory flat capture at K = 1 rewrites all 8192 words in both instances and aligns them
  task resync;
    begin
      set_fir(2'd1, 1'b0, 6'd0, 32'd1, 1'b0);
      adc_mode = 3; flat_val = 8'd20;
      wr1(8'h57, 8'h00);
      wr1(8'h15, 8'h00);
      wr2(8'h55, 16'hFFFF);
      setup_capture(2'd0, 1'b0, 1'b0, 32'd1, 13'd100, 13'd200);
      wr4(8'h0E, 32'd9000);
      start_capture;
      wait_done(40000);
      check(done && dut.sampling_triggered == 1'b0, "resync capture ran to the time-out");
      wr2(8'h1F, 16'd0);                                 // 0x2B..0x32 reads moved the dut's read pointer
      rd2(8'h14);                                        // full-byte read: o_mcu_data equal again (0x05 and
                                                         // 0x0A only set bit 0)
      set_fir(2'd0, 1'b0, 6'd0, 32'd1, 1'b0);
      adc_mode = 0;
    end
  endtask

  //---------------------------------------------------------------------------------
  // Segment runs

  integer word_ns, inc5, i, j, nseg, mm, segw, pre_s, tot_s;
  integer delta0, cross_i, ta_e, off_e, base_e, ok_pos, cnt_ok;
  reg [15:0] lv;

  // legacy or 16-bit word period in ns and the timestamp increment per write clock edge (5 ns units)
  task geometry(input [1:0] kk, input [31:0] divider);
    begin
      if (kk == 0) begin word_ns = 10 * (divider + 1); inc5 = 2 * (divider + 1); end
      else         begin word_ns = 10 * ((divider / 2) + 1); inc5 = 2; end
    end
  endtask

  // EXT triggered segment run: nseg segments of 2^m words, pre/total, n pulses, then the reads and logs
  task seg_ext_run(input [1:0] kk, input fir, input [31:0] divider, input integer ns, input integer m,
                   input integer pre, input integer total, input circ, input integer pulses, input integer gap,
                   input integer hold, input stop_after);
    begin
      geometry(kk, divider);
      if (kk != 0) set_fir(kk, 1'b0, fir ? rec_shift_fir(kk, divider) : rec_shift(kk, divider), divider, fir);
      else         set_fir(2'd0, 1'b0, 6'd0, divider, 1'b0);
      wr1(8'h50, 8'h00);
      ext_en = 1; ext_val = 0;
      setup_capture(2'd2, 1'b0, 1'b1, divider, pre, total);
      seq_cfg(2'd1, circ, 1'b0, ns, m, 16'd1, 5'd0, hold);
      log_run(2'd1, ns, 1, total, pre, m, circ, word_ns, inc5, hold);
      seg_log_arm = 1;
      start_capture;
      ext_pulses(pulses, gap, gap / 4, 100);
      if (stop_after) begin
        #(gap);
        wr1(8'h5D, 8'h01);
      end
      wait_seq(400000);
      seg_log = 0;
      check(done, "segment run completes");
      rd_status; log_status;
      rd_acc(0, ns);
      log_table(ns);
      log_slots(ns << m);
      ext_en = 0; ext_val = 0;
    end
  endtask

  //---------------------------------------------------------------------------------
  // Averaging runs

  // EXT triggered averaging run with random ADC data (adc_mode set by the caller)
  integer pulses_needed;
  task avg_ext_run(input [1:0] kk, input fir, input [31:0] divider, input integer npos, input integer n,
                   input integer pre, input integer total, input integer gap, input mode_auto,
                   input integer timeout_words);
    begin
      geometry(kk, divider);
      if (kk != 0) set_fir(kk, 1'b0, fir ? rec_shift_fir(kk, divider) : rec_shift(kk, divider), divider, fir);
      else         set_fir(2'd0, 1'b0, 6'd0, divider, 1'b0);
      wr1(8'h50, 8'h00);
      ext_en = 1; ext_val = 0;
      setup_capture(2'd2, 1'b0, mode_auto ? 1'b0 : 1'b1, divider, pre, total);
      if (mode_auto) wr4(8'h0E, timeout_words);
      seq_cfg(2'd2, 1'b0, 1'b0, npos, 4'd0, n, 5'd0, 32'd0);
      log_run(2'd2, npos, n, total, pre, 0, 0, word_ns, inc5, 0);
      start_capture;
      rearm_log = 1;
      pulses_needed = npos * n;
      stop_on_done = 1;
      fork
        ext_pulses(2 * pulses_needed + 4, gap, gap / 4, 50);
      join_none
      wait_seq(2000000);
      rearm_log = 0;
      check(done, "averaging run completes");
      rd_status; log_status;
      wait (pulser_busy == 0);
      stop_on_done = 0;
      ext_en = 0; ext_val = 0;
    end
  endtask

  // read the accumulator with shift sh (raw: 0x57 bit 3) and log it
  task avg_read(input integer n, input [4:0] sh, input raw);
    begin
      wr1(8'h5B, {3'd0, sh});
      wr1(8'h57, {4'd0, raw, 1'b0, 2'd2});              // raw bit is a live readout setting
      rd_acc(0, n);
      log_acc(n, sh, raw);
    end
  endtask

  //---------------------------------------------------------------------------------
  // Main sequence

  reg [8*64-1:0] logname;
  integer p1n;
  integer t_end;

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    if (!$value$plusargs("logfile=%s", logname)) logname = "seg.log";
    if (!$value$plusargs("p1=%d", p1n)) p1n = 12;
    logf = $fopen(logname, "w");
    $display("tb_seg: seed %0d, log %0s", seed, logname);
    #200;

    //-------------------------------------------------------------------------------
    // Phase 1: equivalence with 0x1535, sequencer off
    mm0 = eq_mismatch;
    random_phase(p1n);
    $display("phase 1: %0d equivalence samples, %0d mismatches", eq_samples, eq_mismatch);
    check(eq_mismatch == mm0, "modes off: pins, decimator, bus reads, capture regs, memory identical to 0x1535");

    wr1(8'h50, 8'h00);
    ext_en = 0;
    rd2(8'h06);
    $display("version dut %h ref %h", rd16_d, rd16_r);
    check(rd16_d == 16'h153A, "cmd 0x06 reads 0x153A");
    check(rd16_r == 16'h1535, "reference cmd 0x06 reads 0x1535");
    wr1(8'h57, 8'h00);
    rd_status;
    check(st_flags == 8'h00 && st_cnt == 0, "0x2A reads idle with the modes off");

    //-------------------------------------------------------------------------------
    // Phase 2: segments
    // S1 legacy 10 MSa/s (write clock 100 ns), EXT, 6 segments of 64 words
    seg_ext_run(2'd0, 1'b0, 32'd9, 6, 6, 10, 50, 1'b0, 6, 9000, 0, 1'b0);
    check(st_flags == 8'h81 && st_cnt == 6 && st_untrig == 0, "S1: done, 6 segments, all triggered");
    // S2 legacy 200 MSa/s, 8 segments of 128 words, hold-off 1 us
    seg_ext_run(2'd0, 1'b0, 32'd0, 8, 7, 30, 120, 1'b0, 8, 3000, 200, 1'b0);
    check(st_flags == 8'h81 && st_cnt == 8, "S2: done, 8 segments");
    // S3 hold-off longer than the pulse gap: pulses every 600 ns, hold-off 2 us, 5 segments of 32 words
    seg_ext_run(2'd0, 1'b0, 32'd1, 5, 5, 4, 24, 1'b0, 60, 600, 400, 1'b0);
    check(st_flags == 8'h81 && st_cnt == 5, "S3: done, 5 segments with hold-off");
    // S4 circular, 4 slots of 128 words, 7 pulses, then stop
    seg_ext_run(2'd0, 1'b0, 32'd3, 4, 7, 20, 100, 1'b1, 7, 6000, 0, 1'b1);
    check(st_flags[7] && st_flags[6] && st_flags[2] && st_cnt == 7, "S4: circular run stopped after 7 segments, overflow set");
    check(accw[3][63] == 1'b0 && accw[0][63] && accw[1][63] && accw[2][63], "S4: slot being refilled is invalid, the others valid");
    // S5 one-shot stopped after 3 of 8
    seg_ext_run(2'd0, 1'b0, 32'd3, 8, 6, 10, 40, 1'b0, 3, 3000, 0, 1'b1);
    check(st_flags[7] && !st_flags[6] && st_cnt == 3, "S5: one-shot run stopped after 3 segments");
    // S6..S8 16-bit: CIC2 at N = 4, CIC2 + FIR at N = 20, CIC3 + FIR at N = 64, boxcar at N = 2
    seg_ext_run(2'd2, 1'b0, 32'd3, 4, 7, 20, 100, 1'b0, 4, 6000, 0, 1'b0);
    check(st_flags == 8'h91 && st_cnt == 4, "S6: CIC2 N 4 segments done, 16-bit flag");
    seg_ext_run(2'd2, 1'b1, 32'd19, 4, 6, 10, 50, 1'b0, 4, 12000, 0, 1'b0);
    check(st_flags == 8'h91 && st_cnt == 4, "S7: CIC2 + FIR N 20 segments done");
    seg_ext_run(2'd3, 1'b1, 32'd62, 3, 5, 6, 24, 1'b0, 3, 20000, 100, 1'b0);
    check(st_flags == 8'h91 && st_cnt == 3, "S8: CIC3 + FIR N 64 segments done");
    seg_ext_run(2'd1, 1'b0, 32'd0, 16, 5, 6, 28, 1'b0, 16, 800, 0, 1'b0);
    check(st_flags == 8'h91 && st_cnt == 16, "S9: boxcar N 2, 16 segments of 32 words");

    // S10 auto time-out: no trigger, 4 untriggered segments, offset = pre
    set_fir(2'd0, 1'b0, 6'd0, 32'd1, 1'b0);
    adc_mode = 3; flat_val = 8'd40;
    setup_capture(2'd0, 1'b0, 1'b0, 32'd1, 13'd12, 13'd40);
    wr1(8'h17, 8'd200);
    wr4(8'h0E, 32'd60);
    seq_cfg(2'd1, 1'b0, 1'b0, 12'd4, 4'd6, 16'd1, 5'd0, 32'd0);
    log_run(2'd1, 4, 1, 40, 12, 6, 0, 20, 4, 0);
    seg_log_arm = 1;
    start_capture;
    wait_seq(100000);
    seg_log = 0;
    rd_status; log_status;
    check(done && st_flags == 8'h81 && st_cnt == 4 && st_untrig == 4, "S10: 4 untriggered segments");
    rd_acc(0, 4); log_table(4); log_slots(4 << 6);
    cnt_ok = 1;
    for (i = 0; i < 4; i = i + 1)
      if (accw[i][63] !== 1'b1 || accw[i][53] !== 1'b0 || accw[i][52:40] !== 13'd12) cnt_ok = 0;
    check(cnt_ok, "S10: entries valid, untriggered, offset = pre");

    // S11 CH1 level trigger: per segment, the first sample past the level is at offset + delta0,
    // delta0 measured with a normal single capture (K = 0, D = 9)
    adc_mode = 2; step_val = 0;
    wr1(8'h57, 8'h00);
    set_fir(2'd0, 1'b0, 6'd0, 32'd9, 1'b0);
    wr1(8'h17, 8'd128);
    setup_capture(2'd0, 1'b0, 1'b1, 32'd9, 13'd100, 13'd300);
    #500;
    start_capture;
    wait (dut.sampling_half_way === 1'b1);
    #2000;
    step_val = 1;
    wait_done(20000);
    rd2(8'h14);
    cross_i = -1;
    for (i = 1; i < 300 && cross_i < 0; i = i + 1)
      if (dut.samples.mem[i - 1][7:0] < 128 && dut.samples.mem[i][7:0] >= 128) cross_i = i;
    delta0 = cross_i - rd16_d[12:0];
    $display("K = 0 single capture: trigger_address %0d, crossing at %0d, delta0 %0d", rd16_d[12:0], cross_i, delta0);
    step_val = 0;
    #2000;
    setup_capture(2'd0, 1'b0, 1'b1, 32'd9, 13'd20, 13'd100);
    seq_cfg(2'd1, 1'b0, 1'b0, 12'd5, 4'd7, 16'd1, 5'd0, 32'd0);
    log_run(2'd1, 5, 1, 100, 20, 7, 0, 100, 20, 0);
    seg_log_arm = 1;
    start_capture;
    fork
      step_pulses(5, 15000, 3000, 2500);
    join_none
    wait_seq(400000);
    seg_log = 0;
    rd_status; log_status;
    rd_acc(0, 5); log_table(5); log_slots(5 << 7);
    check(done && st_cnt == 5 && st_untrig == 0, "S11: 5 CH1-triggered segments");
    ok_pos = 1;
    for (i = 0; i < 5; i = i + 1) begin
      off_e = accw[i][52:40];
      base_e = i << 7;
      cross_i = -1;
      for (j = 1; j < 100 && cross_i < 0; j = j + 1)
        if (dut.samples.mem[base_e + ((off_e - 20 + j - 1) & 127)][7:0] < 128 &&
            dut.samples.mem[base_e + ((off_e - 20 + j) & 127)][7:0] >= 128) cross_i = j;
      if (cross_i != 20 + delta0) begin
        ok_pos = 0;
        $display("S11 segment %0d: offset %0d, crossing at record index %0d, expected %0d", i, off_e, cross_i, 20 + delta0);
      end
    end
    check(ok_pos, "S11: in every segment the crossing is at record index pre + delta0");
    adc_mode = 0;

    //-------------------------------------------------------------------------------
    // Phase 3: averaging
    adc_mode = 1;
    // A1 N = 1: the accumulator equals the record in the sample memory (mirror check)
    avg_ext_run(2'd0, 1'b0, 32'd0, 1, 1, 20, 300, 2000, 1'b0, 0);
    check(st_flags == 8'h82 && st_cnt == 1, "A1: N = 1 run done");
    avg_read(300, 5'd0, 1'b0);
    // A2 legacy 200 MSa/s, N = 8, 1 position, shifts 0 / 3 and raw
    avg_ext_run(2'd0, 1'b0, 32'd0, 1, 8, 40, 500, 10000, 1'b0, 0);
    check(st_flags == 8'h82 && st_cnt == 8, "A2: legacy N = 8 done");
    avg_read(500, 5'd0, 1'b0);
    avg_read(500, 5'd3, 1'b0);
    avg_read(500, 5'd0, 1'b1);
    // A3 legacy 50 MSa/s, N = 5, 3 positions, total 200 (600 accumulator words)
    avg_ext_run(2'd0, 1'b0, 32'd3, 3, 5, 25, 200, 14000, 1'b0, 0);
    check(st_flags == 8'h82 && st_cnt == 15, "A3: 3 positions x 5 done");
    avg_read(600, 5'd2, 1'b0);
    // A4 back-to-back run with other data: the first record of each position is written, not added
    adc_mode = 0;
    avg_ext_run(2'd0, 1'b0, 32'd3, 3, 2, 25, 200, 14000, 1'b0, 0);
    check(st_cnt == 6, "A4: second run, 3 positions x 2");
    avg_read(600, 5'd0, 1'b0);
    // A5 legacy saturation: 255 x 300 > 65535 (clamps), 200 x 300 = 60000 (exact)
    adc_mode = 3; flat_val = 8'd255;
    avg_ext_run(2'd0, 1'b0, 32'd0, 1, 300, 4, 20, 700, 1'b0, 0);
    check(st_cnt == 300, "A5: N = 300 done");
    avg_read(20, 5'd0, 1'b0);
    cnt_ok = 1;
    for (i = 0; i < 20; i = i + 1) if (accw[i] !== 64'hFFFF_FFFF_FFFF_FFFF) cnt_ok = 0;
    check(cnt_ok, "A5: 300 x 255 saturates every legacy lane at 65535");
    flat_val = 8'd200;
    avg_ext_run(2'd0, 1'b0, 32'd0, 1, 300, 4, 20, 700, 1'b0, 0);
    avg_read(20, 5'd0, 1'b0);
    cnt_ok = 1;
    for (i = 0; i < 20; i = i + 1) if (accw[i] !== {4{16'd60000}}) cnt_ok = 0;
    check(cnt_ok, "A5: 300 x 200 = 60000 in every lane");
    // A6 16-bit: CIC2 + FIR at N = 20, 2 positions x 16, shift 4 (mean), 0 (clamp) and raw
    adc_mode = 1;
    avg_ext_run(2'd2, 1'b1, 32'd19, 2, 16, 10, 100, 16000, 1'b0, 0);
    check(st_flags == 8'h92 && st_cnt == 32, "A6: 16-bit 2 x 16 done");
    avg_read(200, 5'd4, 1'b0);
    avg_read(200, 5'd0, 1'b0);
    avg_read(200, 5'd0, 1'b1);
    // A7 16-bit boxcar at N = 2 (one word per write clock), N = 4
    avg_ext_run(2'd1, 1'b0, 32'd0, 1, 4, 30, 400, 9000, 1'b0, 0);
    avg_read(400, 5'd2, 1'b0);
    avg_read(400, 5'd0, 1'b1);
    // A8 auto mode: untriggered records between the pulses are skipped
    avg_ext_run(2'd0, 1'b0, 32'd1, 1, 6, 10, 60, 3000, 1'b1, 100);
    check(st_cnt == 6 && st_untrig > 0, "A8: auto mode, untriggered records skipped and counted");
    $display("A8: %0d records accumulated, %0d untriggered skipped", st_cnt, st_untrig);
    avg_read(60, 5'd0, 1'b1);
    // A9 CH1 step trigger, legacy D = 9, N = 4: the averaged record crosses at pre + delta0
    adc_mode = 2; step_val = 0;
    set_fir(2'd0, 1'b0, 6'd0, 32'd9, 1'b0);
    wr1(8'h17, 8'd128);
    setup_capture(2'd0, 1'b0, 1'b1, 32'd9, 13'd30, 13'd100);
    seq_cfg(2'd2, 1'b0, 1'b0, 12'd1, 4'd0, 16'd4, 5'd2, 32'd0);
    log_run(2'd2, 1, 4, 100, 30, 0, 0, 100, 20, 0);
    start_capture;
    fork
      step_pulses(4, 15000, 3000, 6000);
    join_none
    wait_seq(400000);
    check(done && dut.q_cnt == 4, "A9: CH1-triggered averaging done");
    avg_read(100, 5'd2, 1'b0);
    cross_i = -1;
    for (i = 1; i < 100 && cross_i < 0; i = i + 1)
      if (accw[i - 1][15:0] < 128 && accw[i][15:0] >= 128) cross_i = i;
    $display("A9: averaged record crossing at index %0d (pre + delta0 = %0d)", cross_i, 30 + delta0);
    check(cross_i == 30 + delta0, "A9: averaged record crosses at pre + delta0");
    // A10 stop: N = 50 requested, 3 pulses, stop -> done with 3
    adc_mode = 1;
    set_fir(2'd0, 1'b0, 6'd0, 32'd0, 1'b0);
    ext_en = 1; ext_val = 0;
    setup_capture(2'd2, 1'b0, 1'b1, 32'd0, 13'd10, 13'd50);
    seq_cfg(2'd2, 1'b0, 1'b0, 12'd1, 4'd0, 16'd50, 5'd0, 32'd0);
    log_run(2'd2, 1, 50, 50, 10, 0, 0, 10, 2, 0);
    start_capture;
    ext_pulses(3, 2000, 100, 50);
    #3000;
    wr1(8'h5D, 8'h01);
    wait_seq(10000);
    rd_status; log_status;
    check(done && st_cnt == 3 && st_flags[7], "A10: stopped averaging run reports 3 records");
    avg_read(50, 5'd0, 1'b1);
    ext_en = 0;
    $fdisplay(logf, "END");
    $fflush(logf);

    //-------------------------------------------------------------------------------
    // Phase 4: modes off again, memory resynchronised, random equivalence
    wr1(8'h57, 8'h00);
    resync;
    mm0 = eq_mismatch;
    compare_memory;
    random_phase(6);
    $display("phase 4: total %0d equivalence samples, %0d mismatches", eq_samples, eq_mismatch);
    check(eq_mismatch == mm0, "after segment and averaging runs, modes off again identical to 0x1535");

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
