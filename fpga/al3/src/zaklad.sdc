create_clock -name clk_200MHz -period 5 -waveform {0 2} [get_nets clk_200MHz]
create_clock -name i_xtal -period 20 -waveform {0 10}  [get_ports i_xtal]
set_clock_latency  -source 1 [get_clocks {i_xtal}]

#create_generated_clock -name clk_ADC0   -source [get_ports i_xtal] [get_ports o_adc1_encA]
#create_generated_clock -name clk_ADC90  -source [get_ports i_xtal] [get_ports o_adc1_encB]
#create_generated_clock -name clk_ADC2_0 -source [get_ports clk_200MHz] [get_ports o_adc2_encA]
#create_generated_clock -name clk_ADC2_90 -source [get_ports clk_200MHz] [get_ports o_adc2_encB]

# 0x1534: sample_write_clock (the flip-flop toggled on the falling edge of clk_200MHz) as a generated
# clock at its fastest rate, 100 MHz, rising on the falling edges of clk_200MHz. This times the
# decimation filter and the capture logic. Paths from it into clk_200MHz (trigger detect into
# sampling_triggered, ext_pending) see a source that holds for a whole write clock period (>= 10 ns),
# so either of the next two clk_200MHz edges may take it; the 0x1533 behaviour is the same either way.
create_generated_clock -name sample_write_clock -source [get_nets clk_200MHz] -edges {2 4 6} [get_nets sample_write_clock]
set_multicycle_path -setup -from [get_clocks sample_write_clock] -to [get_clocks clk_200MHz] 2
set_multicycle_path -hold -from [get_clocks sample_write_clock] -to [get_clocks clk_200MHz] 1

# 0x1539: the MCU bus clock (i_mcu_clk, pin P1). 40 ns is shorter than any strobe the firmware makes (two
# read-modify-writes of the port register each). The read path uses both edges (o_mcu_data and the read address copy
# on the falling edge, the read pointer and the sample memory's read port on the rising edge), so each of its paths
# has half a period. The strobes are asynchronous to the PLL clocks, and what crosses (status reads, the sequencer
# readout, the configuration registers) is asynchronous by design, so i_mcu_clk is its own clock group.
create_clock -name i_mcu_clk -period 40 -waveform {0 20} [get_ports i_mcu_clk]
set_clock_groups -asynchronous -group [get_clocks i_mcu_clk]
