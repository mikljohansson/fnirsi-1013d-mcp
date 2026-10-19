# Appended to src/zaklad.sdc for the ADC capture margin report only (tb/capture_margins.py), never for the build:
# zero external delays, so TD reports the encode clock-to-pad times and the pad-to-IOB-register setup / hold points.
set_output_delay -clock clk_200MHz 0 [get_ports {o_adc1_encA o_adc1_encB o_adc2_encA o_adc2_encB}]
set_input_delay -clock clk_200MHz 0 [get_ports {i_adc1A_d[*] i_adc1B_d[*] i_adc2A_d[*] i_adc2B_d[*]}]
