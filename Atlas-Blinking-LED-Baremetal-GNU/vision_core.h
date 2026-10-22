/* V12 single-translation-unit support header: board-tested V1 crop algorithm,
 * snapshot/cache handling and new CNN upload driver. Include only from atlas_main.c.
 * Requires MMU OFF; maintains L1/PL310 cache lines without changing enable bits.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define W 480
#define H 270
#define N (W*H)
#define SIDE 64
#define ALIGNED __attribute__((aligned(32)))
static uint32_t frame[N] ALIGNED;
static uint8_t gray[N] ALIGNED;
static uint8_t visited[N];
static uint32_t queue[N];
/* Global symbol retained for debugger inspection. Own complete cache lines. */
uint8_t crop64[SIDE*SIDE] ALIGNED;
/* High-resolution window (snapshot IP V2, CSR 10..15): a rectangle of the
 * 1920x1080 source at 1/2^step resolution, used for the DRAW card. */
#define HIRES_STEP 1u                     /* 2 = every 2nd source pixel: 2x the snapshot */
#define HIRES_MAX_WORDS (256u*192u)
static uint32_t hires[HIRES_MAX_WORDS] ALIGNED;
static unsigned hires_x0,hires_y0,hires_w,hires_h;   /* source pixels; buffer is (w>>step) x (h>>step) */
static int hires_supported=0;
typedef struct { double x,y; } Point;
static Point corners[4]; /* TL, TR, BR, BL in camera coordinates */
static uint32_t histogram[256];

static int vision_verbose=1;
#define VPRINT(...) do { if(vision_verbose) printf(__VA_ARGS__); } while(0)
#define VPUTS(s) do { if(vision_verbose) puts(s); } while(0)
static int make_gray_and_threshold(void)
{
    unsigned i,t; uint64_t total=0,sum0=0; uint32_t n0=0;
    double best=-1.0; int threshold=80;
    memset(histogram,0,sizeof(histogram));
    for(i=0;i<N;i++) {
        uint32_t p=frame[i];
        gray[i]=(uint8_t)((77*((p>>16)&255)+150*((p>>8)&255)+29*(p&255)+128)>>8);
        histogram[gray[i]]++;total+=gray[i];
    }
    for(t=0;t<255;t++) {
        double d,score; uint32_t n1;
        n0+=histogram[t];sum0+=(uint64_t)t*histogram[t];n1=N-n0;
        if(!n0||!n1)continue;
        d=(double)sum0/n0-(double)(total-sum0)/n1;
        score=(double)n0*n1*d*d;
        if(score>best){best=score;threshold=(int)t;}
    }
    return threshold;
}

static int detect_card(int threshold)
{
    unsigned i,largest=0,second=0,objects=0;
    memset(visited,0,sizeof(visited));
    for(i=0;i<N;i++) {
        unsigned head=0,tail=0,count=0;
        int mins=100000,maxs=-1,mind=100000,maxd=-100000;
        Point q[4];
        if(visited[i]||gray[i]<=threshold)continue;
        queue[tail++]=i;visited[i]=1;
        while(head<tail) {
            unsigned p=queue[head++],x=p%W,y=p/W,j;
            unsigned neighbors[4],nn=0;
            int s=(int)x+(int)y,d=(int)x-(int)y;
            Point a={(double)x,(double)y};count++;
            if(s<mins){mins=s;q[0]=a;} if(d>maxd){maxd=d;q[1]=a;}
            if(s>maxs){maxs=s;q[2]=a;} if(d<mind){mind=d;q[3]=a;}
            if(x)neighbors[nn++]=p-1;
            if(x+1<W)neighbors[nn++]=p+1;
            if(y)neighbors[nn++]=p-W;
            if(y+1<H)neighbors[nn++]=p+W;
            for(j=0;j<nn;j++) {
                unsigned k=neighbors[j];
                if(!visited[k]&&gray[k]>threshold){visited[k]=1;queue[tail++]=k;}
            }
        }
        if(count>=1000)objects++;
        if(count>largest){second=largest;largest=count;memcpy(corners,q,sizeof(q));}
        else if(count>second)second=count;
    }
    VPRINT("THRESHOLD=%d; largest=%u bright pixels; second=%u; large regions=%u\n",threshold,largest,second,objects);
    if(largest<2000){VPUTS("CROP ERROR: no sufficiently large white card.");return 0;}
    if(second>=1000 && second>largest/4){VPUTS("CROP ERROR: multiple large objects. Show ONE card for this test.");return 0;}
    {
        double width=0,height=0,area=0;
        int k;
        for(k=0;k<4;k++) {
            Point a=corners[k],b=corners[(k+1)%4],c=corners[(k+2)%4];
            double dx=b.x-a.x,dy=b.y-a.y;
            if(dx*(c.y-b.y)-dy*(c.x-b.x)<=0){VPUTS("CROP ERROR: degenerate card corners.");return 0;}
            if((int)a.x<2||(int)a.y<2||a.x>W-3||a.y>H-3){VPUTS("CROP ERROR: card touches frame edge. Move it inward.");return 0;}
            if(k%2)height+=dx*dx+dy*dy;else width+=dx*dx+dy*dy;
            area+=a.x*b.y-a.y*b.x;
        }
        if(height<width*1.15*1.15||height>width*2.5*2.5){
            VPUTS("CROP ERROR: expected an upright portrait card in the camera image. No automatic 90-degree rotation.");return 0;
        }
        if(area<4000||largest/(area*0.5)<0.4){VPUTS("CROP ERROR: unsuitable card geometry.");return 0;}
    }
    return 1;
}

/* Unit square -> camera quadrilateral projective transform. The caller supplies
 * the corner correspondence; board_rectify permutes it for landscape cards.
 * Bilinear grayscale sampling, full card stretched to 64x64 (no padding).
 */
static int rectify(void)
{
    double x0=corners[0].x,y0=corners[0].y,x1=corners[1].x,y1=corners[1].y;
    double x2=corners[2].x,y2=corners[2].y,x3=corners[3].x,y3=corners[3].y;
    double dx1=x1-x2,dx2=x3-x2,dx3=x0-x1+x2-x3;
    double dy1=y1-y2,dy2=y3-y2,dy3=y0-y1+y2-y3;
    double det=dx1*dy2-dx2*dy1,g,h,a,b,c,d,e,f;
    int ox,oy;
    if(det>-0.000001&&det<0.000001)return 0;
    g=(dx3*dy2-dx2*dy3)/det;h=(dx1*dy3-dx3*dy1)/det;
    a=x1-x0+g*x1;b=x3-x0+h*x3;c=x0;
    d=y1-y0+g*y1;e=y3-y0+h*y3;f=y0;
    for(oy=0;oy<SIDE;oy++)for(ox=0;ox<SIDE;ox++) {
        double u=(double)ox/(SIDE-1),v=(double)oy/(SIDE-1),den=g*u+h*v+1;
        double x,y,fx,fy,value;int ix,iy;
        if(den>-0.000001&&den<0.000001)return 0;
        x=(a*u+b*v+c)/den;y=(d*u+e*v+f)/den;
        if(x<0||y<0||x>W-1||y>H-1)return 0;
        ix=(int)x;iy=(int)y;
        if(ix>=W-1)ix=W-2;
        if(iy>=H-1)iy=H-2;
        fx=x-ix;fy=y-iy;
        value=(1-fy)*((1-fx)*gray[iy*W+ix]+fx*gray[iy*W+ix+1])+
                   fy*((1-fx)*gray[(iy+1)*W+ix]+fx*gray[(iy+1)*W+ix+1]);
        crop64[oy*SIDE+ox]=(uint8_t)(value+0.5);
    }
    return 1;
}

static int process_frame(void)
{
    int i,threshold=make_gray_and_threshold();
    uint32_t hash=2166136261u;
    if(!detect_card(threshold))return 0;
    for(i=0;i<4;i++)VPRINT("%s=(%u,%u)\n",i==0?"TL":i==1?"TR":i==2?"BR":"BL",(unsigned)corners[i].x,(unsigned)corners[i].y);
    if(!rectify()){VPUTS("CROP ERROR: perspective mapping failed.");return 0;}
    for(i=0;i<SIDE*SIDE;i++)hash=(hash^crop64[i])*16777619u;
    VPRINT("CROP DONE: 64 x 64, 4096 bytes, FNV1a=0x%08lx\n",(unsigned long)hash);
    return 1;
}


#ifndef VISION_HOST_TEST
#define REG(n) (*(volatile uint32_t *)(uintptr_t)(0xFF200100u+4u*(n)))
static void barrier(void){__asm volatile("dsb sy" ::: "memory");}
static uint32_t l1_enabled,l2_enabled;
#define L2(off) (*(volatile uint32_t *)(uintptr_t)(0xFFFEF000u+(off)))
/* Cortex-A9/PL310 use 32-byte lines. All buffers and lengths below are aligned.
 * CPU0 diagnostic only: CPU1 must remain parked; no concurrent DMA-buffer users.
 * Register offsets/operations checked against Intel SoCFPGA HWLIB alt_cache.c.
 */
static int l2_wait(unsigned off)
{
    unsigned t;
    for(t=0;t<1000000u;t++)if(!(L2(off)&1u))return 1;
    return 0;
}
static int outer_range(void *buffer,unsigned length,int invalidate_only)
{
    uintptr_t a=(uintptr_t)buffer,end=a+length;
    unsigned off=invalidate_only?0x770u:0x7f0u;
    if(!l2_enabled)return 1;
    for(;a<end;a+=32){L2(off)=(uint32_t)a;if(!l2_wait(off))return 0;}
    L2(0x730)=0;
    if(!l2_wait(0x730))return 0;
    barrier();return 1;
}
static void inner_range(void *buffer,unsigned length,int invalidate_only)
{
    uintptr_t a=(uintptr_t)buffer,end=a+length;
    if(l1_enabled)for(;a<end;a+=32) {
        if(invalidate_only)__asm volatile("mcr p15, 0, %0, c7, c6, 1" :: "r"(a) : "memory");
        else __asm volatile("mcr p15, 0, %0, c7, c14, 1" :: "r"(a) : "memory");
    }
    barrier();
}
static int prepare_dma(void *buffer,unsigned length)
{
    inner_range(buffer,length,0);return outer_range(buffer,length,0);
}
static int complete_dma(void *buffer,unsigned length)
{
    /* No CPU writes to this buffer between prepare_dma and complete_dma. */
    if(!outer_range(buffer,length,1))return 0;
    inner_range(buffer,length,1);return 1;
}

#define UREG(n) (*(volatile uint32_t *)(uintptr_t)(0xFF200200u+4u*(n)))
static int vision_init(void)
{
 uint32_t sctlr,l2;uintptr_t address=(uintptr_t)frame;
 __asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
 printf("SCTLR=0x%08lx; MMU=%lu; L1D=%lu\n",(unsigned long)sctlr,(unsigned long)(sctlr&1),(unsigned long)((sctlr>>2)&1));
 if(sctlr&1u){puts("STOP: MMU enabled; send startup source. Do not bypass this check.");return 0;}
 l2=*(volatile uint32_t *)(uintptr_t)0xFFFEF100u;
 l1_enabled=(sctlr>>2)&1u;l2_enabled=l2&1u;
 printf("L2=%lu; frame=0x%08lx\n",(unsigned long)l2_enabled,(unsigned long)address);
 if(address<0x02000000u || address+sizeof(frame)>0x04000000u){puts("STOP: unexpected frame allocation.");return 0;}
 if(REG(8)!=0x534e4150u || REG(2)!=W || REG(3)!=H || REG(4)!=W*4){puts("STOP: snapshot IP/resolution mismatch.");return 0;}
 if(UREG(7)!=0x434e4e31u || UREG(13)!=0x10000u){puts("STOP: V10 CNN upload ID/version absent. Check new SOF and base 0x0200.");return 0;}
 hires_supported=REG(9)>=0x00020000u;
 printf("Snapshot IP version 0x%08lx: high-resolution DRAW %s.\n",(unsigned long)REG(9),
        hires_supported?"ON":"OFF (old SOF, normal crop)");
 UREG(0)=4;barrier();
 {unsigned i;for(i=0;i<100000u;i++)if(!(UREG(0)&0x61u))return 1;}
 puts("STOP: CNN reset did not complete.");return 0;
}
/* Capture the source rectangle x0,y0,w,h (multiples of 2^HIRES_STEP) into hires[].
 * Returns 1 on success; on any problem 0 and window mode is switched off again. */
static int snapshot_window(unsigned x0,unsigned y0,unsigned w,unsigned h)
{
 unsigned words=(w>>HIRES_STEP)*(h>>HIRES_STEP);uint32_t status=0,start_tick;
 if(!hires_supported||words==0||words>HIRES_MAX_WORDS||(REG(0)&1u))return 0;
 if(!prepare_dma(hires,words*4u))return 0;
 REG(10)=x0;REG(11)=y0;REG(12)=w;REG(13)=h;REG(15)=HIRES_STEP;REG(14)=1u;
 REG(1)=(uint32_t)(uintptr_t)hires;barrier();REG(0)=1u;barrier();
 start_tick=UREG(15);
 for(;;){
  status=REG(0);
  if(status&6u)break;
  if((uint32_t)(UREG(15)-start_tick)>100000000u)break;
 }
 REG(14)=0u;barrier();                  /* back to normal 480x270 snapshots */
 if((status&6u)!=2u||REG(6)!=words){
  printf("HIRES: window capture failed (status=%lu words=%lu/%u); using the normal crop.\n",
         (unsigned long)status,(unsigned long)REG(6),words);
  return 0;
 }
 if(!complete_dma(hires,words*4u))return 0;
 hires_x0=x0;hires_y0=y0;hires_w=w;hires_h=h;
 return 1;
}
/* 1=valid crop, 0=no usable single card, -1=hardware/cache fault. */
static int snapshot_frame(void)
{
 uint32_t start_tick,status,guard=0;
 if(REG(0)&1u){puts("ERROR: snapshot already busy.");return -1;}
 if(!prepare_dma(frame,sizeof(frame))){puts("ERROR: pre-capture cache timeout.");return -1;}
 REG(1)=(uint32_t)(uintptr_t)frame;barrier();REG(0)=1u;barrier();
 start_tick=UREG(15);
 for(;;) {
  status=REG(0);
  if(status&4u){
   printf("ERROR: snapshot hardware error while waiting: status=%lu code=%lu words=%lu tick=0x%08lx\n",
          (unsigned long)status,(unsigned long)REG(7),(unsigned long)REG(6),
          (unsigned long)UREG(15));
   return -1;
  }
  if(status&2u)break;
  /* Two independent escape routes. V15 used only UREG(15); if that hardware
   * tick ever stops advancing, the old loop can wait forever. The software
   * guard guarantees that B/A/T/G cannot hard-freeze here. */
  if((uint32_t)(UREG(15)-start_tick)>100000000u || ++guard>50000000u){
   printf("ERROR: snapshot timeout: status=%lu code=%lu words=%lu start_tick=0x%08lx now=0x%08lx guard=%lu\n",
          (unsigned long)status,(unsigned long)REG(7),(unsigned long)REG(6),
          (unsigned long)start_tick,(unsigned long)UREG(15),(unsigned long)guard);
   return -1;
  }
 }
 barrier();
 if(REG(6)!=N){printf("ERROR: snapshot status=%lu code=%lu words=%lu\n",(unsigned long)status,(unsigned long)REG(7),(unsigned long)REG(6));return -1;}
 if(!complete_dma(frame,sizeof(frame))){puts("ERROR: post-capture cache timeout.");return -1;}
 return 1;
}
static int vision_frame(void)
{
 int result=snapshot_frame();
 return result<0?result:process_frame();
}
static uint32_t crop_hash(const uint8_t *pixels)
{
 unsigned i;uint32_t h=0;
 for(i=0;i<4096;i++)h=((h<<5)|(h>>27))^pixels[i];
 return h;
}
/* gesture=1 runs the FPGA's gesture mode (classes 54..57). The mode request travels on
 * the bbox registers: bbox_valid=0 and bbox_min_x=0x7FF (see cnn_accelerator_top).
 * Card runs always write the bbox registers too, so a stale request cannot leak. */
#define CNN_GESTURE_MAGIC 0x7FFu
static int cnn_run_mode(const uint8_t *pixels,int live_bbox,int gesture,uint8_t *prediction)
{
 unsigned i;uint32_t status,tick,expected=crop_hash(pixels);
 if(UREG(0)&0x61u){puts("ERROR: CNN not idle.");return 0;}
 UREG(0)=1;barrier();
 for(i=0;i<4096;i++)UREG(1)=pixels[i];
 barrier();
 if(UREG(2)!=4096u || UREG(14)!=4096u || UREG(3)!=expected || (UREG(0)&4u)){
  printf("UPLOAD ERROR: bridge=%lu RAM=%lu hash=%08lx expected=%08lx err=%lu\n",(unsigned long)UREG(2),(unsigned long)UREG(14),(unsigned long)UREG(3),(unsigned long)expected,(unsigned long)UREG(6));return 0;
 }
 if(gesture)live_bbox=0;
 if(!live_bbox){UREG(12)=0;UREG(8)=gesture?CNN_GESTURE_MAGIC:0u;}
 if(live_bbox){
  unsigned xmin=W-1,xmax=0,ymin=H-1,ymax=0;
  for(i=0;i<4;i++){
   unsigned x=(unsigned)corners[i].x,y=(unsigned)corners[i].y;
   if(x<xmin)xmin=x;
   if(x>xmax)xmax=x;
   if(y<ymin)ymin=y;
   if(y>ymax)ymax=y;
  }
  UREG(8)=xmin*4;UREG(9)=xmax*4+3;UREG(10)=ymin*4;UREG(11)=ymax*4+3;UREG(12)=1;
 }
 barrier();UREG(0)=2;barrier();tick=UREG(15);
 do{
  status=UREG(0);
  if(status&4u){printf("CNN ERROR: code=%lu\n",(unsigned long)UREG(6));return 0;}
  if((uint32_t)(UREG(15)-tick)>100000000u){puts("CNN TIMEOUT: resetting only the CNN core.");UREG(0)=4;barrier();return 0;}
 }while(!(status&2u));
 *prediction=(uint8_t)UREG(4);
 if(gesture?(*prediction<54||*prediction>57):(*prediction>53)){puts("ERROR: invalid CNN class.");return 0;}
 if(vision_verbose)printf("UPLOAD PASS: 4096 bytes, RAM hash=0x%08lx; CNN cycles=%lu (~%lu ms)\n",(unsigned long)expected,(unsigned long)UREG(5),(unsigned long)(UREG(5)/50000u));
 return 1;
}
static int cnn_run(const uint8_t *pixels,int live_bbox,uint8_t *prediction)
{
 return cnn_run_mode(pixels,live_bbox,0,prediction);
}
#endif /* !VISION_HOST_TEST */
#undef VPRINT
#undef VPUTS
