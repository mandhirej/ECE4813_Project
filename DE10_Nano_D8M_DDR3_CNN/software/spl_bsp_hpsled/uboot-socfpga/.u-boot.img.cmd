cmd_u-boot.img := ./tools/mkimage -A arm -T firmware -C none -O u-boot -a 0x01000040 -e 0 -n "U-Boot 2019.10""zImage for cyclone5-socdk board" -d u-boot.bin u-boot.img >/dev/null 
