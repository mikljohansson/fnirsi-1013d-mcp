//-------------------------------------------------------------------------------------
//                           Code for FNIRSI 1013D oscilloscope
//                                      Atlan and PECO
//=====================================================================================
// fnirsi-1013d-mcp changes (version word 0x1538, see fpga/al3/README.md "Changes"):
//  - 0x1533: external trigger input on the calibration pin P133 (o_1khz_calib). Command 0x15
//    value 2 selects EXT; 0 = CH1 and 1 = CH2 are unchanged. Edge from command 0x16.
//    Read command 0x28: bit0 = live EXT pin level, bit1 = generator driving the pin,
//    bit2 = EXT edge seen since the last sampling reset (command 0x01).
//  - 0x1534: decimation filter (command 0x53 order K, 0x54 output shift, 0x55 16-bit trigger
//    level, read 0x29 geometry). K = 0 is the 0x1533 sample skipping, unchanged. K = 1..3 run
//    the ADCs at 200 MSa/s and store a CIC of order K (K = 1 is a boxcar sum) decimated by
//    N = 2 * (floor(divider / 2) + 1) as 16-bit samples, one per channel per memory word.
//  - 0x1535: CIC compensation FIR (command 0x56 bit 0, read-back in 0x29 byte 0 bits 6/5).
//    For K >= 1 and N >= 16 with N a multiple of 4 the CIC decimates by N / 2 and a 15-tap
//    symmetric FIR decimates by 2 (flat to fo/4). Otherwise, and with 0x56 = 0, it is 0x1534.
//  - 0x1536: segmented capture and hardware averaging (commands 0x57-0x5D, reads 0x2A-0x32,
//    docs/plans/segmented-capture.md). A sequencer restarts the unchanged capture logic after
//    each record: segment mode stores each record in its own slot of the sample memory and logs
//    a trigger offset and a timestamp per segment; averaging mode adds each triggered record into
//    a 2048 x 64 accumulator. With 0x57 = 0 (power-on) the design is cycle-identical to 0x1535.
//  - 0x1537: the pretrigger (0x0B) and the segment slot mask (0x57/0x59) also load on the first
//    write clock edge after the release of 0x01, so values written just before the release apply
//    at slow rates too (before, only write clock edges during the reset loaded them). The ADC encode
//    toggles follow the write clock, so the A/B order within a word can no longer flip on a divider
//    or 0x53 write. Otherwise cycle-identical to 0x1536.
//  - 0x1538: the encode flops of 0x1537 loaded the write clock's copy 1.25 ns after it toggled
//    (negedge clk_200MHz to negedge clk_ADC), a path that missed timing for the A encode, so at
//    200 MSa/s the A cores sampled late, with B, or with a metastable clock. Now the next write
//    clock value is precomputed (registered enable look-ahead) on the rising clk_200MHz edge
//    and the encode flops load it with no enable (3.75 ns paths): encB = write clock, encA = its
//    complement, 1.25 ns later, at every rate and after any write (0x1535's normal state). Read
//    0x33 reports a sticky encode self-check. Otherwise cycle-identical to 0x1537.
//-------------------------------------------------------------------------------------

//A simple clock divider module for which the width and the last count can be set

module clockdivider
(
  input  wire clk_in,
  output reg clk_out = 0
);
parameter WIDTH = 32;
parameter LAST  = (2 ** WIDTH) - 1;

reg [WIDTH-1:0] count = 0;

  always @(posedge clk_in)
    begin
      if(count == LAST)
        count <= 0;
      else
        count <= count + 1;
    end

  always @(posedge clk_in)
    begin
      if(count == LAST)
        clk_out <= ~clk_out;
    end

endmodule

//-------------------------------------------------------------------------------------
//A 16 bit pulse width counter module with the pulse width setting as an input

module pwmcounter
(
  input wire clk,

  input wire [15:0] pulsewidth,

  output reg pwm = 0
);
parameter LAST = 65535;

reg [15:0] count = 0;

  always @(posedge clk)
    begin
      if(count == LAST)
        count <= 0;
      else
        begin
          count <= count + 1;

          pwm <= count < pulsewidth;
        end
    end

endmodule

//-------------------------------------------------------------------------------------

module freq_generator_dds_pwm(
    input  wire        clk_in,         // 50 MHz
    input  wire [31:0] phase_inc,      // DDS krok vypočítaný v uP
    input  wire [31:0] duty_threshold, // duty v rozsahu 0..2^32-1 vypočítaný v uP
    output reg         clk_out
);

    reg [31:0] phase_acc = 0;

    always @(posedge clk_in) begin
        phase_acc <= phase_acc + phase_inc;
        clk_out   <= (phase_acc < duty_threshold);
    end

endmodule

//-------------------------------------------------------------------------------------
//Decimation filter for one channel (0x1534), clocked by the full-rate sample write clock
//(100 MHz, one sample pair = cores A and B per clock).
//A CIC of order K (1 to 3) decimating by N = 2P is computed as the polyphase prefilter
//(1 + z^-1)^K at 200 MSa/s, evaluated once per pair, followed by a CIC of order K that
//decimates the 100 MSa/s pair stream by P (noble identity). K = 1 is the boxcar sum of
//N consecutive samples. Gain N^K, output = min(65535, y >> shift).
//Integrators and combs are 48-bit modulo arithmetic; the output is exact while
//255 * N^K < 2^48. Nothing is reset: the filter runs continuously across captures.
//sv[0] .. sv[5] is the shared valid chain: sv[0] samples the last integrator once per P pairs.
//For K < 3 the unused integrator and comb stages are plain registers, so the pipeline (and
//the latency, 12 write clocks from the pin capture to the memory write) is the same for all K.
//Each core's pins are captured on the rising edge of the write clock (pa/pb, IOB registers) or,
//with neg_a / neg_b, on the falling edge half a write clock later (na/nb), for the bench to
//centre the capture in each core's data-valid window.

module decim_chan
(
  input  wire        clk,
  input  wire [7:0]  in_a,        //ADC core A
  input  wire [7:0]  in_b,        //ADC core B
  input  wire [1:0]  order,       //K, 1..3 (0 behaves as 1; its output is not used)
  input  wire        swap,        //0: core B is the earlier sample of a pair, 1: core A
  input  wire        neg_a,       //1: capture core A on the falling edge of clk
  input  wire        neg_b,       //1: capture core B on the falling edge of clk
  input  wire [5:0]  shift,
  input  wire [5:0]  sv,          //valid chain s0 .. s5
  output reg  [15:0] out = 0
);
localparam W = 48;

reg [7:0] pa = 0, pb = 0;         //pins on the rising edge
reg [7:0] na = 0, nb = 0;         //pins on the falling edge (half a clock after pa/pb)
reg [7:0] qa = 0, qb = 0;         //captured pair
reg [7:0] pe = 0, pl = 0;         //previous pair (earlier, later)
reg [10:0] f = 0;                 //prefilter output, at most 8 * 255
reg [W-1:0] i1 = 0, i2 = 0, i3 = 0;
reg [W-1:0] d0 = 0, d0p = 0, c1 = 0, c1p = 0, c2 = 0, c2p = 0, c3 = 0;
reg [W-1:0] sh = 0;

wire [7:0] xe = swap ? qa : qb;   //earlier sample of the pair
wire [7:0] xl = swap ? qb : qa;   //later sample of the pair

wire k2  = (order == 2'd2);
wire k3  = (order == 2'd3);
wire k23 = order[1];              //K = 2 or 3

//(1+z^-1)^1: xl + xe; ^2: xl + 2 xe + pl; ^3: xl + 3 xe + 3 pl + pe
wire [9:0] t_xe = k3 ? ({2'b0, xe} + {1'b0, xe, 1'b0}) : k2 ? {1'b0, xe, 1'b0} : {2'b0, xe};
wire [9:0] t_pl = k3 ? ({2'b0, pl} + {1'b0, pl, 1'b0}) : k2 ? {2'b0, pl} : 10'd0;
wire [7:0] t_pe = k3 ? pe : 8'd0;

wire [W-1:0] fine = sh >> shift[2:0];

  always @(negedge clk)
    begin
      na <= in_a;
      nb <= in_b;
    end

  always @(posedge clk)
    begin
      pa <= in_a;
      pb <= in_b;
      qa <= neg_a ? na : pa;
      qb <= neg_b ? nb : pb;
      pe <= xe;
      pl <= xl;
      f  <= {3'b0, xl} + {1'b0, t_xe} + {1'b0, t_pl} + {3'b0, t_pe};

      i1 <= i1 + f;
      i2 <= (k23 ? i2 : {W{1'b0}}) + i1;    //K = 1: i2 = i1 one clock later
      i3 <= (k3  ? i3 : {W{1'b0}}) + i2;    //K < 3: i3 = i2 one clock later

      if(sv[0])
        d0 <= i3;

      if(sv[1])
        begin
          c1  <= d0 - d0p;
          d0p <= d0;
        end

      if(sv[2])
        begin
          c2  <= c1 - (k23 ? c1p : {W{1'b0}});
          c1p <= c1;
        end

      if(sv[3])
        begin
          c3  <= c2 - (k3 ? c2p : {W{1'b0}});
          c2p <= c2;
        end

      if(sv[4])
        sh <= c3 >> {shift[5:3], 3'b000};

      if(sv[5])
        out <= (|fine[W-1:16]) ? 16'hFFFF : fine[15:0];
    end

endmodule

//-------------------------------------------------------------------------------------
//CIC compensation FIR for one channel (0x1535), clocked by the full-rate sample write clock.
//With the FIR on, decim_chan decimates by N/2 and delivers 16-bit CIC samples at 2 fo; this
//15-tap symmetric FIR (linear phase, DC gain 2^17) decimates them by 2. Taps per K from
//fpga/al3/tb/fir_design.py (coeffs K 15 0.5): flat to fo/4, alias rejection about 17 dB per
//CIC order. One M18x18 per channel, time-multiplexed: the symmetric pre-add leaves 8
//multiply-accumulates per output, one per write clock, and there are P >= 8 write clocks per
//output.
//  shift_in: a new CIC sample is on x (decim_chan out); the 15-sample delay line shifts.
//  load:     the clock after the shift of the sample that ends an output pair; the 8 pre-added
//            sums are copied into the ps shift register and the MAC sequence starts.
//  Pipeline: load, 8 x (ps head and tap into the multiplier input registers), product register,
//            accumulator, output = clamp(0 .. 65535, (acc + 2^16) >> 17); done marks the clock in
//            which out holds the new sample (the memory write is on the next write clock edge).
//Nothing is reset; the delay line runs continuously like the CIC.

module fir_chan
(
  input  wire        clk,
  input  wire [1:0]  order,       //K selects the tap set (0 behaves as 1; its output is not used)
  input  wire        shift_in,
  input  wire        load,
  input  wire [15:0] x,
  output reg  [15:0] out = 0,
  output reg         done = 0
);

reg [15:0] dl [0:14];             //delay line, dl[0] newest
reg [16:0] ps [0:7];              //pre-added sums dl[i] + dl[14 - i], ps[7] = centre tap
reg [3:0]  cnt = 0;               //MAC steps left (8 .. 1)
reg [2:0]  ti = 0;                //tap index of the next MAC step
reg signed [17:0] ma = 0, mb = 0; //multiplier input registers
reg signed [35:0] prod = 0;       //multiplier output register
reg signed [36:0] acc = 0;
reg m0v = 0, m0f = 0, m0l = 0;    //valid / first / last tags at the multiplier input
reg m1v = 0, m1f = 0, m1l = 0;    //tags at the product register
reg m2l = 0;                      //the accumulator holds a complete sum

integer i;
initial
  begin
    for(i = 0; i < 15; i = i + 1) dl[i] = 0;
    for(i = 0; i < 8; i = i + 1) ps[i] = 0;
  end

//Taps h[0..7] of the symmetric 15-tap set (h[14 - i] = h[i]), 18-bit signed, sum 2^17
function signed [17:0] tap(input [1:0] k, input [2:0] t);
  begin
    case({ (k == 2'd3), (k == 2'd2), t })
      //K = 3
      5'b10_000: tap = -18'sd381;
      5'b10_001: tap =  18'sd332;
      5'b10_010: tap =  18'sd2919;
      5'b10_011: tap = -18'sd766;
      5'b10_100: tap = -18'sd11452;
      5'b10_101: tap = -18'sd1289;
      5'b10_110: tap =  18'sd41675;
      5'b10_111: tap =  18'sd68996;
      //K = 2
      5'b01_000: tap = -18'sd231;
      5'b01_001: tap =  18'sd504;
      5'b01_010: tap =  18'sd2397;
      5'b01_011: tap = -18'sd1457;
      5'b01_100: tap = -18'sd10255;
      5'b01_101: tap =  18'sd877;
      5'b01_110: tap =  18'sd40852;
      5'b01_111: tap =  18'sd65698;
      //K = 1 (and 0)
      5'b00_000: tap = -18'sd82;
      5'b00_001: tap =  18'sd682;
      5'b00_010: tap =  18'sd1890;
      5'b00_011: tap = -18'sd2151;
      5'b00_100: tap = -18'sd9088;
      5'b00_101: tap =  18'sd3022;
      5'b00_110: tap =  18'sd40046;
      default:   tap =  18'sd62434;
    endcase
  end
endfunction

  always @(posedge clk)
    begin
      if(shift_in)
        begin
          dl[0] <= x;
          for(i = 1; i < 15; i = i + 1)
            dl[i] <= dl[i - 1];
        end

      //MAC step: the multiplier input registers take the ps head and its tap
      ma  <= { 1'b0, ps[0] };
      mb  <= tap(order, ti);
      m0v <= (cnt != 0);
      m0f <= (cnt == 4'd8);
      m0l <= (cnt == 4'd1);

      if(load)
        begin
          for(i = 0; i < 7; i = i + 1)
            ps[i] <= { 1'b0, dl[i] } + { 1'b0, dl[14 - i] };
          ps[7] <= { 1'b0, dl[7] };
          cnt   <= 4'd8;
          ti    <= 3'd0;
        end
      else
        begin
          for(i = 0; i < 7; i = i + 1)
            ps[i] <= ps[i + 1];
          ps[7] <= 17'd0;
          if(cnt != 0)
            begin
              cnt <= cnt - 1;
              ti  <= ti + 1;
            end
        end

      //Product register
      prod <= ma * mb;
      m1v  <= m0v;
      m1f  <= m0f;
      m1l  <= m0v & m0l;

      //Accumulator, started with the rounding constant 2^16
      if(m1v)
        acc <= (m1f ? 37'sd65536 : acc) + prod;
      m2l <= m1l;

      //Output: (acc + 2^16) >> 17, clamped to 0 .. 65535
      if(m2l)
        out <= acc[36] ? 16'd0 : (|acc[35:33]) ? 16'hFFFF : acc[32:17];
      done <= m2l;
    end

endmodule

//-------------------------------------------------------------------------------------
//Sequencer RAMs (0x1536), TD logic block RAM macros as the IP generator writes them.
//Accumulator / segment table: 2048 x 64, written and read on clk_200MHz, read output registered.
//Record mirror: 2048 x 32 copy of the sample memory's write side (written on the write clock),
//read on clk_200MHz by the averaging post-pass, read output registered.

module seq_acc_ram
(
  input  wire        clk,
  input  wire        we,
  input  wire [10:0] wa,
  input  wire [63:0] wd,
  input  wire [10:0] ra,
  output wire [63:0] rd
);
  AL_LOGIC_BRAM #( .DATA_WIDTH_A(64),
        .DATA_WIDTH_B(64),
        .ADDR_WIDTH_A(11),
        .ADDR_WIDTH_B(11),
        .DATA_DEPTH_A(2048),
        .DATA_DEPTH_B(2048),
        .MODE("PDPW"),
        .REGMODE_A("NOREG"),
        .REGMODE_B("OUTREG"),
        .WRITEMODE_A("NORMAL"),
        .WRITEMODE_B("NORMAL"),
        .RESETMODE("SYNC"),
        .IMPLEMENT("9K(FAST)"),
        .INIT_FILE("NONE"),
        .FILL_ALL("NONE"))
      inst(
        .dia(wd),
        .dib({64{1'b0}}),
        .addra(wa),
        .addrb(ra),
        .cea(we),
        .ceb(1'b1),
        .ocea(1'b0),
        .oceb(1'b1),
        .clka(clk),
        .clkb(clk),
        .wea(1'b1),
        .bea(1'b0),
        .web(1'b0),
        .beb(1'b0),
        .rsta(1'b0),
        .rstb(1'b0),
        .doa(),
        .dob(rd));
endmodule

module seq_mirror_ram
(
  input  wire        clka,
  input  wire        cea,
  input  wire [10:0] wa,
  input  wire [31:0] wd,
  input  wire        clkb,
  input  wire [10:0] ra,
  output wire [31:0] rd
);
  AL_LOGIC_BRAM #( .DATA_WIDTH_A(32),
        .DATA_WIDTH_B(32),
        .ADDR_WIDTH_A(11),
        .ADDR_WIDTH_B(11),
        .DATA_DEPTH_A(2048),
        .DATA_DEPTH_B(2048),
        .MODE("PDPW"),
        .REGMODE_A("NOREG"),
        .REGMODE_B("OUTREG"),
        .WRITEMODE_A("NORMAL"),
        .WRITEMODE_B("NORMAL"),
        .RESETMODE("SYNC"),
        .IMPLEMENT("32K"),
        .INIT_FILE("NONE"),
        .FILL_ALL("NONE"))
      inst(
        .dia(wd),
        .dib({32{1'b0}}),
        .addra(wa),
        .addrb(ra),
        .cea(cea),
        .ceb(1'b1),
        .ocea(1'b0),
        .oceb(1'b1),
        .clka(clka),
        .clkb(clkb),
        .wea(1'b1),
        .bea(1'b0),
        .web(1'b0),
        .beb(1'b0),
        .rsta(1'b0),
        .rstb(1'b0),
        .doa(),
        .dob(rd));
endmodule

//-------------------------------------------------------------------------------------
//Not implemented in this version is the I2C interface part.

module fnirsi_1013D
(
  //Input signals
  input wire       i_xtal,       //50 MHz clock
  input wire       i_mcu_clk,    //Active low going pulse from the mcu to clock data
  input wire       i_mcu_rws,    //Read 0 / write 1
  input wire       i_mcu_dcs,    //Data 0 / command 1
  input wire [7:0] i_adc1A_d,    //ADC 1 databus
  input wire [7:0] i_adc1B_d,        
  input wire [7:0] i_adc2A_d,    //ADC 2 databus        
  input wire [7:0] i_adc2B_d,        

  //Bi-directional parallel data bus to / from the mcu        
  inout wire [7:0] io_mcu_data,
  
  //Bi-directional pin: to FPGA ext trigger / from FPGA gen output
  inout wire o_1khz_calib,

  //I2C port
  //inout wire io_i2c_sda,

  //Outputs
  output wire o_adc1_encA,       //ADC clock outputs   
  output wire o_adc1_encB,
  output wire o_adc2_encA,        
  output wire o_adc2_encB,        
  output wire o_offset_1,        //DC offset channnel 1
  output wire o_relay1_1,        //Relay channel 1
  output wire o_relay1_2,        
  output wire o_relay1_3,
  output wire o_ac_dc_1,         //AC / DC select channel 1        
  output wire o_offset_2,        //DC offset channnel 2
  output wire o_relay2_1,        //Relay channel 2
  output wire o_relay2_2,        
  output wire o_relay2_3,
  output wire o_ac_dc_2,         //AC / DC select channel 2  
  output wire o_pwm_display      //Display brightness control
);

//-------------------------------------------------------------------------------------
//Wires to connect parts together

  wire clk_200MHz;
  wire clk_ADC;//
  //wire clk_ADC0;
  
  wire clk_50MHz;
  wire clk_1KHz;//
  wire clk_out;

  wire sample_clock_enable;

  wire sample_read_clock;

  wire sampling_enable;

  wire time_base_timeout;

  wire trigger_check_enable;
  wire trigger_rise_enable;
  wire trigger_fall_enable;

  wire cur_greater_equal_lev;      //Signal active when the current sample is less then or equal to the set trigger level
  wire prev_less_equal_lev;        //Signal active when the previous sample is less then or equal to the set trigger level

  wire [12:0] sample_read_address;
  
  wire [12:0] addr_delta;

  wire [7:0] o_adc1A_d;
  wire [7:0] o_adc1B_d;
  wire [7:0] o_adc2A_d;
  wire [7:0] o_adc2B_d;

//-------------------------------------------------------------------------------------
//Registers to store working data

  reg [7:0] mcu_command = 0;               //Current command set by the MCU

  reg [7:0] o_mcu_data = 0;                //Data set to be read by the MCU

  reg [3:0] mcu_state = 0;                 //Current state for writing or reading multiple bytes (Can be reduced to two bits)

  reg [15:0] ch1_offset = 0;               //Channel 1 DC offset setting
  reg [15:0] ch2_offset = 0;               //Channel 2 DC offset setting

  reg [15:0] display_brigthness = 0;       //Display brightness control

  reg [15:0] touch_panel_address = 0;      //Data written with command 0x3C and read with 0x41

  reg [31:0] sample_rate_divider = 0;      //For controlling the sample rate a large divider

  reg [31:0] time_base_set = 0;            //Set by the MCU a value to count up to before trigger can get valid?
  reg [31:0] time_base_cnt = 0;            //Counter running on the sample write clock to count up to the set value?

  reg [31:0] hold_on_divider = 0;          //For controlling the hold on a large divider

  //input wire       i_mcu_rws, 
  reg gen_out_enable = 1;
  reg [31:0] clk_out_divider = 85899;      //For controlling the clk out frekvency  1kHz
  reg [31:0] clk_out_duty = 32'h80000000;  //For controlling the clk out duty       50%

  reg relay1_1 = 0;                        //Relay control for channel 1
  reg relay1_2 = 0;
  reg relay1_3 = 0;
  reg ac_dc_1  = 0;

  reg relay2_1 = 0;                        //Relay control for channel 2
  reg relay2_2 = 0;
  reg relay2_3 = 0;
  reg ac_dc_2  = 0;

  reg [31:0] sample_rate_counter = 0;      //Counter for making the sample rate

  reg sample_write_clock = 0;              //Free running clock on which the samples are taken
  reg sample_write_enable = 0;             //Flag to allow writing the samples or not (Read with command 0x0A)
  reg sample_read_enable = 0;              //Flag to allow reading of the samples by the MCU. (Written with command 0x0F)
  reg sample_system_reset = 0;             //Set by the MCU to force a reset of the sampling system
  reg sample_system_state = 0;             //Flag read by the MCU to check the reset state

  reg [3:0] sample_system_reset_count;     //Delay counter for the sample system reset state to go high on a reset

  reg [12:0] sample_write_address = 0;     //Circulair address counter for storing the samples when enabled

  reg [12:0] sample_read_offset = 0;       //An address offset for the reading of the data set by the MCU command 0x1F

  reg sampling_triggered = 0;              //Set when a trigger has occured
  reg trigger_detect_enable = 0;           //Set when a falling edge trigger occurs
  reg sampling_half_way = 0;               //Set when half the memory has been filled with samples

  reg trigger_mode = 0;                    //0 == auto mode and 1 == normal mode.
  reg trigger_edge = 0;                    //0 == rising edge and 1 =  falling edge.
  reg trigger_channel = 0;                 //0 == channel 1 and 1 == channel 2.
  reg trigger_ext = 0;                     //1 == external trigger on the calibration pin (command 0x15 value 2)

  reg [7:0] trigger_level = 0;
  
  reg [7:0] trigger_data;
  
  reg [7:0] trigger_previous = 0;          //On reset set to the inverted trigger edge for detecting the trigger
  
  localparam BUFFER_BITS = 13;
  localparam BUFFER_SIZE = 1 << BUFFER_BITS;  // 8192

  reg [12:0] pretrigger_samples= 750;     // samples before trigger alias set_trigger_point
  reg [12:0] total_samples = 1500;        // total number of samples (pretrigger + posttrigger)

  reg [12:0] trigger_read_address;        //A counter that starts to count when the half way point has been passed (0x0320)

  reg [12:0] trigger_address = 0;         //The trigger read address is captured on the trigger signal and can be read on command 0x14

  //Decimation filter (0x1534)
  reg [1:0]  dec_order = 0;                //Command 0x53 bits 1..0: 0 = sample skipping (0x1533), 1 = boxcar, 2 = CIC2, 3 = CIC3
  reg        dec_swap = 0;                 //Command 0x53 bit 2: 1 = core A is the earlier sample of a pair
  reg        dec_neg_a = 0;                //Command 0x53 bit 3: capture the A cores on the falling write clock edge
  reg        dec_neg_b = 0;                //Command 0x53 bit 4: capture the B cores on the falling write clock edge
  reg [5:0]  dec_shift = 0;                //Command 0x54: output = min(65535, filter >> dec_shift)
  reg [15:0] trigger_level16 = 0;          //Command 0x55: trigger level on the 16-bit samples (K >= 1)

  reg        fir_en = 0;                   //Command 0x56 bit 0: CIC compensation FIR (0x1535)

  //Segmented capture and averaging (0x1536), written by the MCU while 0x01 = 1
  reg [1:0]  seq_mode = 0;                 //Command 0x57 bits 1..0: 0 off, 1 segments, 2 averaging, 3 off
  reg        seq_circ = 0;                 //Command 0x57 bit 2: circular segments (until stopped)
  reg        seq_raw = 0;                  //Command 0x57 bit 3: raw accumulator readout
  reg [11:0] seq_count = 12'd1;            //Command 0x58: segments, or averaging positions
  reg [3:0]  seg_m = 4'd7;                 //Command 0x59: segment slot = 2^m words
  reg [15:0] avg_n = 16'd1;                //Command 0x5A: records per position (0 = 65536)
  reg [4:0]  avg_shift = 0;                //Command 0x5B: readout shift
  reg [31:0] seq_hold = 0;                 //Command 0x5C: hold-off, 5 ns units
  reg        seq_stop = 0;                 //Command 0x5D bit 0: no further restarts (cleared by 0x01 = 1)

  //Restart of the capture logic by the sequencer: seq_rst_c is high for one write clock and acts on
  //the write clock edge at its end, like the MCU sampling reset; seq_rst_q follows one write clock
  //later and resets the clk_200MHz side (sampling_triggered, ext_pending) 7.5 ns after that edge.
  //Both are 0 while the sequencer is idle (0x57 = 0), so cap_rst = sample_system_reset.
  reg        seq_rq_s = 0;                 //Sequencer restart request, sampled on the write clock
  reg        seq_rq_d = 0;
  reg        seq_rst_q = 0;
  reg        swc_rise_q = 0;               //swc_rise one clk_200MHz cycle later: write clock signals are settled
  //0x1537: the MCU sampling reset one write clock edge later. Registers that load MCU settings while the
  //reset is on also load on the first edge after its release, so a setting written in the last write clock
  //period before the release still applies (0x1532..0x1536 kept the older value at slow rates)
  reg        ssr_d = 0;
  wire       seq_rst_c   = seq_rq_s & ~seq_rq_d;
  wire       cap_rst     = sample_system_reset | seq_rst_c;
  wire       cap_rst200  = sample_system_reset | (swc_rise_q & seq_rst_q);

  wire dec_on = (dec_order != 2'd0);

  wire [15:0] dec_out1;                    //Decimated 16-bit samples (CIC, or FIR when fir_act)
  wire [15:0] dec_out2;
  wire        swc_adv;                     //Capture logic advances on this write clock edge
  reg         swc_adv_q = 1;               //The last write clock edge advanced the capture logic (wrote a sample)

  reg adc1_encA = 1;                       //Clock signals for the ADC's (0x1538: A starts as the complement of B)
  reg adc1_encB = 0;
  reg adc2_encA = 1;
  reg adc2_encB = 0;

//-------------------------------------------------------------------------------------

  pll main_clock
  (
    .refclk   (i_xtal),
    .reset    (1'b0),
    .clk0_out (clk_200MHz),
    .clk1_out (clk_ADC)

    //.clk1_out (clk_ADC90),
    //.clk2_out (clk_ADC0)

  );

//-------------------------------------------------------------------------------------
//Clock divider for making the 50MHz clock
//The original uses 16 bits

  clockdivider #
  (
    .WIDTH (1),
    .LAST  (1)
  )
  peripheral_clock
  (
    .clk_in  (clk_200MHz),
    .clk_out (clk_50MHz)
  );

//-------------------------------------------------------------------------------------
// frequency generator

freq_generator_dds_pwm u_freq
  (
    .clk_in(clk_50MHz),
    .phase_inc(clk_out_divider),    // DDS step calculated in uP
    .duty_threshold(clk_out_duty),  // duty in the range 0..2^32-1
    .clk_out(clk_1KHz)
  );

//Control of the bidirectional bus for input and output
  assign o_1khz_calib = gen_out_enable ? clk_1KHz : 1'bZ; 

  //assign o_1khz_calib = clk_1KHz;

//-------------------------------------------------------------------------------------
//External trigger input on the calibration pin
//The input buffer of the pad is always read. With the generator off (command 0x50 = 0)
//the pad is hi-Z and an external 3.3 V logic signal can drive it. With the generator on
//the input reads back the generator's own output.
//Two flip-flop synchroniser into the clk_200MHz domain, then a 3-sample majority filter
//(pulses of one 5 ns clock are rejected, two or more pass), then the edge detect.
//The selected edge is held in ext_pending and applied to sampling_triggered right after
//the next rising edge of sample_write_clock, which is where an ADC trigger is applied too.
//So trigger_address follows the ADC convention: it is the address of the last sample that
//was written before the (filtered) EXT edge. The pad to filtered-edge latency is 4 to 5
//clk_200MHz cycles (20 to 25 ns). Pulses shorter than a sample period still trigger.

  reg [1:0] ext_sync = 0;                  //Synchroniser, ext_sync[1] is the safe one
  reg [1:0] ext_hist = 0;                  //Two older synchronised samples for the majority filter
  reg ext_level = 0;                       //Filtered EXT level
  reg ext_level_prev = 0;                  //Filtered EXT level one clock earlier
  reg ext_activity = 0;                    //An EXT edge was seen since the last sampling reset
  reg ext_pending = 0;                     //Selected EXT edge seen, waiting for the next sample write clock
  reg swc_prev = 0;                        //sample_write_clock one clk_200MHz cycle earlier

  wire ext_rise = ext_level & ~ext_level_prev;
  wire ext_fall = ~ext_level & ext_level_prev;

  //Selected edge per command 0x16 (0 = rising, 1 = falling), only when EXT is the source
  wire ext_edge = trigger_ext & (trigger_edge ? ext_fall : ext_rise);

  //Rising edge of sample_write_clock (it toggles on the falling edge of clk_200MHz)
  wire swc_rise = sample_write_clock & ~swc_prev;

  //With the decimation filter on, only after an edge that wrote an output sample
  //(swc_adv_q is constant 1 with K = 0; one LUT4 like the 0x1533 term)
  wire ext_trigger_detect = ext_pending & swc_rise & swc_adv_q;

  always @(posedge clk_200MHz)
    begin
      ext_sync       <= { ext_sync[0], o_1khz_calib };
      ext_hist       <= { ext_hist[0], ext_sync[1] };
      ext_level      <= (ext_sync[1] & ext_hist[0]) | (ext_sync[1] & ext_hist[1]) | (ext_hist[0] & ext_hist[1]);
      ext_level_prev <= ext_level;

      swc_prev       <= sample_write_clock;

      if(sample_system_reset == 1)
        ext_activity <= 0;
      else if(ext_rise | ext_fall)
        ext_activity <= 1;

      if(cap_rst200 == 1)
        ext_pending <= 0;
      else if(trigger_check_enable & ext_edge)
        ext_pending <= 1;
    end

//Clock divider for making the 1KHz calibration signal
//The original uses 32 bits and 25000 for last which gives the incorrect frequency
/*
  clockdivider #
  (
    .WIDTH (16),
    .LAST  (24999)
  )
  calibration_clock
  (
    .clk_in  (clk_50MHz),
    .clk_out (clk_1KHz)
  );

  assign o_1khz_calib = clk_1KHz;
*/
//-------------------------------------------------------------------------------------
//Channel 1 DC offset

  pwmcounter #
  (
    .LAST (1999)
  )
  channel1_dc_offset
  (
    .clk        (clk_50MHz),
    .pulsewidth (ch1_offset),
    .pwm        (o_offset_1)
  );

//-------------------------------------------------------------------------------------
//Channel 2 DC offset

  pwmcounter #
  (
    .LAST (1999)
  )
  channel2_dc_offset
  (
    .clk        (clk_50MHz),
    .pulsewidth (ch2_offset),
    .pwm        (o_offset_2)
  );

//-------------------------------------------------------------------------------------
//Display brightness control

  pwmcounter #
  (
    .LAST (59999)//2047 59999 1861
  )
  display_brightness
  (
    .clk        (clk_50MHz),
    .pulsewidth (display_brigthness),	//.pulsewidth (display_brigthness), .pulsewidth (display_brigthness >> 5),
    .pwm        (o_pwm_display)
  );

//---------------------------------------------------------------------------
//Memory part
//0x1536: in segment mode the write address is folded into the current segment slot
//(seg_mask_s = slot size - 1, seg_base_s = slot start); otherwise the mask is all ones and the
//address passes unchanged. The record mirror takes the same data and write enable.

  wire [31:0] sample_dia = dec_on ? { dec_out2, dec_out1 } : { i_adc2B_d, i_adc2A_d, i_adc1B_d, i_adc1A_d };
  reg  [12:0] seg_mask_s = 13'h1FFF;
  reg  [12:0] seg_base_s = 0;
  wire [12:0] sample_write_address_phys = (sample_write_address & seg_mask_s) | (seg_base_s & ~seg_mask_s);

  sample_memory samples
  (
    .dia   (sample_dia),
    .addra (sample_write_address_phys),
    .clka  (sample_write_clock),
    .cea   (sample_write_enable & swc_adv),

    .dob   ({ o_adc2B_d, o_adc2A_d, o_adc1B_d, o_adc1A_d }),
    .addrb (sample_read_address),
    .clkb  (sample_read_clock)
    //.ceb   (1'b1)
  );

//-------------------------------------------------------------------------------------
//Sample write clock

  //0x1538: encode generation. sce_q is sample_clock_enable one clock ahead, as a register: computed on each falling
  //clk_200MHz edge from the counter, it equals the enable of the cycle after that edge while the divider is stable.
  //swc_sh is a copy of the write clock (loaded from it, so a difference lasts one clock; it keeps the write clock's
  //fanout, and with it the half-cycle path into sampling_triggered, as in 0x1537). enc_nxt is the value the write
  //clock will have after the next falling clk_200MHz edge, made on the rising edge before it (2.5 ns path), and the
  //encode flops load it on the falling edge of clk_ADC 1.25 ns after that falling edge (3.75 ns path, no enable).
  //So encB follows the write clock and encA its complement, 1.25 ns later, at every rate, and nothing in the
  //encodes keeps state of its own: a cycle disturbed by an asynchronous 0x0D / 0x53 write (sce_q one cycle behind
  //the combinational enable) repairs itself on the next one. The write clock and the divider counter still use the
  //combinational enable, so they stay cycle-identical to 0x1537.
  //0x1537 loaded its copy swc_enc_n across the 1.25 ns window from negedge clk_200MHz to negedge clk_ADC; the A path
  //(through an inverter) missed it by 0.21 ns in TD's report, so an A encode could be right, one clock late (A
  //sampling with B) or metastable (fpga/al3/README.md "Changes" section 8).
  reg sce_q     = 0;
  reg swc_sh    = 0;                       //copy of sample_write_clock, loaded from it (a difference lasts one clock)
  reg enc_nxt   = 0;                       //sample_write_clock after the next falling clk_200MHz edge
  reg enc_nxt_n = 1;                       //its complement, a flop of its own (no LUT in front of the encA flops)

  always @(posedge clk_200MHz)
    begin
      enc_nxt   <=  (swc_sh ^ sce_q);
      enc_nxt_n <= ~(swc_sh ^ sce_q);
    end

  always @(negedge clk_ADC)	//clk_ADC   clk_RAM
    begin
      adc1_encB <= enc_nxt;
      adc2_encB <= enc_nxt;

      //The A and B clocks are inverted versions of each other
      adc1_encA <= enc_nxt_n;
      adc2_encA <= enc_nxt_n;
    end

  always @(negedge clk_200MHz)//clk_200MHz  clk_RAM
    begin
      if(sample_clock_enable == 1)
        sample_write_clock <= ~sample_write_clock;

      swc_sh <= sample_write_clock ^ sample_clock_enable;
    end

  //Encode self-check (read 0x33). enc_ref is enc_nxt taken on the falling clk_200MHz edge, i.e. the write clock this
  //edge was predicted to give. On the next falling edge the four encode flops (loaded 3.75 ns before from the same
  //enc_nxt) must equal enc_ref (B) and its complement (A), and the write clock must equal enc_ref (the prediction).
  //enc_err: a mismatch since the last 0x01 = 1 (an asynchronous divider or 0x53 write can make the prediction miss
  //once; the firmware writes those before the reset). swc_sh stands in for the write clock (equal unless a
  //disturbed clock, which the next check then also flags).
  reg       enc_ref     = 0;
  reg [3:0] enc_ok      = 4'hF;
  reg       enc_pred_ok = 1;
  reg       enc_err     = 0;

  always @(negedge clk_200MHz)
    begin
      enc_ref     <= enc_nxt;
      enc_ok      <= { adc2_encA ^ enc_ref, ~(adc2_encB ^ enc_ref), adc1_encA ^ enc_ref, ~(adc1_encB ^ enc_ref) };
      enc_pred_ok <= ~(swc_sh ^ enc_ref);

      if(sample_system_reset == 1)
        enc_err <= 0;
      else if((enc_ok != 4'hF) | ~enc_pred_ok)
        enc_err <= 1;
    end

// ---------------------------------------------------
// Sample rate divider
// ---------------------------------------------------

  always @(negedge clk_200MHz)
    begin
      if(sample_clock_enable == 1)
        sample_rate_counter <= 1;
      else
        sample_rate_counter <= sample_rate_counter + 1;
    end

  //To allow the sample clock to range from 100MHz down to very low rates the plus one is needed.
  //Without it the maximum output would be 50MHz.
  //With the decimation filter on, the ADCs and the write clock always run at full rate
  //(divider forced to 0 on the static side of the compare, off the timed counter path).
  wire [31:0] sample_rate_divider_eff = dec_on ? 32'd0 : sample_rate_divider;

  assign sample_clock_enable = sample_rate_divider_eff < (sample_rate_counter);

  //0x1538: the enable of the cycle after this edge is divider < counter after this edge, where the counter becomes
  //1 (enable now) or counter + 1, so: enable now ? divider == 0 : divider <= counter
  always @(negedge clk_200MHz)
    sce_q <= sample_clock_enable ? (sample_rate_divider_eff == 32'd0) : (sample_rate_divider_eff <= sample_rate_counter);

//-------------------------------------------------------------------------------------
//Decimation filter control and data path (0x1534)
//dec_pc counts P = floor(divider / 2) + 1 write clocks per output sample. dec_sv is the
//valid chain through the filter pipeline; dec_sv[6] (dec_wr) marks the write clock edge on
//which the output sample is written to memory and the capture logic advances (swc_adv).
//With K = 0 swc_adv is constant 1, so every capture register behaves as in 0x1533.

  reg [30:0] dec_pc = 0;
  reg [6:0]  dec_sv = 0;

  //CIC compensation FIR (0x1535). fir_act: 0x56 bit 0, K >= 1 and N = 2P a multiple of 4 with
  //N >= 16, i.e. P even and P >= 8, i.e. divider >= 14 with divider bit 1 set. The CIC then gets
  //a second strobe half way through the P count (dec_pc == fir_hp = P / 2), so it decimates by
  //N / 2, and the FIR decimates its output by 2. dec_t tags the strobes from dec_pc == 0: the
  //CIC sample they make is the newest one of an output pair. dec_wr then comes from the FIR,
  //still once per P write clocks. With fir_act = 0 every 0x1534 register and output is unchanged.
  wire        fir_ok = fir_en & dec_on & sample_rate_divider[1] & (sample_rate_divider >= 32'd14);
  reg         fir_act = 0;
  reg [30:0]  fir_hp = 0;
  reg [7:0]   dec_t = 0;
  wire [15:0] cic_out1, cic_out2, fir_out1, fir_out2;
  wire        fir_done1, fir_done2;

  wire [30:0] dec_ph = sample_rate_divider[31:1];
  wire dec_wr  = fir_act ? fir_done1 : dec_sv[6];
  assign swc_adv = ~dec_on | dec_wr;

  assign dec_out1 = fir_act ? fir_out1 : cic_out1;
  assign dec_out2 = fir_act ? fir_out2 : cic_out2;

  always @(posedge sample_write_clock)
    begin
      if((dec_pc == 0) || (dec_pc > dec_ph))
        dec_pc <= dec_ph;
      else
        dec_pc <= dec_pc - 1;

      dec_sv      <= { dec_sv[5:0], (dec_pc == 0) | (fir_act & (dec_pc == fir_hp)) };
      swc_adv_q   <= swc_adv;

      fir_act     <= fir_ok;
      fir_hp      <= dec_ph[30:1] + 31'd1;
      dec_t       <= { dec_t[6:0], (dec_pc == 0) };
    end

  decim_chan dec_ch1
  (
    .clk   (sample_write_clock),
    .in_a  (i_adc1A_d),
    .in_b  (i_adc1B_d),
    .order (dec_order),
    .swap  (dec_swap),
    .neg_a (dec_neg_a),
    .neg_b (dec_neg_b),
    .shift (dec_shift),
    .sv    (dec_sv[5:0]),
    .out   (cic_out1)
  );

  decim_chan dec_ch2
  (
    .clk   (sample_write_clock),
    .in_a  (i_adc2A_d),
    .in_b  (i_adc2B_d),
    .order (dec_order),
    .swap  (dec_swap),
    .neg_a (dec_neg_a),
    .neg_b (dec_neg_b),
    .shift (dec_shift),
    .sv    (dec_sv[5:0]),
    .out   (cic_out2)
  );

  fir_chan fir_ch1
  (
    .clk      (sample_write_clock),
    .order    (dec_order),
    .shift_in (dec_sv[6]),
    .load     (dec_t[7]),
    .x        (cic_out1),
    .out      (fir_out1),
    .done     (fir_done1)
  );

  fir_chan fir_ch2
  (
    .clk      (sample_write_clock),
    .order    (dec_order),
    .shift_in (dec_sv[6]),
    .load     (dec_t[7]),
    .x        (cic_out2),
    .out      (fir_out2),
    .done     (fir_done2)
  );

  //Trigger on the 16-bit sample that is written on this edge (channel per 0x15): the result
  //of sample >= trigger_level16 goes into the 8-bit trigger_data register as 0xFF or 0x00 and
  //the 8-bit compare runs against 0x80, so the timed trigger path keeps its 0x1533 structure.
  wire [15:0] dec_trig_sample = trigger_channel ? dec_out2 : dec_out1;
  wire        dec_trig_ge     = dec_trig_sample >= trigger_level16;
  wire [7:0]  trigger_level_eff = dec_on ? 8'h80 : trigger_level;

//-------------------------------------------------------------------------------------
//Sampling write address counter
//This counter is reset when the reset is activated by the MCU

  always @(posedge sample_write_clock)
    begin
      if(cap_rst == 1)
        sample_write_address <= 0;
      else if(swc_adv)
        sample_write_address <= sample_write_address + 1;
    end

//-------------------------------------------------------------------------------------
//Time base counter
//This counter is reset when the reset is activated by the MCU
//The oscilloscope firmware does not give correct values ​​for this type of calculation, 
//so the assignment trigger address = 750 is uncommented. 
//This is caused by the meamder when the base is running freely in auto mode without a trigger.

  always @(posedge sample_write_clock) 
    begin
      if(cap_rst == 1)
        time_base_cnt <= 0;
      else if(swc_adv)
        time_base_cnt <= time_base_cnt + 1;
    end

  assign time_base_timeout = time_base_cnt > time_base_set;


//-------------------------------------------------------------------------------------
//Sample writing enable control

  always @(posedge sample_write_clock)
    begin
      if(cap_rst == 1)
        sample_write_enable <= 1;                //Enable the writing on reset
      else if((sampling_enable == 0) && swc_adv)
        sample_write_enable <= 0;                //Disable it when enough samples have been gathered
    end

//-------------------------------------------------------------------------------------
// Sampling is stopped when a trigger has been found AND the post-trigger region is complete 
// or when in auto mode a time out occurred

// delta address (modulo 8192)
  assign  addr_delta = trigger_read_address - trigger_address;

  assign sampling_enable = ~(
    (sampling_triggered && (addr_delta >= total_samples - pretrigger_samples))//);  // post-trigger complete
         || (~trigger_mode && time_base_timeout)                                    // auto mode timeout
         				 );

// For testing purposes for trigger control
/*
assign sampling_enable = ~(
    (sampling_triggered )   // post-trigger complete
    || (~trigger_mode && time_base_timeout)                  // auto mode timeout
);
*/
  //Half way the sampling buffer cycle the check for a trigger can be done
  assign trigger_check_enable = ((~sampling_triggered & sampling_half_way));

//--------------------------------------------------------------------------------------------
//Sampling triggered detect

// Previous states
//With the decimation filter on, trigger_data holds 0xFF / 0x00 and the level is 0x80
  assign prev_less_equal_lev = trigger_previous < trigger_level_eff;
  assign cur_greater_equal_lev = trigger_data >= trigger_level_eff;
  assign cur_less_equal_lev = trigger_data < trigger_level_eff;

// Enable signals
  assign trigger_rise_enable = (~trigger_edge & prev_less_equal_lev) | trigger_detect_enable;
  assign trigger_fall_enable =  (trigger_edge & ~prev_less_equal_lev) | trigger_detect_enable;

// Rising edge: previous ≤ level, current ≥ level (ADC trigger, not when EXT is selected)
  assign trigger_rise_detect = (~trigger_ext & ~trigger_edge & prev_less_equal_lev & cur_greater_equal_lev);

// Falling edge: previous ≥ level, current ≤ level (ADC trigger, not when EXT is selected)
  assign trigger_fall_detect = (~trigger_ext & trigger_edge & ~prev_less_equal_lev & cur_less_equal_lev); 


  always @(posedge clk_200MHz) begin			//clk_RAM  sample_write_clock
    if(cap_rst200 == 1) begin
        sampling_triggered <= 0;
        trigger_detect_enable <= 0;
    end else if(trigger_check_enable == 1) begin
        sampling_triggered <= sampling_triggered | trigger_rise_detect | trigger_fall_detect | ext_trigger_detect;
        trigger_detect_enable <= trigger_rise_detect | trigger_fall_detect | ext_trigger_detect;
    end
  end

//------------------------------------------------

  always @(posedge sample_write_clock) begin	//clk_200MHz clk_RAM  sample_write_clock clk_ADC90 clk_ADC
    if (cap_rst) 
       begin
         trigger_previous <= 0;
         trigger_data     <= 0;
       end 
    else if(swc_adv)
       begin
         //sample from the previous cycle
         trigger_previous <= trigger_data;

         //current sample (decimation filter on: 16-bit compare result, see dec_trig_ge)
	     trigger_data <= dec_on ? {8{dec_trig_ge}} : trigger_channel ? i_adc2A_d : i_adc1A_d;
       end
  end

//-------------------------------------------------------------------------------------
//Sampling half way detect

  always @(posedge sample_write_clock)
    begin
      if(cap_rst == 1)
        sampling_half_way <= 0;
      else
        sampling_half_way <= swc_adv ? ((sample_write_address >= pretrigger_samples) || sampling_half_way) : sampling_half_way;
    end

//-------------------------------------------------------------------------------------
//An address counter that is preloaded and starts counting on the half way point

  always @(posedge sample_write_clock)
    begin
      ssr_d <= sample_system_reset;

      if((cap_rst == 1) || (ssr_d == 1))              //0x1537: also on the first edge after the release
        trigger_read_address <= pretrigger_samples;   //Starting point in memory
      else if((sampling_half_way == 1)&&(sampling_enable == 1)&&swc_adv)
        trigger_read_address <= trigger_read_address + 1;
    end

//-------------------------------------------------------------------------------------
//Capture of the trigger address

  always @(posedge sample_write_clock)
    begin
      if(cap_rst == 1)
        trigger_address <= 0;
        
     else if((sampling_triggered == 0) && swc_adv)
       begin
        if (~trigger_mode && time_base_timeout) trigger_address <= pretrigger_samples;
        else trigger_address <= trigger_read_address;
       end

    end

//-------------------------------------------------------------------------------------
//Segmented capture and averaging (0x1536), write clock side
//The sequencer (clk_200MHz, below) raises seq_req to restart the capture logic; the rising edge of
//its sampled copy is the one-write-clock pulse seq_rst_c (in cap_rst). In segment mode the slot
//base moves on by one slot at each restart, or back to slot 0 when the sequencer set seq_wrap.
//ts_cnt counts 5 ns units from the release of the MCU sampling reset: 2 (D + 1) per write clock
//edge, D the effective divider (0 with K >= 1). For the record that just ended, ts_cap holds
//ts_cnt from the last edge that latched trigger_address and seq_trig holds sampling_triggered as of
//the last word written. Both are taken one write clock late from registered copies (st_s, adv_d,
//swe_d, ts_d) of exactly what the trigger_address logic saw, so sampling_triggered gains one load
//only; swe_d is what the sequencer watches, so its snapshot is consistent.

  reg        seq_req = 0;                  //clk_200MHz: restart request (sequencer)
  reg        seq_wrap = 0;                 //clk_200MHz: the restart goes to slot 0
  reg        seq_wrap_s = 0;
  reg [33:0] ts_inc = 34'd2;
  reg [39:0] ts_cnt = 0;
  reg [39:0] ts_d = 0;
  reg [39:0] ts_cap = 0;
  reg        st_s = 0, adv_d = 0, swe_d = 0;
  reg        seq_trig = 0;

  wire [12:0] seg_mask_m = (seg_m >= 4'd13) ? 13'h1FFF : ~(13'h1FFF << seg_m);

  always @(posedge sample_write_clock)
    begin
      seq_rq_s   <= seq_req;
      seq_rq_d   <= seq_rq_s;
      seq_rst_q  <= seq_rst_c;
      seq_wrap_s <= seq_wrap;

      if((sample_system_reset == 1) || (ssr_d == 1))  //0x1537: also on the first edge after the release
        begin
          seg_mask_s <= (seq_mode == 2'd1) ? seg_mask_m : 13'h1FFF;
          seg_base_s <= 0;
        end
      else if(seq_rst_c)
        seg_base_s <= seq_wrap_s ? 13'd0 : seg_base_s + seg_mask_s + 13'd1;

      ts_inc <= { {1'b0, sample_rate_divider_eff} + 33'd1, 1'b0 };
      if(sample_system_reset == 1)
        ts_cnt <= 0;
      else
        ts_cnt <= ts_cnt + {6'd0, ts_inc};

      st_s  <= sampling_triggered;
      adv_d <= swc_adv;
      swe_d <= sample_write_enable;
      ts_d  <= ts_cnt;

      if(~st_s & adv_d & swe_d)
        ts_cap <= ts_d;

      if(swe_d)
        seq_trig <= st_s;
    end

//-------------------------------------------------------------------------------------
//Sequencer (0x1536), clk_200MHz
//Write clock signals are read through sn_* snapshots taken on the clk_200MHz edge 7.5 ns after a
//write clock rise (swc_rise_q): they have settled by then (multicycle 2 in zaklad.sdc) and hold
//until the next rise, so the snapshot is never taken while they change. The MCU sampling reset and
//0x5D are synchronised. The configuration (0x57-0x5C, 0x0B, 0x0C, 0x53) is latched while 0x01 = 1.
//  IDLE  modes off at the last 0x01 (0x57 = 0 or 3): nothing happens, 0x0A as before
//  WEN   a restart was requested: wait until the capture logic runs again
//  RUN   a record is in progress; it has ended when sample_write_enable falls
//  POST  averaging: add the record (from the mirror) into the accumulator, one word per clock
//  TAB   segments: write the table entry
//  NEXT  slot / position advanced; done or stop ends the run; segments: invalidate the entry of
//        the slot about to be refilled
//  HOLD  hold-off, then the restart request
//  DONE  run complete or stopped (0x0A = 1)
//For 200 MHz every decision uses registers only: increments and compares (q_*_nx, q_*_end, pp_z,
//pp_busy, pp_start) are computed every clock from values that change at most once per record.
//Post-pass pipeline: the mirror and accumulator reads issued with pp_i, BRAM latch (pp_v1),
//output register (pp_v2), pp_x / pp_acc (pp_v3), 17-bit lane sums (pp_v4), carry or saturation
//into the write register (aw_we), written on the next clock. 16-bit format: two 32-bit lanes
//(CH1, CH2), no saturation needed (65535 x 65536 < 2^32); legacy: four 16-bit lanes (CH1A, CH1B,
//CH2A, CH2B), each saturating at 65535. The first record of each position is written, not added.
//Table entries take the write port through tab_we / tab_a / tab_d one clock after TAB and NEXT.

  localparam Q_IDLE = 3'd0, Q_WEN  = 3'd1, Q_RUN  = 3'd2, Q_POST = 3'd3,
             Q_TAB  = 3'd4, Q_NEXT = 3'd5, Q_HOLD = 3'd6, Q_DONE = 3'd7;

  reg [2:0]  q_st = Q_IDLE;
  reg        q_rs1 = 0, q_rs2 = 0;         //synchronised MCU sampling reset
  reg        q_sp1 = 0, q_sp2 = 0;         //synchronised stop (0x5D)
  reg        sn_swe = 0, sn_trig = 0;      //write clock snapshots
  reg [12:0] sn_ta = 0;
  reg [39:0] sn_ts = 0;
  reg [1:0]  q_mode = 0;                   //mode of the run, 0 = none
  reg        q_seg = 0;                    //q_mode == 1, as a register (0x1537: one LUT level less in Q_RUN)
  reg        q_circ = 0, q_fmt16 = 0;
  reg        q_active = 0, q_done = 0, q_ovf = 0, q_last = 0, q_first = 1;
  reg [11:0] q_n1 = 0;                     //segments or positions - 1
  reg [16:0] q_ntgt = 0;                   //records per position
  reg [16:0] q_pass = 0, q_pass_nx = 0;    //completed passes over all positions
  reg        q_pass_end = 0;               //q_pass + 1 == q_ntgt
  reg [12:0] q_total = 0, q_pre = 0, q_mask = 0;
  reg [31:0] q_hold = 0, q_hc = 0;
  reg        q_hz = 0;
  reg [11:0] q_idx = 0, q_idx_nx = 0;      //next slot or position
  reg        q_idx_end = 0;                //q_idx == q_n1
  reg [12:0] q_base = 0, q_base_nx = 0;    //accumulator word of the position
  reg [23:0] q_cnt = 0, q_cnt_nx = 0;      //segments completed / records accumulated
  reg [15:0] q_untrig = 0, q_untrig_nx = 0;//untriggered records (saturating)

  reg        q_post = 0;                   //the accumulator read port belongs to the post-pass
  reg        pp_i = 0, pp_z = 0, pp_busy = 0;
  reg [12:0] pp_left = 0;
  reg [10:0] pp_start = 0;
  reg [10:0] pp_ma = 0, pp_aa = 0;
  reg        pp_v1 = 0, pp_v2 = 0, pp_v3 = 0, pp_v4 = 0;
  reg [10:0] pp_a1 = 0, pp_a2 = 0, pp_a3 = 0, pp_a4 = 0;
  reg [31:0] pp_x = 0;
  reg [63:0] pp_acc = 0;
  reg [16:0] pp_s0 = 0, pp_s1 = 0, pp_s2 = 0, pp_s3 = 0;

  reg        tab_we = 0;                   //table entry write request
  reg [10:0] tab_a = 0;
  reg [63:0] tab_d = 0;

  reg        aw_we = 0;                    //accumulator write port
  reg [10:0] aw_a = 0;
  reg [63:0] aw_d = 0;

  wire [31:0] mir_q;
  wire [63:0] acc_q;

  wire [15:0] pp_xa0 = q_fmt16 ? pp_x[15:0]  : { 8'd0, pp_x[7:0] };
  wire [15:0] pp_xa1 = q_fmt16 ? 16'd0       : { 8'd0, pp_x[15:8] };
  wire [15:0] pp_xa2 = q_fmt16 ? pp_x[31:16] : { 8'd0, pp_x[23:16] };
  wire [15:0] pp_xa3 = q_fmt16 ? 16'd0       : { 8'd0, pp_x[31:24] };

  function [15:0] pp_sat(input [16:0] s);
    pp_sat = s[16] ? 16'hFFFF : s[15:0];
  endfunction

  wire [15:0] pp_w1 = pp_s1[15:0] + { 15'd0, pp_s0[16] };
  wire [15:0] pp_w3 = pp_s3[15:0] + { 15'd0, pp_s2[16] };
  wire [63:0] pp_w  = q_fmt16 ? { pp_w3, pp_s2[15:0], pp_w1, pp_s0[15:0] }
                              : { pp_sat(pp_s3), pp_sat(pp_s2), pp_sat(pp_s1), pp_sat(pp_s0) };

  wire        q_mode_on = (seq_mode == 2'd1) || (seq_mode == 2'd2);

  always @(posedge clk_200MHz)
    begin
      swc_rise_q <= swc_rise;
      q_rs1 <= sample_system_reset;
      q_rs2 <= q_rs1;
      q_sp1 <= seq_stop;
      q_sp2 <= q_sp1;

      if(swc_rise_q)
        begin
          sn_swe  <= swe_d;
          sn_trig <= seq_trig;
          sn_ta   <= trigger_address;
          sn_ts   <= ts_cap;
        end

      //values the decisions below use, one clock ahead
      q_cnt_nx    <= q_cnt + 24'd1;
      q_untrig_nx <= q_untrig + { 15'd0, ~&q_untrig };
      q_idx_nx    <= q_idx + 12'd1;
      q_idx_end   <= (q_idx == q_n1);
      q_base_nx   <= q_base + q_total;
      q_pass_nx   <= q_pass + 17'd1;
      q_pass_end  <= (q_pass_nx == q_ntgt);
      pp_start    <= sn_ta[10:0] - q_pre[10:0];
      pp_busy     <= pp_i | pp_v1 | pp_v2 | pp_v3 | pp_v4 | aw_we;

      //post-pass pipeline and the write port
      pp_v1  <= pp_i;  pp_a1 <= pp_aa;
      pp_v2  <= pp_v1; pp_a2 <= pp_a1;
      pp_v3  <= pp_v2; pp_a3 <= pp_a2;
      pp_x   <= mir_q;
      pp_acc <= q_first ? 64'd0 : acc_q;
      pp_v4  <= pp_v3; pp_a4 <= pp_a3;
      pp_s0  <= { 1'b0, pp_acc[15:0] }  + { 1'b0, pp_xa0 };
      pp_s1  <= { 1'b0, pp_acc[31:16] } + { 1'b0, pp_xa1 };
      pp_s2  <= { 1'b0, pp_acc[47:32] } + { 1'b0, pp_xa2 };
      pp_s3  <= { 1'b0, pp_acc[63:48] } + { 1'b0, pp_xa3 };
      aw_we  <= pp_v4 | tab_we;
      aw_a   <= tab_we ? tab_a : pp_a4;
      aw_d   <= tab_we ? tab_d : pp_w;
      tab_we <= 0;

      if(pp_i)
        begin
          pp_ma <= pp_ma + 11'd1;
          pp_aa <= pp_aa + 11'd1;
          pp_left <= pp_left - 13'd1;
          pp_z <= (pp_left == 13'd1);
          if(pp_z)
            pp_i <= 0;
        end
      else if(~q_post)
        begin
          //0x1537: the post-pass start values are preloaded every clock while no post-pass runs (before, Q_RUN
          //loaded them on the clock that starts the pass, a 36-fanout select at the end of the state decode)
          pp_ma   <= pp_start;
          pp_aa   <= q_base[10:0];
          pp_left <= q_total - 13'd1;
          pp_z    <= (q_total == 13'd1);
        end

      if(q_rs2)
        begin
          q_st     <= q_mode_on ? Q_RUN : Q_IDLE;
          q_mode   <= q_mode_on ? seq_mode : 2'd0;
          q_seg    <= (seq_mode == 2'd1);
          q_active <= q_mode_on;
          q_circ   <= q_mode_on & seq_circ;
          q_fmt16  <= q_mode_on & dec_on;
          q_n1     <= seq_count - 12'd1;
          q_ntgt   <= (avg_n == 16'd0) ? 17'h10000 : { 1'b0, avg_n };
          q_total  <= total_samples;
          q_pre    <= pretrigger_samples;
          q_mask   <= seg_mask_m;
          q_hold   <= seq_hold;
          q_done   <= 0;
          q_ovf    <= 0;
          q_last   <= 0;
          q_first  <= 1;
          q_pass   <= 0;
          q_idx    <= 0;
          q_base   <= 0;
          q_cnt    <= 0;
          q_untrig <= 0;
          q_post   <= 0;
          pp_i     <= 0;
          seq_req  <= 0;
          seq_wrap <= 0;
        end
      else
        case(q_st)
          Q_WEN:
            if(q_sp2)
              begin
                seq_req <= 0;
                q_done  <= 1;
                q_st    <= Q_DONE;
              end
            else if(sn_swe)
              begin
                seq_req <= 0;
                q_st    <= Q_RUN;
              end

          Q_RUN:
            if(~sn_swe)
              begin
                if(q_seg)
                  q_st <= Q_TAB;
                else if(sn_trig)
                  begin
                    q_post  <= 1;
                    pp_i    <= 1;
                    q_st    <= Q_POST;
                  end
                else
                  begin
                    q_untrig <= q_untrig_nx;
                    q_st     <= Q_NEXT;
                  end
              end
            else if(q_sp2 & ~sampling_triggered)
              begin
                q_done <= 1;
                q_st   <= Q_DONE;
              end

          Q_POST:
            if(~pp_i & ~pp_busy)
              begin
                q_post <= 0;
                q_cnt  <= q_cnt_nx;
                if(q_idx_end)
                  begin
                    q_idx   <= 0;
                    q_base  <= 0;
                    q_first <= 0;
                    q_pass  <= q_pass_nx;
                    q_last  <= q_pass_end;
                  end
                else
                  begin
                    q_idx  <= q_idx_nx;
                    q_base <= q_base_nx;
                  end
                q_st <= Q_NEXT;
              end

          Q_TAB:
            begin
              tab_we <= 1;
              tab_a  <= q_idx[10:0];
              tab_d  <= { 1'b1, 9'd0, sn_trig, sn_ta & q_mask, sn_ts };
              q_cnt  <= q_cnt_nx;
              if(~sn_trig)
                q_untrig <= q_untrig_nx;
              if(q_idx_end)
                begin
                  q_idx    <= 0;
                  seq_wrap <= 1;
                  q_ovf    <= q_ovf | q_circ;
                  q_last   <= ~q_circ;
                end
              else
                begin
                  q_idx    <= q_idx_nx;
                  seq_wrap <= 0;
                end
              q_st <= Q_NEXT;
            end

          Q_NEXT:
            if(q_last | q_sp2)
              begin
                q_done <= 1;
                q_st   <= Q_DONE;
              end
            else
              begin
                if(q_seg)
                  begin
                    tab_we <= 1;
                    tab_a  <= q_idx[10:0];
                    tab_d  <= 64'd0;
                  end
                q_hc <= q_hold;
                q_hz <= 0;
                q_st <= Q_HOLD;
              end

          Q_HOLD:
            if(q_sp2)
              begin
                q_done <= 1;
                q_st   <= Q_DONE;
              end
            else if(q_hz)
              begin
                seq_req <= 1;
                q_st    <= Q_WEN;
              end
            else
              begin
                q_hc <= q_hc - 32'd1;
                q_hz <= (q_hc[31:1] == 31'd0);
              end

          default: ;
        endcase
    end

  seq_acc_ram acc_ram
  (
    .clk (clk_200MHz),
    .we  (aw_we),
    .wa  (aw_a),
    .wd  (aw_d),
    .ra  (q_post ? pp_aa : sample_read_offset[10:0]),
    .rd  (acc_q)
  );

  seq_mirror_ram mirror_ram
  (
    .clka (sample_write_clock),
    .cea  (sample_write_enable & swc_adv),
    .wa   (sample_write_address[10:0]),
    .wd   (sample_dia),
    .clkb (clk_200MHz),
    .ra   (pp_ma),
    .rd   (mir_q)
  );

  //MCU view of the accumulator word at the read pointer (0x2B..0x32): segment table and raw mode
  //as stored; legacy lanes >> S; 16-bit lanes min(65535, sum >> S) in L0 (CH1) and L2 (CH2).
  //seq_lat takes it on every i_mcu_clk edge, like the sample memory's own read register.
  function [15:0] seq_sh16(input [15:0] v, input [4:0] s);
    seq_sh16 = s[4] ? 16'd0 : (v >> s[3:0]);
  endfunction

  function [15:0] seq_sh32(input [31:0] v, input [4:0] s);
    reg [31:0] t;
    begin
      t = v >> s;
      seq_sh32 = (|t[31:16]) ? 16'hFFFF : t[15:0];
    end
  endfunction

  wire [63:0] seq_view = ((q_mode == 2'd1) | seq_raw) ? acc_q :
                         q_fmt16 ? { 16'd0, seq_sh32(acc_q[63:32], avg_shift), 16'd0, seq_sh32(acc_q[31:0], avg_shift) } :
                                   { seq_sh16(acc_q[63:48], avg_shift), seq_sh16(acc_q[47:32], avg_shift),
                                     seq_sh16(acc_q[31:16], avg_shift), seq_sh16(acc_q[15:0], avg_shift) };

  reg [63:0] seq_lat = 0;
  always @(posedge i_mcu_clk)
    seq_lat <= seq_view;

//=====================================================================================

  //Select the address and clock source based on the sample read enbale setting

  assign sample_read_address = sample_read_enable ? sample_read_offset : sample_read_offset;

  assign sample_read_clock   = sample_read_enable ? i_mcu_clk : i_mcu_clk;

//-------------------------------------------------------------------------------------
//Sampling reset state delay counter
//This counter is activated when the reset is activated by the MCU
//Only used to delay the software and on low sample rates it slows the system down

  always @(posedge sample_write_clock)
    begin
      if(sample_system_reset == 0)
        begin
          sample_system_reset_count <= 0;

          sample_system_state <= 0;
        end
      else
        begin
          sample_system_reset_count <= sample_system_reset_count + 1;

          //Flag remains high when count has been reached
          sample_system_state <= (sample_system_reset_count == 6) | sample_system_state;
        end
    end

//-------------------------------------------------------------------------------------
//Loading of the command register

  always @(posedge i_mcu_clk)
    begin
      if(i_mcu_dcs & i_mcu_rws)
        mcu_command <= io_mcu_data;
    end

//-------------------------------------------------------------------------------------
//State machine for the multiple byte write and read of data
//Only used when writing data for the commands 0x0D, 0x0E, 0x1F, 0x32, 0x35, 0x38 and 0x3C
//Or when reading data for the commands 0x06, 0x14 and 0x41

  always @(posedge i_mcu_clk)
    begin
      if(i_mcu_dcs == 1'b1)
        mcu_state <= 3'h0;
      else if((((mcu_command == 8'h0B) | (mcu_command == 8'h0C) | (mcu_command == 8'h0D) | (mcu_command == 8'h0E) 
              | (mcu_command == 8'h1B) | (mcu_command == 8'h1F)
              | (mcu_command == 8'h32) | (mcu_command == 8'h35) | (mcu_command == 8'h38) | (mcu_command == 8'h3C)
              | (mcu_command == 8'h51) | (mcu_command == 8'h52) | (mcu_command == 8'h55)
              | (mcu_command == 8'h58) | (mcu_command == 8'h5A) | (mcu_command == 8'h5C)) & i_mcu_rws) |
              (((mcu_command == 8'h06) | (mcu_command == 8'h14) | (mcu_command == 8'h29) | (mcu_command == 8'h41)
              | (mcu_command == 8'h2A)) & ~i_mcu_rws))
        mcu_state <= mcu_state + 1;
    end

//-------------------------------------------------------------------------------------
//Updating of the sample read address on every read for one of the designated commands

  always @(posedge i_mcu_clk)
    begin
      if((i_mcu_dcs == 0) & (i_mcu_rws == 0) & ((mcu_command == 8'h20) | (mcu_command == 8'h21) | (mcu_command == 8'h22) | (mcu_command == 8'h23)
         | ((mcu_command >= 8'h2B) & (mcu_command <= 8'h32))))
        begin       
          sample_read_offset = sample_read_offset + 1;
        end
        
      else if((i_mcu_dcs == 0) & (i_mcu_rws == 1) & (mcu_command == 8'h1F))
        begin
          //Loading of the two bytes of the data read address offset high byte first
          case(mcu_state)
            4'h0:
              sample_read_offset[12:8] <= io_mcu_data[5:0];
            4'h1:
              sample_read_offset[7:0] <= io_mcu_data;
          endcase
        end
    end

//-------------------------------------------------------------------------------------
//Loading data into the registers based on the set command

  always @(posedge i_mcu_clk)
    begin
      if((i_mcu_dcs == 0) & i_mcu_rws)       //Only handled for data and write
        begin
          case(mcu_command)
            //Setting the sampling reset state
            8'h01:
              begin
                sample_system_reset <= io_mcu_data[0];
                if(io_mcu_data[0])
                  seq_stop <= 0;
              end
              
            //Loading of the two bytes of the pretrigger_samples (set_trigger_point) high byte first
            8'h0B:
              begin
                case(mcu_state)
                  4'h0:
                    pretrigger_samples[12:8] <= io_mcu_data[5:0];
                  4'h1:
                    pretrigger_samples[7:0] <= io_mcu_data;
                endcase
              end
              
            //Loading of the two bytes of the total number of sample high byte first
            8'h0C:
              begin
                case(mcu_state)
                  4'h0:
                    total_samples[12:8] <= io_mcu_data[5:0];
                  4'h1:
                    total_samples[7:0] <= io_mcu_data;
                endcase
              end

            //Loading of the four bytes of the sample rate divider high byte first
            8'h0D:
              begin
                case(mcu_state)
                  4'h0:
                    sample_rate_divider[31:24] <= io_mcu_data;
                  4'h1:
                    sample_rate_divider[23:16] <= io_mcu_data;
                  4'h2:
                    sample_rate_divider[15:8] <= io_mcu_data;
                  4'h3:
                    sample_rate_divider[7:0] <= io_mcu_data;
                endcase
              end

            //Loading of the four bytes of the time base counter high byte first
            8'h0E:
              begin
                case(mcu_state)
                  4'h0:
                    time_base_set[31:24] <= io_mcu_data;
                  4'h1:
                    time_base_set[23:16] <= io_mcu_data;
                  4'h2:
                    time_base_set[15:8] <= io_mcu_data;
                  4'h3:
                    time_base_set[7:0] <= io_mcu_data;
                endcase
              end

            //Setting start of the conversion
            8'h0F:
              sample_read_enable <= io_mcu_data[0];

            //Setting the trigger source: 0 = channel 1, 1 = channel 2, 2 = EXT (3 reserved, acts as EXT)
            8'h15:
              begin
                trigger_channel <= io_mcu_data[0];
                trigger_ext     <= io_mcu_data[1];
              end

            //Setting the trigger edge
            8'h16:
              trigger_edge <= io_mcu_data[0];

            //Setting the trigger level
            8'h17:
              trigger_level <= io_mcu_data;

            //Setting the trigger mode
            8'h1A:
              trigger_mode <= io_mcu_data[0];

            //Loading of the four bytes of the hold on divider high byte first
            8'h1B:
              begin
                case(mcu_state)
                  4'h0:
                    hold_on_divider[31:24] <= io_mcu_data;
                  4'h1:
                    hold_on_divider[23:16] <= io_mcu_data;
                  4'h2:
                    hold_on_divider[15:8] <= io_mcu_data;
                  4'h3:
                    hold_on_divider[7:0] <= io_mcu_data;
                endcase
              end

            //Loading of the two bytes of the channel 1 DC offset high byte first
            8'h32:
              begin
                case(mcu_state)
                  4'h0:
                    ch1_offset[15:8] <= io_mcu_data;
                  4'h1:
                    ch1_offset[7:0] <= io_mcu_data;
                endcase
              end

            //Controlling of the channel 1 input sensitivity relays
            8'h33:
              begin
                if(io_mcu_data < 8'h6)
                  begin
                    relay1_1 <=  io_mcu_data[0];
                    relay1_2 <= ~io_mcu_data[1];
                    relay1_3 <= ~io_mcu_data[2];
                  end
              end

            //Controlling of the channel 1 input for AC or DC
            8'h34:
              ac_dc_1 <= io_mcu_data[0];

            //Loading of the two bytes of the channel 2 DC offset high byte first
            8'h35:
              begin
                case(mcu_state)
                  4'h0:
                    ch2_offset[15:8] <= io_mcu_data;
                  4'h1:
                    ch2_offset[7:0] <= io_mcu_data;
                endcase
              end

            //Controlling of the channel 2 input sensitivity relays
            8'h36:
              begin
                if(io_mcu_data < 8'h6)
                  begin
                    relay2_1 <=  io_mcu_data[0];
                    relay2_2 <= ~io_mcu_data[1];
                    relay2_3 <= ~io_mcu_data[2];
                  end
              end

            //Controlling of the channel 2 input for AC or DC
            8'h37:
              ac_dc_2 <= io_mcu_data[0];

            //Loading of the two bytes of the display brightness control high byte first
            8'h38:
              begin
                case(mcu_state)
                  4'h0:
                    display_brigthness[15:8] <= io_mcu_data;
                  4'h1:
                    display_brigthness[7:0] <= io_mcu_data;
                endcase
              end

            //Loading of the two bytes of the touch panel read address high byte first
            8'h3C:
              begin
                case(mcu_state)
                  4'h0:
                    touch_panel_address[15:8] <= io_mcu_data;
                  4'h1:
                    touch_panel_address[7:0] <= io_mcu_data;
                endcase
              end

            //Setting the output on or tristate
            8'h50:
              gen_out_enable <= io_mcu_data[0];

            //Loading of the four bytes of the clk out divider high byte first 
            8'h51:
              begin
                case(mcu_state)
                  4'h0:
                    clk_out_divider[31:24] <= io_mcu_data;
                  4'h1:
                    clk_out_divider[23:16] <= io_mcu_data;
                  4'h2:
                    clk_out_divider[15:8] <= io_mcu_data;
                  4'h3:
                    clk_out_divider[7:0] <= io_mcu_data;
                endcase
              end

            //Loading of the four bytes of the clk out duty divider high byte first
            8'h52:
              begin
                case(mcu_state)
                  4'h0:
                    clk_out_duty[31:24] <= io_mcu_data;
                  4'h1:
                    clk_out_duty[23:16] <= io_mcu_data;
                  4'h2:
                    clk_out_duty[15:8] <= io_mcu_data;
                  4'h3:
                    clk_out_duty[7:0] <= io_mcu_data;
                endcase
              end

            //Decimation filter order and core order (0x1534)
            8'h53:
              begin
                dec_order <= io_mcu_data[1:0];
                dec_swap  <= io_mcu_data[2];
                dec_neg_a <= io_mcu_data[3];
                dec_neg_b <= io_mcu_data[4];
              end

            //Decimation filter output shift
            8'h54:
              dec_shift <= io_mcu_data[5:0];

            //CIC compensation FIR enable (0x1535), bit 0; bits 7..1 reserved
            8'h56:
              fir_en <= io_mcu_data[0];

            //Loading of the two bytes of the 16-bit trigger level high byte first
            8'h55:
              begin
                case(mcu_state)
                  4'h0:
                    trigger_level16[15:8] <= io_mcu_data;
                  4'h1:
                    trigger_level16[7:0] <= io_mcu_data;
                endcase
              end

            //Segmented capture and averaging (0x1536); write while 0x01 = 1
            8'h57:
              begin
                seq_mode <= io_mcu_data[1:0];
                seq_circ <= io_mcu_data[2];
                seq_raw  <= io_mcu_data[3];
              end

            //Segments or averaging positions, two bytes high byte first (12 bits used)
            8'h58:
              begin
                case(mcu_state)
                  4'h0:
                    seq_count[11:8] <= io_mcu_data[3:0];
                  4'h1:
                    seq_count[7:0] <= io_mcu_data;
                endcase
              end

            //Segment slot size: 2^m words
            8'h59:
              seg_m <= io_mcu_data[3:0];

            //Records per averaging position, two bytes high byte first (0 = 65536)
            8'h5A:
              begin
                case(mcu_state)
                  4'h0:
                    avg_n[15:8] <= io_mcu_data;
                  4'h1:
                    avg_n[7:0] <= io_mcu_data;
                endcase
              end

            //Averaging readout shift
            8'h5B:
              avg_shift <= io_mcu_data[4:0];

            //Hold-off in 5 ns units, four bytes high byte first
            8'h5C:
              begin
                case(mcu_state)
                  4'h0:
                    seq_hold[31:24] <= io_mcu_data;
                  4'h1:
                    seq_hold[23:16] <= io_mcu_data;
                  4'h2:
                    seq_hold[15:8] <= io_mcu_data;
                  4'h3:
                    seq_hold[7:0] <= io_mcu_data;
                endcase
              end

            //Stop the run: no further restarts (cleared by 0x01 = 1)
            8'h5D:
              seq_stop <= io_mcu_data[0];

          endcase
        end
    end

//-------------------------------------------------------------------------------------
//For reading data the requested byte is loaded into a register on the positive edge
//of the i_mcu_clk signal. 

  always @(posedge i_mcu_clk)
    begin
      if((i_mcu_dcs == 0) & (i_mcu_rws == 0))    //Only handled when data and read
        begin
          case(mcu_command)
            //Reading of the sampling system resets state
            8'h05:
              o_mcu_data[0] <= sample_system_state;

            //Reading of the FPGA version needs loading of high byte first
            8'h06:
              begin
                case(mcu_state)
                  4'h0:
                    o_mcu_data <= 8'h15;
                  4'h1:
                    o_mcu_data <= 8'h38;
                endcase
              end

            //Reading of the sampling done state
            //When sampling finishes the writing is disabled. Enabled again with a reset.
            //Can't determine how the original does this, but for the software to work it needs to be inverted
            //0x1536: for a run armed in segment or averaging mode, the run is done
            8'h0A:
              o_mcu_data[0] <= q_active ? q_done : ~sample_write_enable;

            //Reading of the trigger address needs loading of high byte first
            8'h14:
              begin
                case(mcu_state)
                  4'h0:
                    o_mcu_data <= trigger_address[12:8];
                  4'h1:
                    o_mcu_data <= trigger_address[7:0];
                endcase
              end

            //Reading of the sampling triggered state
            //This is not used in the software and is basically the same as command 0x0A
            8'h18:
              o_mcu_data[0] <= sampling_triggered;

            //Reading of the sample data for channel 1 ADC 1 from memory
            8'h20:
              o_mcu_data <= o_adc1A_d;

            //Reading of the sample data for channel 1 ADC 2 from memory
            8'h21:
              o_mcu_data <= o_adc1B_d;

            //Reading of the sample data for channel 2 ADC 1 from memory
            8'h22:
              o_mcu_data <= o_adc2A_d;

            //Reading of the sample data for channel 2 ADC 2 from memory
            8'h23:
              o_mcu_data <= o_adc2B_d;

            //Reading of the sample data for channel 1 ADC 1 
            8'h24:
              o_mcu_data <= i_adc1A_d;

            //Reading of the sample data for channel 1 ADC 2
            8'h25:
              o_mcu_data <= i_adc1B_d;

            //Reading of the sample data for channel 2 ADC 1
            8'h26:
              o_mcu_data <= i_adc2A_d;

            //Reading of the sample data for channel 2 ADC 2
            8'h27:
              o_mcu_data <= i_adc2B_d;

            //Reading of the external trigger status
            //bit0 = filtered EXT pin level, bit1 = generator drives the pin, bit2 = EXT edge since the last reset
            8'h28:
              o_mcu_data <= { 5'b0, ext_activity, gen_out_enable, ext_level };

            //Reading of the decimation geometry, two bytes
            //byte 0: bit7 = 16-bit samples (K >= 1), bit4/bit3 = B/A cores on the falling edge,
            //bit2 = core order swap, bits 1..0 = K; byte 1: shift
            8'h29:
              begin
                case(mcu_state)
                  4'h0:
                    o_mcu_data <= { dec_on, fir_en, fir_act, dec_neg_b, dec_neg_a, dec_swap, dec_order };
                  4'h1:
                    o_mcu_data <= { 2'b0, dec_shift };
                endcase
              end

            //Sequencer status (0x1536), 8 bytes: flags, count (3 bytes), untriggered (2), state and index (2)
            8'h2A:
              begin
                case(mcu_state)
                  4'h0:
                    o_mcu_data <= { q_done, q_ovf, q_active & ~q_done, q_fmt16, seq_raw, q_circ, q_mode };
                  4'h1:
                    o_mcu_data <= q_cnt[23:16];
                  4'h2:
                    o_mcu_data <= q_cnt[15:8];
                  4'h3:
                    o_mcu_data <= q_cnt[7:0];
                  4'h4:
                    o_mcu_data <= q_untrig[15:8];
                  4'h5:
                    o_mcu_data <= q_untrig[7:0];
                  4'h6:
                    o_mcu_data <= { q_st, 1'b0, q_idx[11:8] };
                  4'h7:
                    o_mcu_data <= q_idx[7:0];
                endcase
              end

            //Encode self-check (0x1538), 1 byte: bit 0 sticky, a check failed since the last 0x01 = 1; bit 1 the last
            //write clock prediction was right; bits 3:2 = 01, encode convention 1 (B = write clock, A = its complement,
            //1.25 ns later, as 0x1535 normally ran); bits 7:4 the last pin check {CH2 A, CH2 B, CH1 A, CH1 B}, 1 = right
            8'h33:
              o_mcu_data <= { enc_ok, 2'b01, enc_pred_ok, enc_err };

            //Accumulator / segment table word at the read pointer, bytes 0..7 (0x1536)
            8'h2B:
              o_mcu_data <= seq_lat[7:0];
            8'h2C:
              o_mcu_data <= seq_lat[15:8];
            8'h2D:
              o_mcu_data <= seq_lat[23:16];
            8'h2E:
              o_mcu_data <= seq_lat[31:24];
            8'h2F:
              o_mcu_data <= seq_lat[39:32];
            8'h30:
              o_mcu_data <= seq_lat[47:40];
            8'h31:
              o_mcu_data <= seq_lat[55:48];
            8'h32:
              o_mcu_data <= seq_lat[63:56];

            //Reading of the touch panel address needs loading of high byte first
            8'h41:
              begin
                case(mcu_state)
                  4'h0:
                    o_mcu_data <= touch_panel_address[15:8];
                  4'h1:
                    o_mcu_data <= touch_panel_address[7:0];
                endcase
              end
          endcase
        end
    end

//-------------------------------------------------------------------------------------
//Control of the bidirectional bus for input and output
//The bus direction is based on the read write signal

  assign io_mcu_data = i_mcu_rws ? 8'bZ : o_mcu_data;

//-------------------------------------------------------------------------------------
//Connect the relay control signals to the outside world

  assign o_relay1_1 = relay1_1;
  assign o_relay1_2 = relay1_2;
  assign o_relay1_3 = relay1_3;
  assign o_ac_dc_1  = ac_dc_1;

  assign o_relay2_1 = relay2_1;
  assign o_relay2_2 = relay2_2;
  assign o_relay2_3 = relay2_3;
  assign o_ac_dc_2  = ac_dc_2;


  assign o_adc1_encA = adc1_encA;
  assign o_adc1_encB = adc1_encB;
  assign o_adc2_encA = adc2_encA;
  assign o_adc2_encB = adc2_encB;

/*
  assign o_adc1_encA = clk_ADC0;
  assign o_adc1_encB = clk_ADC90;
  assign o_adc2_encA = clk_ADC0;
  assign o_adc2_encB = clk_ADC90;
*/
  /*
  assign o_adc1_encA = clk_RAM;
  assign o_adc1_encB = clk_200MHz;
  assign o_adc2_encA = clk_RAM;
  assign o_adc2_encB = clk_200MHz;
*/
  
//assign adc1_encB = clk_200MHz; // 0°
//assign adc1_encA = clk_RAM; // 90°

//-------------------------------------------------------------------------------------
endmodule
//-------------------------------------------------------------------------------------
