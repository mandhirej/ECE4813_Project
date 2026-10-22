cmd_arch/arm/cpu/armv7/sctlr.o := arm-linux-gnueabihf-gcc -Wp,-MD,arch/arm/cpu/armv7/.sctlr.o.d -nostdinc -isystem /usr/lib/gcc-cross/arm-linux-gnueabihf/13/include -Iinclude    -I./arch/arm/include -include ./include/linux/kconfig.h -D__KERNEL__ -D__UBOOT__ -D__ASSEMBLY__ -fno-PIE -g -D__ARM__ -Wa,-mimplicit-it=always -mthumb -mthumb-interwork -mabi=aapcs-linux -mword-relocations -fno-pic -mno-unaligned-access -ffunction-sections -fdata-sections -fno-common -ffixed-r9 -msoft-float -pipe -D__LINUX_ARM_ARCH__=7 -mtune=generic-armv7-a -I./arch/arm/mach-socfpga/include   -c -o arch/arm/cpu/armv7/sctlr.o arch/arm/cpu/armv7/sctlr.S

source_arch/arm/cpu/armv7/sctlr.o := arch/arm/cpu/armv7/sctlr.S

deps_arch/arm/cpu/armv7/sctlr.o := \
  include/linux/linkage.h \
  arch/arm/include/asm/linkage.h \

arch/arm/cpu/armv7/sctlr.o: $(deps_arch/arm/cpu/armv7/sctlr.o)

$(deps_arch/arm/cpu/armv7/sctlr.o):
