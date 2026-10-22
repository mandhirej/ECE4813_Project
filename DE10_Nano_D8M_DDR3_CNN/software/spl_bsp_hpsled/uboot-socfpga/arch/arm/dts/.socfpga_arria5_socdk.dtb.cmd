cmd_arch/arm/dts/socfpga_arria5_socdk.dtb := mkdir -p arch/arm/dts/ ; (cat arch/arm/dts/socfpga_arria5_socdk.dts; echo '$(pound)include "socfpga_arria5_socdk-u-boot.dtsi"') > arch/arm/dts/.socfpga_arria5_socdk.dtb.pre.tmp; arm-linux-gnueabihf-gcc -E -Wp,-MD,arch/arm/dts/.socfpga_arria5_socdk.dtb.d.pre.tmp -nostdinc -I./arch/arm/dts -I./arch/arm/dts/include -Iinclude -I./include -I./arch/arm/include -include ./include/linux/kconfig.h -D__ASSEMBLY__ -undef -D__DTS__ -x assembler-with-cpp -o arch/arm/dts/.socfpga_arria5_socdk.dtb.dts.tmp arch/arm/dts/.socfpga_arria5_socdk.dtb.pre.tmp ; ./scripts/dtc/dtc -O dtb -o arch/arm/dts/socfpga_arria5_socdk.dtb -b 0 -i arch/arm/dts/ -Wno-unit_address_vs_reg -Wno-simple_bus_reg -Wno-unit_address_format -Wno-pci_bridge -Wno-pci_device_bus_num -Wno-pci_device_reg -Wno-avoid_unnecessary_addr_size -Wno-alias_paths -d arch/arm/dts/.socfpga_arria5_socdk.dtb.d.dtc.tmp arch/arm/dts/.socfpga_arria5_socdk.dtb.dts.tmp ; cat arch/arm/dts/.socfpga_arria5_socdk.dtb.d.pre.tmp arch/arm/dts/.socfpga_arria5_socdk.dtb.d.dtc.tmp > arch/arm/dts/.socfpga_arria5_socdk.dtb.d ; sed -i "s:arch/arm/dts/.socfpga_arria5_socdk.dtb.pre.tmp:arch/arm/dts/socfpga_arria5_socdk.dts:" arch/arm/dts/.socfpga_arria5_socdk.dtb.d

source_arch/arm/dts/socfpga_arria5_socdk.dtb := arch/arm/dts/socfpga_arria5_socdk.dts

deps_arch/arm/dts/socfpga_arria5_socdk.dtb := \
  arch/arm/dts/socfpga_arria5.dtsi \
  arch/arm/dts/socfpga.dtsi \
  arch/arm/dts/include/dt-bindings/reset/altr,rst-mgr.h \
  arch/arm/dts/socfpga_arria5_socdk-u-boot.dtsi \
  arch/arm/dts/socfpga-common-u-boot.dtsi \

arch/arm/dts/socfpga_arria5_socdk.dtb: $(deps_arch/arm/dts/socfpga_arria5_socdk.dtb)

$(deps_arch/arm/dts/socfpga_arria5_socdk.dtb):
