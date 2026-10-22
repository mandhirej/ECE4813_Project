cmd_u-boot-with-spl.sfp := cat	spl/u-boot-spl.sfp spl/u-boot-spl.sfp spl/u-boot-spl.sfp spl/u-boot-spl.sfp u-boot.img > u-boot-with-spl.sfp || rm -f u-boot-with-spl.sfp
