// Testbench for the external trigger on the calibration pin (fpga/al3/src/zaklad.v, 0x1533 feature;
// the design is now 0x1535, decimation off by default, see tb_decim.v).
// Run with fpga/al3/tb/run.sh (iverilog). Two instances share every input:
//   dut = src/zaklad.v (module fnirsi_1013D)
//   refd = Atlan4's 0x1532 zaklad.v from git, modules renamed with a _ref suffix by run.sh
// Phase 1 (equivalence): random CH1/CH2 trigger captures, generator settings and EXT pin activity
//   with trigger source 0/1. Every output pin, the MCU read bus and the capture registers of dut
//   and ref are compared continuously and must stay identical.
// Phase 2 (directed): version word, 0x28 status, generator, EXT rising/falling trigger and
//   trigger_address, glitch filter, short pulses, EXT ignored for source 0/1, EXT vs ADC trigger.
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

  // Calibration pads. The board pad has PULLDOWN in zaklad.adc; model it as a weak 0.
  reg  ext_en  = 0;
  reg  ext_val = 0;
  wire pad_dut, pad_ref;
  assign pad_dut = ext_en ? ext_val : 1'bz;
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

  // Capture state that must match while the trigger source is CH1/CH2
  // 0x153B: after the stop the dut's trigger_address (time-out: W - total + pretrigger) and sampling_triggered (no late
  // trigger) differ from the references by design, and trigger_read_address is frozen at the stop (the references run it on
  // after a 0x0E or 0x1A write); the three are compared while the write runs (tb_timeout.v has the rest)
  wire [12:0] ta_cmp_dut = dut.sample_write_enable ? dut.trigger_address : 13'd0;
  wire [12:0] ta_cmp_ref = dut.sample_write_enable ? refd.trigger_address : 13'd0;
  // fpga-gen12: sampling_triggered (and ext_pending) also not during the reset: a post-stop trigger of the reference is
  // cleared by cap_rst200 on the clk_200MHz edge after the write clock edge that raised sample_write_enable (2.5 ns)
  wire        tr_cmp_dut = (dut.sample_write_enable & ~dut.cap_rst200) ? dut.sampling_triggered : 1'b0;
  wire        tr_cmp_ref = (dut.sample_write_enable & ~dut.cap_rst200) ? refd.sampling_triggered : 1'b0;
  wire [12:0] ra_cmp_dut = dut.sample_write_enable ? dut.trigger_read_address : 13'd0;
  wire [12:0] ra_cmp_ref = dut.sample_write_enable ? refd.trigger_read_address : 13'd0;
  wire [63:0] st_dut = {ta_cmp_dut, ra_cmp_dut, dut.sample_write_address,
                        tr_cmp_dut, dut.sample_write_enable, dut.sampling_half_way,
                        dut.sample_system_state, dut.sample_read_offset, 8'd0};
  wire [63:0] st_ref = {ta_cmp_ref, ra_cmp_ref, refd.sample_write_address,
                        tr_cmp_ref, refd.sample_write_enable, refd.sampling_half_way,
                        refd.sample_system_state, refd.sample_read_offset, 8'd0};

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

  task check(input cond, input [8*96-1:0] what);
    begin
      checks = checks + 1;
      if (!cond) begin
        errors = errors + 1;
        $display("FAIL  t=%0t  %0s", $time, what);
      end
    end
  endtask

  // Continuous equivalence monitor, sampled off every stimulus grid: clock edges are on multiples
  // of 1.25 ns, bus and EXT changes on multiples of 0.1 ns, ADC updates at 0.3 + 7k ns.
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
        if ((out_dut & out_cmp_mask) !== (out_ref & out_cmp_mask) || st_dut !== st_ref || (!ext_en && pad_dut !== pad_ref)) begin
          eq_mismatch = eq_mismatch + 1;
          if (eq_mismatch <= 30)
            $display("MISMATCH t=%0t out %h/%h st %h/%h pad %b/%b  trig %b/%b mode %b tmo %b/%b en %b/%b ta %0d/%0d tra %0d/%0d", $time, out_dut, out_ref, st_dut, st_ref, pad_dut, pad_ref,
                     dut.sampling_triggered, refd.sampling_triggered, dut.trigger_mode, dut.time_base_timeout, refd.time_base_timeout,
                     dut.sampling_enable, refd.sampling_enable, dut.trigger_address, refd.trigger_address, dut.trigger_read_address, refd.trigger_read_address);
        end
      end
    end
  end

  //---------------------------------------------------------------------------------
  // ADC stimulus: free-running triangles (or a step that follows the EXT pin in step mode),
  // updated every 7 ns at times that never coincide with a clock edge

  reg        step_mode = 0;
  reg  [7:0] step_lo = 50, step_hi = 200;
  reg  [7:0] flat_val = 0;
  reg        flat_mode = 0;
  integer    triA = 0, triB = 0, d1 = 3, d2 = -5;

  initial begin
    #0.3;
    forever begin
      #7;
      triA = triA + d1; if (triA > 250) begin triA = 250; d1 = -d1; end if (triA < 5) begin triA = 5; d1 = -d1; end
      triB = triB + d2; if (triB > 250) begin triB = 250; d2 = -d2; end if (triB < 5) begin triB = 5; d2 = -d2; end
      if (step_mode) begin
        adc1A = ext_val ? step_hi : step_lo; adc1B = adc1A;
        adc2A = ext_val ? step_hi : step_lo; adc2B = adc2A;
      end else if (flat_mode) begin
        adc1A = flat_val; adc1B = flat_val; adc2A = flat_val; adc2B = flat_val;
      end else begin
        adc1A = triA; adc1B = triA + 1; adc2A = triB; adc2B = triB + 2;
      end
    end
  end

  //---------------------------------------------------------------------------------
  // MCU bus (as the firmware bit-bangs it: data on the bus, strobe low, sampled on the rising edge)

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
          if (eq_mismatch <= 30) $display("MISMATCH t=%0t bus read %h/%h cmd %h/%h off %0d/%0d memd %h memr %h",
                                          $time, rd_d, rd_r, dut.mcu_command, refd.mcu_command,
                                          dut.sample_read_offset, refd.sample_read_offset,
                                          dut.samples.mem[dut.sample_read_offset-1], refd.samples.mem[refd.sample_read_offset-1]);
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
  // Capture helpers (firmware sequence: reset high, wait for 0x05, reset low, poll 0x0A)

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

  // Wait until the dut has passed the pretrigger point (trigger checks are enabled from there)
  task wait_half_way;
    begin
      wait (dut.sampling_half_way === 1'b1);
    end
  endtask

  //---------------------------------------------------------------------------------
  // EXT edge helper: put the edge at a chosen time after a rising edge of the dut's sample
  // write clock, and return the write address at the moment of the edge.

  reg [12:0] wa_at_edge;
  task ext_edge_at(input real after_swc, input new_val);
    begin
      @(posedge dut.sample_write_clock);
      #(after_swc);
      ext_val = new_val;
      wa_at_edge = dut.sample_write_address;
    end
  endtask

  //---------------------------------------------------------------------------------
  // Standard EXT capture setup

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

  //---------------------------------------------------------------------------------
  // Random equivalence phase

  integer it, k, seed, nread;
  reg [31:0] rnd;
  reg [12:0] pre_r, tot_r;
  reg gen_on;

  task random_phase(input integer iterations);
    begin
      eq_on = 1; bus_cmp_on = 1;
      for (it = 0; it < iterations; it = it + 1) begin
        rnd = $random(seed);
        // configuration through the MCU bus exactly as firmware would
        wr1(8'h15, {7'd0, rnd[0]});                       // CH1 or CH2
        wr1(8'h16, {7'd0, rnd[1]});                       // edge
        wr1(8'h17, 8'd60 + (rnd[15:8] % 8'd130));         // level
        wr1(8'h1A, {7'd0, rnd[2]});                       // auto / normal
        wr1(8'h0F, {7'd0, rnd[3]});
        wr4(8'h0D, rnd[5:4]);                             // 100, 50, 33, 25 MHz write clock
        pre_r = 13'd20 + (rnd[24:16] % 13'd280);
        tot_r = pre_r + 13'd20 + ($random(seed) & 13'h00FF);
        wr2(8'h0B, {3'd0, pre_r});
        wr2(8'h0C, {3'd0, tot_r});
        wr4(8'h0E, 32'd2000 + ($random(seed) & 32'h3FFF));// auto time-out
        wr2(8'h32, $random(seed)); wr2(8'h35, $random(seed));
        wr1(8'h33, $random(seed) % 6); wr1(8'h36, $random(seed) % 6);
        wr1(8'h34, $random(seed)); wr1(8'h37, $random(seed));
        wr2(8'h38, $random(seed));
        wr2(8'h3C, $random(seed));
        gen_on = rnd[6];
        wr1(8'h50, {7'd0, gen_on});
        wr4(8'h51, 32'd85899346 + ($random(seed) & 32'h00FFFFFF)); // about 1 MHz
        wr4(8'h52, $random(seed));
        d1 = 1 + (rnd[27:25]); d2 = -(1 + rnd[30:28]);
        // EXT activity only while the generator is off (else it would fight the pin)
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
        start_capture;
        wait_done(4000);
        // 0x153B: a capture that ended on the auto time-out reports W - total + pretrigger (reference: pretrigger) and
        // never a trigger after the stop: these two reads are checked against the formula instead of the reference
        bus_cmp_on = rnd[2];
        rd2(8'h14);
        rd1(8'h18);
        if (!rnd[2] && !rd_d[0])
          check(rd16_d[12:0] == ((dut.w_last - tot_r + pre_r) & 13'h1FFF), "auto time-out: 0x14 = W - total + pretrigger");
        rd2(8'h41);                                       // refills the read register's stale bits identically in both
        bus_cmp_on = 1;
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
      end
      eq_on = 0; bus_cmp_on = 0;
    end
  endtask

  //---------------------------------------------------------------------------------
  // Main sequence

  integer   t_rise1, t_rise2, period_ns;
  reg [12:0] exp_lo, exp_hi, adc_addr, ext_addr;
  reg [255:0] strength;

  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 12345;
    $display("tb_ext_trigger: seed %0d", seed);

    // Let the PLL model and the dividers start
    #200;

    //-------------------------------------------------------------------------------
    // Phase 1: equivalence with Atlan4's 0x1532 design for trigger sources 0/1
    mm0 = eq_mismatch;
    random_phase(60);
    $display("phase 1: %0d equivalence samples, %0d mismatches, %0d captures the reference cut at its time-out", eq_samples, eq_mismatch, cut_caps);
    check(eq_mismatch == mm0, "CH1/CH2 captures, outputs and bus reads identical to Atlan4 0x1532");

    //-------------------------------------------------------------------------------
    // Version word and status read
    rd2(8'h06);
    $display("version dut %h ref %h", rd16_d, rd16_r);
    check(rd16_d == 16'h153B, "cmd 0x06 reads 0x153B");
    check(rd16_r == 16'h1532, "reference cmd 0x06 reads 0x1532");

    //-------------------------------------------------------------------------------
    // Generator mode unchanged: 1 MHz, 50 %, compared with the reference on the pin
    mm0 = eq_mismatch;
    eq_on = 1;
    ext_en = 0;
    wr1(8'h50, 8'h01);
    wr4(8'h51, 32'd85899346);
    wr4(8'h52, 32'h80000000);
    @(posedge pad_dut); t_rise1 = $time;
    repeat (10) @(posedge pad_dut);
    t_rise2 = $time;
    period_ns = (t_rise2 - t_rise1) / 10;
    $display("generator: mean period %0d ns over 10 cycles (expect 1000)", period_ns);
    check(period_ns >= 990 && period_ns <= 1010, "generator 1 MHz period");
    $sformat(strength, "%v", pad_dut);
    check(strength[23:0] == "St1" || strength[23:0] == "St0", "generator drives the pin strongly when enabled");
    rd1(8'h28);
    check(rd8_d[1] == 1'b1, "0x28 bit1 = 1 while the generator is on");
    wr1(8'h50, 8'h00);
    #100;
    $sformat(strength, "%v", pad_dut);
    $display("pin strength with generator off: %0s", strength[23:0]);
    check(strength[23:0] == "We0", "pin hi-Z with generator off (only the pull-down)");
    check(eq_mismatch == mm0, "generator pin identical to reference");
    eq_on = 0;

    //-------------------------------------------------------------------------------
    // 0x28 live level
    ext_en = 1; ext_val = 0; #100;
    rd1(8'h28);
    check(rd8_d[0] == 1'b0 && rd8_d[1] == 1'b0, "0x28 bit0 = 0 with EXT low, bit1 = 0 with generator off");
    ext_val = 1; #100;
    rd1(8'h28);
    check(rd8_d[0] == 1'b1, "0x28 bit0 = 1 with EXT high");
    check(rd8_d[7:3] == 5'd0, "0x28 bits 7..3 read 0");
    ext_val = 0; #100;

    //-------------------------------------------------------------------------------
    // EXT rising edge, 1 MSa/s write clock (divider 49 = 500 ns), edge mid-period
    setup_capture(2'd2, 1'b0, 1'b1, 32'd49, 13'd100, 13'd200);
    ext_val = 0;
    start_capture;
    rd1(8'h28);
    check(rd8_d[2] == 1'b0, "0x28 bit2 cleared by the sampling reset");
    wait_half_way;
    #3000;
    rd1(8'h18);
    check(rd8_d[0] == 1'b0, "no trigger before the EXT edge");
    ext_edge_at(250.0, 1'b1);
    wait_done(20000);
    check(done, "EXT rising: capture completes");
    rd2(8'h14);
    ext_addr = rd16_d[12:0];
    $display("EXT rising @1MHz write clock: edge at write address %0d, trigger_address %0d", wa_at_edge, ext_addr);
    check(ext_addr == wa_at_edge - 13'd1, "EXT rising: trigger_address = last sample written before the edge");
    rd1(8'h28);
    check(rd8_d[2] == 1'b1 && rd8_d[0] == 1'b1, "0x28 bit2 = edge seen, bit0 = high");
    rd1(8'h18);
    check(rd8_d[0] == 1'b1, "0x18 triggered");
    check(dut.trigger_read_address - dut.trigger_address >= 13'd100, "post-trigger count reached (total - pretrigger)");

    //-------------------------------------------------------------------------------
    // EXT falling edge
    setup_capture(2'd2, 1'b1, 1'b1, 32'd49, 13'd100, 13'd200);
    ext_val = 1; #200;
    start_capture;
    wait_half_way;
    #3000;
    ext_edge_at(100.0, 1'b0);
    wait_done(20000);
    check(done, "EXT falling: capture completes");
    rd2(8'h14);
    $display("EXT falling @1MHz: edge at write address %0d, trigger_address %0d", wa_at_edge, rd16_d[12:0]);
    check(rd16_d[12:0] == wa_at_edge - 13'd1, "EXT falling: trigger_address = last sample before the edge");

    //-------------------------------------------------------------------------------
    // Falling edge selected: a rising edge must not trigger
    ext_val = 0; #200;
    start_capture;
    wait_half_way;
    #3000;
    ext_edge_at(100.0, 1'b1);
    #5000;
    rd1(8'h18);
    check(rd8_d[0] == 1'b0, "EXT falling selected: rising edge ignored");
    ext_edge_at(100.0, 1'b0);
    wait_done(20000);
    check(done, "EXT falling selected: falling edge then triggers");

    //-------------------------------------------------------------------------------
    // 200 MSa/s (divider 0, write clock 100 MHz): edge latency is 2 to 3 write clocks
    setup_capture(2'd2, 1'b0, 1'b1, 32'd0, 13'd300, 13'd600);
    ext_val = 0;
    start_capture;
    wait_half_way;
    #500;
    ext_edge_at(3.0, 1'b1);
    wait_done(20000);
    rd2(8'h14);
    $display("EXT rising @100MHz write clock: edge at write address %0d, trigger_address %0d (offset %0d)",
             wa_at_edge, rd16_d[12:0], rd16_d[12:0] - wa_at_edge);
    check(done && (rd16_d[12:0] - wa_at_edge + 13'd1) <= 13'd4, "EXT at 200 MSa/s: trigger_address within latency window");

    //-------------------------------------------------------------------------------
    // Glitch filter: 3 ns pulse is rejected, 12 ns pulse triggers; 100 ns pulse at slow rate triggers
    setup_capture(2'd2, 1'b0, 1'b1, 32'd49, 13'd100, 13'd200);
    ext_val = 0;
    start_capture;
    wait_half_way;
    #1000;
    @(posedge dut.sample_write_clock); #101.3;
    ext_val = 1; #3; ext_val = 0;
    #3000;
    rd1(8'h18);
    check(rd8_d[0] == 1'b0, "3 ns glitch rejected by the majority filter");
    rd1(8'h28);
    check(rd8_d[2] == 1'b0, "3 ns glitch not counted as EXT activity");
    @(posedge dut.sample_write_clock); #101.3;
    wa_at_edge = dut.sample_write_address;
    ext_val = 1; #12; ext_val = 0;
    wait_done(20000);
    rd2(8'h14);
    check(done && rd16_d[12:0] == wa_at_edge - 13'd1, "12 ns pulse triggers with the right trigger_address");

    //-------------------------------------------------------------------------------
    // Short pulse at a slow write clock (divider 999 = 10 us): 100 ns pulse is latched
    setup_capture(2'd2, 1'b0, 1'b1, 32'd999, 13'd30, 13'd60);
    ext_val = 0;
    start_capture;
    wait_half_way;
    #30000;
    @(posedge dut.sample_write_clock); #3000.3;
    wa_at_edge = dut.sample_write_address;
    ext_val = 1; #100; ext_val = 0;
    wait_done(200000);
    rd2(8'h14);
    $display("100 ns pulse @10us write clock: edge at %0d, trigger_address %0d", wa_at_edge, rd16_d[12:0]);
    check(done && rd16_d[12:0] == wa_at_edge - 13'd1, "pulse shorter than a sample period triggers");

    //-------------------------------------------------------------------------------
    // EXT ignored when the source is CH1 or CH2 (flat ADC data below the level, normal mode)
    flat_mode = 1; flat_val = 8'd20;
    wr1(8'h17, 8'd128);
    for (k = 0; k < 2; k = k + 1) begin
      setup_capture(k[1:0], 1'b0, 1'b1, 32'd9, 13'd100, 13'd200);
      ext_val = 0;
      start_capture;
      wait_half_way;
      repeat (20) begin #(200 + ($random(seed) & 127)); ext_val = ~ext_val; end
      #2000;
      rd1(8'h18);
      check(rd8_d[0] == 1'b0, k ? "source CH2: EXT edges ignored" : "source CH1: EXT edges ignored");
      rd1(8'h0A);
      check(rd8_d[0] == 1'b0, k ? "source CH2: capture still waiting" : "source CH1: capture still waiting");
      rd1(8'h28);
      check(rd8_d[2] == 1'b1, "0x28 bit2 shows EXT activity regardless of the source");
    end
    flat_mode = 0;

    //-------------------------------------------------------------------------------
    // EXT vs ADC: the same step seen by CH1 (level trigger) and by EXT gives the same address
    step_mode = 1;
    wr1(8'h17, 8'd128);
    setup_capture(2'd0, 1'b0, 1'b1, 32'd49, 13'd100, 13'd200);
    ext_val = 0; #500;
    start_capture;
    wait_half_way;
    #3000;
    ext_edge_at(250.0, 1'b1);
    wait_done(20000);
    rd2(8'h14);
    adc_addr = rd16_d[12:0];
    setup_capture(2'd2, 1'b0, 1'b1, 32'd49, 13'd100, 13'd200);
    ext_val = 0; #500;
    start_capture;
    wait_half_way;
    #3000;
    ext_edge_at(250.0, 1'b1);
    wait_done(20000);
    rd2(8'h14);
    ext_addr = rd16_d[12:0];
    $display("step: CH1 trigger_address - edge = %0d, EXT trigger_address - edge = %0d",
             $signed(adc_addr - wa_at_edge), $signed(ext_addr - wa_at_edge));
    // 0x153A: the ADC data reaches the trigger compare and the memory 2.5 T + 15 ns after the pins (A core, stage 4;
    // fpga/al3/README.md "Changes" section 10), EXT is not delayed: at divider 49 (T = 500 ns) CH1 triggers 1265 ns = 2.53
    // words later than EXT, i.e. 2 or 3 words depending on where the step falls in the word (0x1532..0x1539: the same word)
    check((adc_addr - ext_addr == 13'd2) || (adc_addr - ext_addr == 13'd3),
          "EXT and CH1 level trigger on the same step: CH1 2 or 3 words later than EXT at divider 49 (0x153A data latency)");
    step_mode = 0;

    //-------------------------------------------------------------------------------
    // Auto mode with EXT and no edge: the time-out stops the write at W = 0x0E + 2 words and reports
    // trigger_address = W - total + pretrigger (0x153B; 0x1532..0x153A: pretrigger), so the record is the newest total words
    setup_capture(2'd2, 1'b0, 1'b0, 32'd9, 13'd100, 13'd200);
    wr4(8'h0E, 32'd1000);
    ext_val = 0;
    start_capture;
    wait_done(20000);
    rd2(8'h14);
    $display("auto time-out EXT: trigger_address %0d, W %0d", rd16_d[12:0], dut.w_last);
    check(done && dut.w_last == 13'd1002, "auto mode time-out with EXT source: the write stops at 0x0E + 2 words");
    check(done && rd16_d[12:0] == dut.w_last - 13'd200 + 13'd100, "auto mode time-out with EXT source: trigger_address = W - total + pretrigger");

    //-------------------------------------------------------------------------------
    // Source 3 (reserved) acts as EXT
    setup_capture(2'd3, 1'b0, 1'b1, 32'd49, 13'd100, 13'd200);
    ext_val = 0;
    start_capture;
    wait_half_way;
    #3000;
    ext_edge_at(250.0, 1'b1);
    wait_done(20000);
    check(done, "source 3 behaves as EXT");

    //-------------------------------------------------------------------------------
    // Back to CH1 after EXT: CH1 triangle triggers normally and matches the reference again
    // Resynchronise: the EXT captures left trigger_address, memory and the read register different
    // from the reference. One CH1 capture from a common reset plus a full-byte read realigns them.
    // The capture runs in auto mode at 100 MHz write clock until a time-out of 9000 write clocks,
    // so all 8192 memory words are overwritten with identical data in both instances.
    // Flat ADC data below the level keeps it from triggering.
    ext_en = 0;
    flat_mode = 1; flat_val = 8'd20;
    wr1(8'h15, 8'h00);
    wr1(8'h17, 8'd128);
    setup_capture(2'd0, 1'b0, 1'b0, 32'd0, 13'd100, 13'd200);
    wr4(8'h0E, 32'd9000);
    start_capture;
    wait_done(20000);
    check(done && dut.sampling_triggered == 1'b0, "resync capture ran to the time-out");
    flat_mode = 0;
    rd2(8'h41);
    mm0 = eq_mismatch;
    random_phase(10);
    $display("phase 3: total %0d equivalence samples, %0d mismatches, %0d captures the reference cut at its time-out", eq_samples, eq_mismatch, cut_caps);
    check(eq_mismatch == mm0, "after EXT use, CH1/CH2 again identical to Atlan4 0x1532");

    $display("RESULT: %0d checks, %0d failed -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

  // Safety net
  initial begin
    #100_000_000;
    $display("RESULT: timeout -> FAIL");
    $finish;
  end

endmodule
