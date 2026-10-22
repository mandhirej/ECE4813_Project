cmd_spl/arch/arm/lib/reloc_arm_efi.o := arm-linux-gnueabihf-gcc -Wp,-MD,spl/arch/arm/lib/.reloc_arm_efi.o.d -nostdinc -isystem /usr/lib/gcc-cross/arm-linux-gnueabihf/13/include -Iinclude    -I./arch/arm/include -include ./include/linux/kconfig.h -D__KERNEL__ -D__UBOOT__ -DCONFIG_SPL_BUILD -Wall -Wstrict-prototypes -Wno-format-security -fno-builtin -ffreestanding -std=gnu11 -fshort-wchar -fno-strict-aliasing -fno-PIE -Os -fno-stack-protector -fno-delete-null-pointer-checks -fmacro-prefix-map=./= -g -fstack-usage -Wno-format-nonliteral -Werror=date-time -D__ARM__ -Wa,-mimplicit-it=always -mthumb -mthumb-interwork -mabi=aapcs-linux -mno-unaligned-access -fno-common -msoft-float -pipe -D__LINUX_ARM_ARCH__=7 -mtune=generic-armv7-a -I./arch/arm/mach-socfpga/include -fpic -fshort-wchar    -D"KBUILD_STR(s)=$(pound)s" -D"KBUILD_BASENAME=KBUILD_STR(reloc_arm_efi)"  -D"KBUILD_MODNAME=KBUILD_STR(reloc_arm_efi)" -c -o spl/arch/arm/lib/reloc_arm_efi.o arch/arm/lib/reloc_arm_efi.c

source_spl/arch/arm/lib/reloc_arm_efi.o := arch/arm/lib/reloc_arm_efi.c

deps_spl/arch/arm/lib/reloc_arm_efi.o := \
  include/efi.h \
    $(wildcard include/config/efi/stub/64bit.h) \
    $(wildcard include/config/x86/64.h) \
  include/linux/linkage.h \
  arch/arm/include/asm/linkage.h \
  include/linux/string.h \
  include/linux/types.h \
    $(wildcard include/config/uid16.h) \
  include/linux/posix_types.h \
  include/linux/stddef.h \
  arch/arm/include/asm/posix_types.h \
  arch/arm/include/asm/types.h \
    $(wildcard include/config/arm64.h) \
    $(wildcard include/config/phys/64bit.h) \
    $(wildcard include/config/dma/addr/t/64bit.h) \
  include/asm-generic/int-ll64.h \
  /usr/lib/gcc-cross/arm-linux-gnueabihf/13/include/stdbool.h \
  arch/arm/include/asm/string.h \
    $(wildcard include/config/spl/use/arch/memcpy.h) \
    $(wildcard include/config/spl/use/arch/memset.h) \
  include/config.h \
    $(wildcard include/config/boarddir.h) \
  include/config_defaults.h \
    $(wildcard include/config/defaults/h/.h) \
  include/config_uncmd_spl.h \
    $(wildcard include/config/uncmd/spl/h//.h) \
    $(wildcard include/config/spl/build.h) \
    $(wildcard include/config/spl/dm.h) \
    $(wildcard include/config/dm/serial.h) \
    $(wildcard include/config/dm/gpio.h) \
    $(wildcard include/config/dm/i2c.h) \
    $(wildcard include/config/dm/spi.h) \
    $(wildcard include/config/dm/warn.h) \
    $(wildcard include/config/dm/stdio.h) \
  include/configs/socfpga_cyclone5_socdk.h \
    $(wildcard include/config/socfpga/cyclone5/h//.h) \
    $(wildcard include/config/loadaddr.h) \
    $(wildcard include/config/sys/load/addr.h) \
  arch/arm/include/asm/arch/base_addr_ac5.h \
  include/configs/socfpga_common.h \
    $(wildcard include/config/socfpga/common/h//.h) \
    $(wildcard include/config/clocks.h) \
    $(wildcard include/config/timestamp.h) \
    $(wildcard include/config/sys/malloc/len.h) \
    $(wildcard include/config/sys/memtest/start.h) \
    $(wildcard include/config/sys/memtest/end.h) \
    $(wildcard include/config/target/socfpga/gen5.h) \
    $(wildcard include/config/sys/init/ram/addr.h) \
    $(wildcard include/config/sys/init/ram/size.h) \
    $(wildcard include/config/target/socfpga/arria10.h) \
    $(wildcard include/config/sys/spl/malloc/size.h) \
    $(wildcard include/config/sys/spl/malloc/start.h) \
    $(wildcard include/config/sys/bootcount/addr.h) \
    $(wildcard include/config/spl/stack.h) \
    $(wildcard include/config/spl/stack/r/addr.h) \
    $(wildcard include/config/sys/init/sp/addr.h) \
    $(wildcard include/config/sys/sdram/base.h) \
    $(wildcard include/config/sys/cbsize.h) \
    $(wildcard include/config/sys/maxargs.h) \
    $(wildcard include/config/sys/bargsize.h) \
    $(wildcard include/config/sys/l2/pl310.h) \
    $(wildcard include/config/sys/pl310/base.h) \
    $(wildcard include/config/cmd/net.h) \
    $(wildcard include/config/dw/altdescriptor.h) \
    $(wildcard include/config/cmd/fpga.h) \
    $(wildcard include/config/fpga/count.h) \
    $(wildcard include/config/timer.h) \
    $(wildcard include/config/sys/timerbase.h) \
    $(wildcard include/config/sys/timer/counts/down.h) \
    $(wildcard include/config/sys/timer/counter.h) \
    $(wildcard include/config/sys/timer/rate.h) \
    $(wildcard include/config/hw/watchdog.h) \
    $(wildcard include/config/designware/watchdog.h) \
    $(wildcard include/config/dw/wdt/base.h) \
    $(wildcard include/config/dw/wdt/clock/khz.h) \
    $(wildcard include/config/watchdog/timeout/msecs.h) \
    $(wildcard include/config/cmd/mmc.h) \
    $(wildcard include/config/sys/mmc/max/blk/count.h) \
    $(wildcard include/config/nand/denali.h) \
    $(wildcard include/config/sys/max/nand/device.h) \
    $(wildcard include/config/sys/nand/onfi/detection.h) \
    $(wildcard include/config/sys/nand/regs/base.h) \
    $(wildcard include/config/sys/nand/data/base.h) \
    $(wildcard include/config/spi/flash/mtd.h) \
    $(wildcard include/config/cqspi/ref/clk.h) \
    $(wildcard include/config/cmd/dfu.h) \
    $(wildcard include/config/cmd/usb/mass/storage.h) \
    $(wildcard include/config/sys/dfu/data/buf/size.h) \
    $(wildcard include/config/g/dnl/ums/vendor/num.h) \
    $(wildcard include/config/g/dnl/ums/product/num.h) \
    $(wildcard include/config/env/size.h) \
    $(wildcard include/config/env/is/in/mmc.h) \
    $(wildcard include/config/env/offset.h) \
    $(wildcard include/config/sys/mmc/env/dev.h) \
    $(wildcard include/config/sys/redundand/environment.h) \
    $(wildcard include/config/env/is/in/spi/flash.h) \
    $(wildcard include/config/env/sect/size.h) \
    $(wildcard include/config/env/offset/redund.h) \
    $(wildcard include/config/env/size/redund.h) \
    $(wildcard include/config/env/is/in/nand.h) \
    $(wildcard include/config/spl/text/base.h) \
    $(wildcard include/config/spl/max/size.h) \
    $(wildcard include/config/spl/mmc/support.h) \
    $(wildcard include/config/spl/fs/fat.h) \
    $(wildcard include/config/spl/fs/ext4.h) \
    $(wildcard include/config/spl/fs/load/payload/name.h) \
    $(wildcard include/config/sys/mmcsd/fs/boot/partition.h) \
    $(wildcard include/config/sys/mmcsd/raw/mode/u/boot/partition.h) \
    $(wildcard include/config/spl/nand/support.h) \
    $(wildcard include/config/sys/nand/u/boot/offs.h) \
    $(wildcard include/config/cmd/dhcp.h) \
    $(wildcard include/config/cmd/pxe.h) \
    $(wildcard include/config/cmd/sf.h) \
    $(wildcard include/config/extra/env/settings.h) \
    $(wildcard include/config/default/fdt/file.h) \
  arch/arm/include/asm/config.h \
    $(wildcard include/config/h/.h) \
    $(wildcard include/config/lmb.h) \
    $(wildcard include/config/sys/boot/ramdisk/high.h) \
    $(wildcard include/config/arch/ls1021a.h) \
    $(wildcard include/config/cpu/pxa27x.h) \
    $(wildcard include/config/cpu/monahans.h) \
    $(wildcard include/config/cpu/pxa25x.h) \
    $(wildcard include/config/fsl/layerscape.h) \
  include/config_fallbacks.h \
    $(wildcard include/config/fallbacks/h.h) \
    $(wildcard include/config/spl.h) \
    $(wildcard include/config/spl/pad/to.h) \
    $(wildcard include/config/sys/baudrate/table.h) \
    $(wildcard include/config/cmd/kgdb.h) \
    $(wildcard include/config/sys/pbsize.h) \
    $(wildcard include/config/sys/prompt.h) \
    $(wildcard include/config/sys/i2c.h) \
  include/linux/linux_string.h \
  include/elf.h \
  include/compiler.h \
  /usr/lib/gcc-cross/arm-linux-gnueabihf/13/include/stddef.h \
  arch/arm/include/asm/byteorder.h \
  include/linux/byteorder/little_endian.h \
  include/linux/compiler.h \
    $(wildcard include/config/sparse/rcu/pointer.h) \
    $(wildcard include/config/trace/branch/profiling.h) \
    $(wildcard include/config/profile/all/branches.h) \
    $(wildcard include/config/kasan.h) \
    $(wildcard include/config/enable/must/check.h) \
    $(wildcard include/config/enable/warn/deprecated.h) \
    $(wildcard include/config/kprobes.h) \
  include/linux/compiler-gcc.h \
    $(wildcard include/config/arch/supports/optimized/inlining.h) \
    $(wildcard include/config/optimize/inlining.h) \
    $(wildcard include/config/gcov/kernel.h) \
    $(wildcard include/config/arch/use/builtin/bswap.h) \
  include/linux/byteorder/swab.h \
  include/linux/byteorder/generic.h \

spl/arch/arm/lib/reloc_arm_efi.o: $(deps_spl/arch/arm/lib/reloc_arm_efi.o)

$(deps_spl/arch/arm/lib/reloc_arm_efi.o):
