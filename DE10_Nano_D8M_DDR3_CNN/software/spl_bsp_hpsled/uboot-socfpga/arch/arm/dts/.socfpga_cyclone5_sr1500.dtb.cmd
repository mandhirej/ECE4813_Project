cmd_arch/arm/dts/socfpga_cyclone5_sr1500.dtb := mkdir -p arch/arm/dts/ ; (cat arch/arm/dts/socfpga_cyclone5_sr1500.dts; ) > arch/arm/dts/.socfpga_cyclone5_sr1500.dtb.pre.tmp; arm-linux-gnueabihf-gcc -E -Wp,-MD,arch/arm/dts/.socfpga_cyclone5_sr1500.dtb.d.pre.tmp -nostdinc -I./arch/arm/dts -I./arch/arm/dts/include -Iinclude -I./include -I./arch/arm/include -include ./include/linux/kconfig.h -D__ASSEMBLY__ -undef -D__DTS__ -x assembler-with-cpp -o arch/arm/dts/.socfpga_cyclone5_sr1500.dtb.dts.tmp arch/arm/dts/.socfpga_cyclone5_sr1500.dtb.pre.tmp ; ./scripts/dtc/dtc -O dtb -o arch/arm/dts/socfpga_cyclone5_sr1500.dtb -b 0 -i arch/arm/dts/ -Wno-unit_address_vs_reg -Wno-simple_bus_reg -Wno-unit_address_format -Wno-pci_bridge -Wno-pci_device_bus_num -Wno-pci_device_reg -Wno-avoid_unnecessary_addr_size -Wno-alias_paths -d arch/arm/dts/.socfpga_cyclone5_sr1500.dtb.d.dtc.tmp arch/arm/dts/.socfpga_cyclone5_sr1500.dtb.dts.tmp ; cat arch/arm/dts/.socfpga_cyclone5_sr1500.dtb.d.pre.tmp arch/arm/dts/.socfpga_cyclone5_sr1500.dtb.d.dtc.tmp > arch/arm/dts/.socfpga_cyclone5_sr1500.dtb.d ; sed -i "s:arch/arm/dts/.socfpga_cyclone5_sr1500.dtb.pre.tmp:arch/arm/dts/socfpga_cyclone5_sr1500.dts:" arch/arm/dts/.socfpga_cyclone5_sr1500.dtb.d

source_arch/arm/dts/socfpga_cyclone5_sr1500.dtb := arch/arm/dts/socfpga_cyclone5_sr1500.dts

deps_arch/arm/dts/socfpga_cyclone5_sr1500.dtb := \
  arch/arm/dts/socfpga_cyclone5.dtsi \
  arch/arm/dts/socfpga.dtsi \
  arch/arm/dts/include/dt-bindings/reset/altr,rst-mgr.h \
  arch/arm/dts/socfpga-common-u-boot.dtsi \

arch/arm/dts/socfpga_cyclone5_sr1500.dtb: $(deps_arch/arm/dts/socfpga_cyclone5_sr1500.dtb)

$(deps_arch/arm/dts/socfpga_cyclone5_sr1500.dtb):
