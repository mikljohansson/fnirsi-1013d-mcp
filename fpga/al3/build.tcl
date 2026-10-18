# Headless Anlogic TD flow for the AL3A10 project. UNTESTED in this repo.
# Adapted from D:\src\FNIRSI_1014D_Firmware\fpga\AL3_1014D\build.tcl (validated there with TD 5.0.3 Linux).
# build.sh fills @TD@ and concatenates the sources into all.v.
import_device al3_10.db
read_verilog -lib @TD@/arch/al3_macro.v -file all.v -top fnirsi_1013D
read_adc zaklad.adc
optimize_rtl
read_sdc zaklad.sdc
map_macro
map
pack
place
route
report_area -file area.rpt
report_io -file io.rpt
report_timing -file timing.rpt
bitgen -bit FPGA.bit -bin FPGA.bin
exit
