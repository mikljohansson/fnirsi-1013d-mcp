// Long free-running test of the decimation filter arithmetic (decim_chan, fir_chan of src/zaklad.v).
// Run with fpga/al3/tb/run.sh (step 5) or ONLY=long.
// The two modules are driven directly at the 100 MHz write clock with a copy of the strobe logic of
// the top level ("Decimation filter control and data path"); the top level itself is covered by
// tb_decim.v and tb_fir.v. Per configuration (K, divider, FIR on/off):
//   - the input is exactly periodic: L = M * N samples (M output samples per period) of DC + tone +
//     a noise table repeated every period, so the output must repeat exactly every M samples;
//   - two filter copies get the same input: z starts from zero, r from random 48-bit integrator and
//     comb state, with i1 just below 2^48 so that it wraps during the run (the modulo-2^48 wrap must not
//     matter), so their outputs must be identical;
//   - the output must stay inside (0, 65535) at the recommended shift (no saturation, no clamp);
//   - the mean over each period must equal mean(input) * m^K / 2^shift (m = N, or N/2 with the FIR),
//     within 1 LSB16 + 0.1 %.
// Checked from output sample `skip` on; it prints one line per configuration.
`timescale 1ns / 1ps

module tb;

  reg clk = 0;
  always #5 clk = ~clk;

  reg  [31:0] divider = 0;
  reg  [1:0]  order = 1;
  reg  [5:0]  shift = 0;
  reg         fir_act = 0;
  reg  [7:0]  in_a = 0, in_b = 0;

  //---------------------------------------------------------------------------------
  // Strobe logic, as in the top level
  reg [30:0] dec_pc = 0;
  reg [6:0]  dec_sv = 0;
  reg [30:0] fir_hp = 0;
  reg [7:0]  dec_t = 0;
  wire [30:0] dec_ph = divider[31:1];

  always @(posedge clk)
    begin
      if((dec_pc == 0) || (dec_pc > dec_ph))
        dec_pc <= dec_ph;
      else
        dec_pc <= dec_pc - 1;
      dec_sv <= { dec_sv[5:0], (dec_pc == 0) | (fir_act & (dec_pc == fir_hp)) };
      fir_hp <= dec_ph[30:1] + 31'd1;
      dec_t  <= { dec_t[6:0], (dec_pc == 0) };
    end

  wire [15:0] cz, cr, fz, fr;
  wire        dz, dr;

  decim_chan dz_c (.clk(clk), .in_a(in_a), .in_b(in_b), .order(order),
                   .shift(shift), .sv(dec_sv[5:0]), .peak(1'b0), .wstart(1'b0), .out(cz));
  decim_chan dr_c (.clk(clk), .in_a(in_a), .in_b(in_b), .order(order),
                   .shift(shift), .sv(dec_sv[5:0]), .peak(1'b0), .wstart(1'b0), .out(cr));
  fir_chan   fz_c (.clk(clk), .order(order), .shift_in(dec_sv[6]), .load(dec_t[7]), .x(cz), .out(fz), .done(dz));
  fir_chan   fr_c (.clk(clk), .order(order), .shift_in(dec_sv[6]), .load(dec_t[7]), .x(cr), .out(fr), .done(dr));

  wire        wr    = fir_act ? dz : dec_sv[6];
  wire [15:0] out_z = fir_act ? fz : cz;
  wire [15:0] out_r = fir_act ? fr : cr;

  //---------------------------------------------------------------------------------
  // Periodic stimulus: pair j carries samples 2j (core B, the earlier one) and 2j + 1 (core A)

  integer L = 1;
  integer xt [0:2097151];
  integer pj = 0;                                   // index of the earlier sample of the next pair
  real    xmean;
  integer seed = 1;

  always @(posedge clk)
    begin
      #1;
      if (L > 1) begin                              // 0 until the first table exists (x would stick in the integrators)
        in_b = xt[pj];
        in_a = xt[(pj + 1) % L];
        pj = (pj + 2) % L;
      end
    end

  task make_table(input integer n, input integer m);
    integer s, v;
    real acc;
    begin
      L = n * m;
      acc = 0;
      for (s = 0; s < L; s = s + 1) begin
        v = $rtoi(110.0 + 90.0 * $sin(2.0 * 3.14159265358979 * 3.0 * s / L) + 0.5) + ($random(seed) % 9);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        xt[s] = v;
        acc = acc + v;
      end
      xmean = acc / L;
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

  //---------------------------------------------------------------------------------
  // Output checking

  integer errors = 0, checks = 0;
  integer nout = 0, skip = 40, M = 64;
  integer bad_per = 0, bad_zr = 0, bad_rng = 0, bad_mean = 0, n_per = 0;
  reg [15:0] ring [0:63];
  real psum, gain, exp_mean, worst_dev;
  integer pcount;
  reg chk_on = 0;

  always @(posedge clk)
    if (chk_on && wr === 1'b1)
      begin
        if (nout >= skip)
          begin
            if (out_z !== out_r) bad_zr = bad_zr + 1;
            if (out_z == 16'h0000 || out_z == 16'hFFFF || ^out_z === 1'bx) bad_rng = bad_rng + 1;
            if (nout >= skip + M)
              begin
                if (out_z !== ring[nout % M]) bad_per = bad_per + 1;
                n_per = n_per + 1;
              end
            psum = psum + out_z;
            pcount = pcount + 1;
            if (pcount == M)
              begin
                if ((psum / M - exp_mean) > 1.0 + 0.001 * exp_mean || (exp_mean - psum / M) > 1.0 + 0.001 * exp_mean)
                  bad_mean = bad_mean + 1;
                if ((psum / M - exp_mean) > worst_dev || (exp_mean - psum / M) > worst_dev)
                  worst_dev = (psum / M > exp_mean) ? psum / M - exp_mean : exp_mean - psum / M;
                psum = 0;
                pcount = 0;
              end
          end
        ring[nout % M] = out_z;
        nout = nout + 1;
      end

  task check(input cond, input [8*100-1:0] what);
    begin
      checks = checks + 1;
      if (!cond) begin
        errors = errors + 1;
        $display("FAIL  %0s", what);
      end
    end
  endtask

  // random 48-bit state in the r copy: integrators, comb delays and the FIR delay line
  task scramble;
    integer i;
    begin
      // i1 starts just below 2^48 so that it wraps within about 1e6 write clocks, inside the checked part
      dr_c.i1 = 48'd0 - {20'd0, $random(seed) & 28'hFFFFFFF}; dr_c.i2 = {$random(seed), $random(seed)};
      dr_c.i3 = {$random(seed), $random(seed)};
      dr_c.d0 = {$random(seed), $random(seed)}; dr_c.d0p = {$random(seed), $random(seed)};
      dr_c.c1 = {$random(seed), $random(seed)}; dr_c.c1p = {$random(seed), $random(seed)};
      dr_c.c2 = {$random(seed), $random(seed)}; dr_c.c2p = {$random(seed), $random(seed)};
      dr_c.c3 = {$random(seed), $random(seed)};
      for (i = 0; i < 15; i = i + 1) fr_c.dl[i] = $random(seed);
    end
  endtask

  integer n_div, m_eff, t0;
  task run(input [1:0] k, input integer d, input fir, input integer outputs, input integer m);
    begin
      chk_on = 0;
      @(posedge clk); #2;
      order = k; divider = d; fir_act = fir;
      n_div = 2 * ((d / 2) + 1);
      m_eff = fir ? n_div / 2 : n_div;
      shift = fir ? shift_for(k, m_eff) + 1 : shift_for(k, m_eff);
      M = m;
      make_table(n_div, M);
      pj = 0;
      gain = 1.0;
      for (t0 = 0; t0 < k; t0 = t0 + 1) gain = gain * m_eff;
      gain = gain / (2.0 ** shift);
      exp_mean = xmean * gain;
      scramble;
      nout = 0; bad_per = 0; bad_zr = 0; bad_rng = 0; bad_mean = 0; n_per = 0;
      psum = 0; pcount = 0; worst_dev = 0;
      chk_on = 1;
      wait (nout >= outputs && nout >= skip + 3 * M);
      chk_on = 0;
      $display("long K %0d N %0d %0s shift %0d: %0d outputs, %0d period compares, mismatches period %0d zero/random %0d range %0d mean %0d (worst mean deviation %.2f LSB16 of %.1f)",
               k, n_div, fir ? "FIR" : "CIC", shift, nout, n_per, bad_per, bad_zr, bad_rng, bad_mean, worst_dev, exp_mean);
      check(bad_per == 0 && bad_zr == 0 && bad_rng == 0 && bad_mean == 0 && n_per > 0,
            "long run: periodic, wrap-independent, bounded, mean = input mean * gain");
    end
  endtask

  integer scale;
  initial begin
    if (!$value$plusargs("seed=%d", seed)) seed = 1;
    if (!$value$plusargs("scale=%d", scale)) scale = 100;     // percent of the full output counts
    #100;
    // output counts at scale 100 (about 1.8e7 write clocks, a few minutes)
    run(2'd3, 199,   1'b0, 1000 * scale, 64);   // the bench case: CIC3 at 1 MSa/s (0x1534 path), 100k outputs
    run(2'd3, 199,   1'b1, 200 * scale,  64);   // same with the FIR
    run(2'd2, 15,    1'b1, 1000 * scale, 64);   // FIR at its minimum N = 16
    run(2'd1, 19,    1'b1, 1000 * scale, 64);
    run(2'd1, 1,     1'b0, 1000 * scale, 64);   // N = 2
    run(2'd2, 9,     1'b0, 1000 * scale, 64);
    run(2'd3, 9999,  1'b0, 3 * scale,    16);   // largest exact CIC3 (N = 10000)
    run(2'd3, 20667, 1'b1, 2 * scale,    8);    // largest exact FIR K = 3 (N/2 = 10334)
    $display("RESULT: %0d checks, %0d failed -> %0s", checks, errors, errors ? "FAIL" : "PASS");
    $finish;
  end

endmodule
