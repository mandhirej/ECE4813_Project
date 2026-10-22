/* Temporary bare-metal test program. Build INSTEAD OF the game's atlas_main.c.
 * It never reads image pixels through the CPU cache. Export DDR through the
 * Arm DS debugger, then use raw_to_bmp.py on the host PC.
 * Reserve 0x08000000..0x0807E8FF first; see QSYS_STEPS.md.
 */
#include <stdio.h>
#include <stdint.h>
int __auto_semihosting;
#define SNAP_REG(n) (*(volatile uint32_t *)(0xFF200100u+4u*(n)))
#define FRAME_BASE 0x08000000u
static void barrier(void){__asm volatile("dmb sy" ::: "memory");}
int main(void)
{
    uint32_t i,status,width,height,stride;
    printf("V9 HPS snapshot test\n");
    if(SNAP_REG(8)!=0x534E4150u){
        printf("SNAP signature absent. Load the Qsys capture build first.\n");
        return 1;
    }
    width=SNAP_REG(2);height=SNAP_REG(3);stride=SNAP_REG(4);
    if(width!=480u || height!=270u || stride!=1920u){
        printf("Unexpected sampling setting: %lu x %lu stride=%lu.\n",
               (unsigned long)width,(unsigned long)height,(unsigned long)stride);
        return 1;
    }
    if(SNAP_REG(0)&1u){printf("Capture already busy.\n");return 1;}
    SNAP_REG(1)=FRAME_BASE;barrier();
    SNAP_REG(0)=1u;barrier();
    for(i=0;i<100000000u;i++){
        status=SNAP_REG(0);
        if(status&2u)break;
    }
    if(i==100000000u){
        printf("TIMEOUT. Check exported video clock, VS/DE and bridge access.\n");
        return 1;
    }
    if(status&4u){
        printf("CAPTURE ERROR: code=%lu, words=%lu. Do not use this image.\n",
               (unsigned long)SNAP_REG(7),(unsigned long)SNAP_REG(6));
        return 1;
    }
    printf("DONE: %lu x %lu, %lu words, %lu bytes.\n",
           (unsigned long)width,(unsigned long)height,
           (unsigned long)SNAP_REG(6),(unsigned long)(height*stride));
    printf("Export physical DDR: start=0x08000000, length=0x0007E900.\n");
    printf("Save as frame.bin and run: python raw_to_bmp.py frame.bin frame.bmp\n");
    fflush(stdout);
    for(;;)__asm volatile("wfi");
}
