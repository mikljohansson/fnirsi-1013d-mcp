# Calibration v4: background interleave-mismatch tracking (fw-cal3, fw 0.2.50, protocol 1.14)

Status: design, 2026-10-19. Implementation: `firmware/scope/acal_track.c/.h` (pure C, host-tested), hooks in
`fpga_control.c`; wire: docs/protocol.md section 14 row 53.

## 1. Why

Bench facts (docs/hardware-status.md 2026-10-19 04:00, 08:15, 08:30, and the coordinator's shorted-input run the same
morning):

- Gated class (legacy divider >= 1): per-core zeros within 0.3 code over 9 hours, gains within 0.4 %. A one-time
  calibration holds.
- Full-rate class (divider 0 and every filtered capture; both cores at 100 MHz, interleaved): the core A/B gain ratio
  was 1.033 one evening, 1.013 at the next morning's gain step and 1.000 fifteen minutes later; the A/B zero split moved
  1.1 code within 15 minutes of calibration, and CH2 core A stepped 1.7 codes once overnight after 4.4 flat hours.
- Shorted inputs the same morning: the stored full-rate zeros were exact (corrected A - B within 0.2 code for 10
  minutes; raw split 3.8-4.3 vs stored 3.9), while at -1.19 V and +1.87 V (about -35 / +53 codes) the raw split was 5.0
  at both levels. So that morning's deviation was **even** in the signal (a bow of about 1 code at 35-50 codes from
  the centre); the evening before it was **odd** (gain-like). Source impedance and the MMU were ruled out.

Symptom: in 200 MSa/s traces the A/B difference alternates sample by sample: a spur at fs/2 (100 MHz) and a 1-2 code
split. Where it matters: **only 8-bit full-rate records** (legacy 200 MSa/s, divider 0). Filtered (16-bit) records
weigh both cores equally for even N, so the split cancels there exactly; the gated class is stable. The tracker
therefore estimates and applies the split in the full-rate class only and leaves the common mode (mean of the cores)
alone.

## 2. Estimator

Per channel, from every usable 8-bit full-rate record (UI or host capture, after the v3 correction). Trace positions
alternate A (odd) and B (even), 5 ns apart. Let s be the corrected code and x = s - 128 (the ADC's mid-scale; the INL
bow is a property of the ADC code, not of 0 V).

**Pairs.** For each interior sample s_n the other core's neighbours give i_n = (s_{n-1} + s_{n+1}) / 2, and
y_n = sigma_n (s_n - i_n), sigma = +1 when s_n is core A, -1 when B, estimates delta(x) = A(x) - B(x) at
x_n = (s_n + i_n) / 2 - 128. Using both directions (A against interpolated B and B against interpolated A) cancels the
interpolation's own error: for a sine of frequency f, i = x cos(2 pi f T); A - cos B and -(B - cos A) carry the same
error with opposite sign, so it drops out of the fit to first order for any band-limited signal.

**Usable pairs**: all three samples within 4..251 (rails and the rail rule excluded) and |s_{n-1} - s_{n+1}| <= 6
codes (the other core's neighbours agree: the local slope is small). This keeps the plateaus of a square, the slow
parts of a sine and a quiet input, and drops edges and anything near fs/2. **Usable record**: at least 256 usable
pairs and at most 1/8 of the in-range pairs dropped for slope (energy near fs/2 shows up as many slope drops). y is
clamped to +-16 codes.

**Model and fit.** delta(x) = d + e x + q x^2 (offset, gain, even-order bow). Per record the firmware sums n, Sx, Sx^2,
Sx^3, Sx^4, Sy, Sxy, Sx^2y (int64) and adds them to moments with exponential forgetting, M <- M - M / 256 + S. The
records were corrected with the tracker's own applied (d, e), so y is moved back to the open-loop domain first (per
pair, y + d_applied + e_applied x): all records share one domain and the fit is not a feedback loop. The 3-term
least-squares solution comes from the normalised moments (m_k = M_k / M_0, t_k = sum x^k y / M_0) with orthogonal
polynomials, in integer arithmetic (no float):

    mu = m1,  v = m2 - mu^2                                         spread of x (codes^2)
    beta = (m3 - m2 mu) / v,  w = (m4 - m2^2) - beta (m3 - m2 mu)    norm of the quadratic part
    q = (t2 - beta t1 - (m2 - beta mu) t0) / w
    e = (t1 - mu t0 - q (m3 - mu m2)) / v
    d = t0 - q m2 - e mu

**Identifiability** (what the accumulated signal allows):
- q only when w >= 256 codes^4 (x visits at least three separated levels: a sine of +-30 codes, or a quiet input
  followed by a square within a few tau); else q is held at its last value (start 0).
- e only when v >= 64 codes^2 (spread >= 8 codes RMS); else held. A quiet input (generator off, probes shorted) tracks
  the offset only.
- d always, with the held q and e.
With q in the model the bow **does not bias e**: a two-level signal at -35 / +53 codes gives an unbiased e once q is
known; a two-term fit would be off by q (x1 + x2) = 1.0 % for a 1-code bow at 42 codes.

**Applied** per core, symmetric about the mean, in the full-rate class only: A' = A - (d + e x) / 2,
B' = B + (d + e x) / 2, folded into the readout's gain_k / off_k (no per-sample cost): gain_k' = gain_k (1 -+ e/2),
off_k' = off_k (1 -+ e/2) -+ 65536 (d - 128 e) / 2, clip limits recomputed. The mean of the two cores is unchanged,
so the 16-bit path (corr16, gain16) keeps its values by construction (gain16 would move by e^2 / 4: 0.02 % at
e = 3 %); the spectrum view's 8-bit FFT reads the corrected trace and gets the fix. The bow q is **not applied** (it
needs a per-sample square or a table per core; section 6).

## 3. Numbers

Record: 3000 samples per channel, about 2990 pairs, effectively 1500 independent (neighbouring pairs share samples).
Per-sample noise 0.6-1.3 code RMS (bench) plus the dither's 0.29: sigma_s about 1.05, sigma_y = sigma_s sqrt(1.5),
about 1.3 code.

- Per record: sigma_d = 1.3 / sqrt(1500) = 0.034 code; sigma_e = 1.3 / (sqrt(1500) sigma_x) = 1.2e-3 for a +-40 code
  sine (sigma_x 28).
- Forgetting 1/256 per accepted record, at most one record per channel every 50 ms (20 per second; the UI makes about
  60 per second at 200 MSa/s, pass_ms 15): **time constant tau = 256 records = 12.8 s** (longer when records come
  slower, e.g. host single captures). Noise floor of the estimate (EMA variance sigma^2 / 511): d 0.0015 code, e 5e-5.
- Convergence: a 1.7-code step falls under 0.1 code after ln(17) tau = 2.8 tau = **36 s**; a 3 % gain step under
  0.1 code at +-40 codes (e < 0.0025) after ln(12) tau = 2.5 tau = **32 s**. Inside a minute of 200 MSa/s use in both
  cases.
- Cost: about 15 instructions per pair on cached data (three loads, two compares, the 64-bit sums): about 50 us per
  record at 600 MHz, under 0.5 % of a 15 ms pass; the solve once per accepted record (a few 64-bit divisions).
- Residual a 1-code bow leaves (q = 1/42^2 = 5.7e-4 per code^2, not applied): q x^2 = 0 at mid-scale, 0.25 code at
  +-21, 1 code at +-42 codes. For a sine of amplitude a codes about mid-scale: an fs/2 tone of q a^2 / 2 (0.5 code at
  a = 42) plus tones at fs/2 +- 2f of q a^2 / 4 each. A quiet input and small signals come out exact, and d and e are
  not biased by it.

## 4. When it does not run

- Gated class, 16-bit (filtered) records, the calibration's scratch channel, records with fewer than 256 usable pairs
  or too many slope drops: no update (the age keeps growing, the applied values stay).
- No v3 block bound to the channel: nothing applied, nothing tracked.
- Reset to zero (the stored v3 values alone) at boot and whenever the calibration changes (wizard Done). Not
  persisted: a fresh start converges within a minute of 200 MSa/s use.
- Inverted channels are un-inverted for the fit (s -> 255 - s).
- A host re-read of the same record (READ_TRACE twice) can count twice; the 50 ms gate limits it, and it only adds a
  duplicate of a valid record.

## 5. Readout

GET_CAL grows from 256 to 280 bytes (protocol 1.14, row 53): flags, d (1/256 code), e (Q24), q (Q24 per code^2),
age (ms since the last accepted record), accepted-record count, the last record's usable pairs, spread of the
accumulated x. The diagnostics view shows the tracked d, e, q and age per channel.

## 6. Decisions

- **The one-time gain step stays (recommended).** The tracker sees only the A/B difference of 8-bit full-rate
  records. It cannot see (a) the full-rate class gain against the gated class (the mean of both cores: 1.1 % on the
  bench, which every filtered capture reads with) nor (b) the gated-class gains. Both are stable and come only from
  the step. For the full-rate A/B split the step's value is the tracker's starting point. The wizard keeps Skip for it.
- Bow correction (q applied) is left for later: a 256-entry Q16 table per core built once per readout (512 entries,
  a few us) would replace the readout's multiply-add and apply any polynomial at no per-sample cost; worth it if the
  bench shows a bow of a code or more persisting. The tracked q in GET_CAL decides it.
- Offset at mid-scale (x = 0), not at 0 V: bow and offset are properties of the ADC code. With dcoffset 0 the two
  coincide (T = 128).

## 7. Tests

`host_tests/test_acal_track.c`: a drifting two-core model (per-core offset, gain, bow, Gaussian noise; the v3
correction with stale stored values, the readout's dither, clamp and rail rule) fed with a quiet input, a +-40 code
sine at 1 MHz, the post square (-35 / +53) and mixes. Checks: a 1.7-code step and a 3 % gain drift converge below
0.1 code (A - B split and fs/2 amplitude) within 3 tau; the bow does not bias e; a quiet input holds e; a 40 MHz signal
is rejected; the noise floor; the 16-bit numbers unchanged.

Bench (owner): firmware/README.md section 6.4.
