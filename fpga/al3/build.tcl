# Headless replay of the command sequence Atlan4's TD 4.6.8 SP1 (V4.6.116866) GUI ran for this
# project (research/atlan4-extracted/FPGA/FPGA/td_2025-11-1*.log), minus the GUI-only steps.
# legalize_phy_inst, start_timer and the second read_sdc appear in that log nested inside
# optimize_gate/place/route; issuing them again by hand makes route segfault, so they are omitted.
# The bitgen user code is 8 zero bits + the 8-bit <UCode> from FPGA.al + 16 zero bits (Atlan4's
# Feb 2026 image carries 0x0056 = UCode 01010110; his Nov 2025 log used 11010010).
# Sources are used unchanged; build.sh stages src/ (FPGA.al, zaklad.*, al_ip/) into the work dir.
open_project FPGA.al
import_device al3_10.db -package LQFP144
elaborate -top fnirsi_1013D
read_adc zaklad.adc
optimize_rtl
report_area -file FPGA_rtl.area
read_sdc zaklad.sdc
export_db FPGA_rtl.db
optimize_gate -packarea FPGA_gate.area
report_area -file FPGA_gate.area
export_db FPGA_gate.db
place
route
report_area -io_info -file FPGA_phy.area
export_db FPGA_pr.db
report_timing -mode FINAL -net_info -ep_num 3 -path_num 3 -file FPGA_phy.timing
bitgen -bit FPGA.bit -version 0X00 -g ucode:00000000@UCODE@0000000000000000 -bin FPGA.bin
exit
