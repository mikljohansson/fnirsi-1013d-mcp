# Donwulff-style flow (read_verilog/map/pack), kept as an alternative. Worked with TD 5.0.3 per his notes.
# With TD 4.6.8 it runs through route, then report_io asserts (Design::GetFloorColumn placer_).
# build.sh fills @TD@; select with FLOW=donwulff.
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
