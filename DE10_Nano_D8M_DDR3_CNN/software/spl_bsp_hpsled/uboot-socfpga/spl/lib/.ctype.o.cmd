cmd_spl/lib/ctype.o := arm-linux-gnueabihf-gcc -Wp,-MD,spl/lib/.ctype.o.d -nostdinc -isystem /usr/lib/gcc-cross/arm-linux-gnueabihf/13/include -Iinclude    -I./arch/arm/include -include ./include/linux/kconfig.h -D__KERNEL__ -D__UBOOT__ -DCONFIG_SPL_BUILD -Wall -Wstrict-prototypes -Wno-format-security -fno-builtin -ffreestanding -std=gnu11 -fshort-wchar -fno-strict-aliasing -fno-PIE -Os -fno-stack-protector -fno-delete-null-pointer-checks -fmacro-prefix-map=./= -g -fstack-usage -Wno-format-nonliteral -Werror=date-time -ffunction-sections -fdata-sections -D__ARM__ -Wa,-mimplicit-it=always -mthumb -mthumb-interwork -mabi=aapcs-linux -mno-unaligned-access -ffunction-sections -fdata-sections -fno-common -ffixed-r9 -msoft-float -pipe -D__LINUX_ARM_ARCH__=7 -mtune=generic-armv7-a -I./arch/arm/mach-socfpga/include    -D"KBUILD_STR(s)=$(pound)s" -D"KBUILD_BASENAME=KBUILD_STR(ctype)"  -D"KBUILD_MODNAME=KBUILD_STR(ctype)" -c -o spl/lib/ctype.o lib/ctype.c

source_spl/lib/ctype.o := lib/ctype.c

deps_spl/lib/ctype.o := \
  include/linux/ctype.h \

spl/lib/ctype.o: $(deps_spl/lib/ctype.o)

$(deps_spl/lib/ctype.o):
