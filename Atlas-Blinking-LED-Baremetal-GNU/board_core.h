/* V16 4x4 suit-board + fixed fifth-column DRAW-slot detector (row from GRID_DRAW_Y0).
 * Include after vision_core.h in exactly one translation unit.
 *
 * Geometry:
 *   - 16 board cells: row-major 0..15, columns D/C/H/S, rows 1..4.
 *   - cell 16: fixed DRAW slot in the fifth column, row BOARD_DRAW_ROW (top).
 *   - the other three fifth-column cells are the HG hand-gesture zone.
 *
 * Detection is component-based over one combined 5-column camera region.
 * A card is assigned by the detected card CENTER, so edges may cross guide
 * lines slightly. The three HG cells below DRAW are ignored here.
 */
#ifndef BOARD_CORE_H
#define BOARD_CORE_H

#include "fixed_board_grid.h"

#define BOARD_CELLS 16u
#define BOARD_ACTIVE_CELLS 17u
#define BOARD_DRAW_CELL 16u
#define BOARD_DRAW_ROW ((GRID_DRAW_Y0-GRID_Y0)/GRID_STEP_Y)
#define BOARD_MIN_PIXELS 180u
#define BOARD_SEARCH_MARGIN 10
#define BOARD_MAX_COMPONENTS 40u

enum { BOARD_SLOT_BAD=0, BOARD_SLOT_GOOD=1, BOARD_SLOT_AMBIG=2 };

#ifndef BOARD_LANDSCAPE_ROTATION
#define BOARD_LANDSCAPE_ROTATION 1
#endif
#if BOARD_LANDSCAPE_ROTATION != 1 && BOARD_LANDSCAPE_ROTATION != -1
#error "BOARD_LANDSCAPE_ROTATION must be 1 or -1"
#endif

typedef struct {
    Point q[4];
    double cx,cy,area;
    int xmin,xmax,ymin,ymax;
    unsigned pixels;
    int landscape;
} CardQuad;

typedef struct { int x0,y0,x1,y1; } BoardROI;

static BoardROI board_roi[BOARD_ACTIVE_CELLS];
static CardQuad board_detected[BOARD_ACTIVE_CELLS];
static uint8_t board_detect_state[BOARD_ACTIVE_CELLS];
static CardQuad board_component_scratch[BOARD_MAX_COMPONENTS];
static int board_ready=0;
/* Only the second, cell-local pass uses the more tolerant border check. */
static unsigned board_border_min_hits=24u;

/* Debugger-export buffers. 0..15 = D/C/H/S board, 16 = DRAW. */
uint8_t board_crops[BOARD_ACTIVE_CELLS][4096] ALIGNED;
uint32_t board_meta[BOARD_ACTIVE_CELLS][9] ALIGNED;

static int quad_geometry(CardQuad *c)
{
    int k; double w2=0,h2=0,area=0;
    c->cx=c->cy=0;c->xmin=W;c->xmax=0;c->ymin=H;c->ymax=0;
    for(k=0;k<4;k++) {
        Point a=c->q[k],b=c->q[(k+1)%4],d=c->q[(k+2)%4];
        double dx=b.x-a.x,dy=b.y-a.y;
        if(dx*(d.y-b.y)-dy*(d.x-b.x)<=0)return 0;
        if(a.x<2||a.y<2||a.x>W-3||a.y>H-3)return 0;
        if(k&1)h2+=dx*dx+dy*dy; else w2+=dx*dx+dy*dy;
        area+=a.x*b.y-a.y*b.x; c->cx+=a.x/4; c->cy+=a.y/4;
        if(a.x<c->xmin)c->xmin=(int)a.x;
        if(a.x>c->xmax)c->xmax=(int)a.x;
        if(a.y<c->ymin)c->ymin=(int)a.y;
        if(a.y>c->ymax)c->ymax=(int)a.y;
    }
    c->area=area/2;
    c->landscape=w2>h2;
    {
        double short2=c->landscape?h2:w2,long2=c->landscape?w2:h2;
        if(short2<2*18*18||long2<2*27*27||
           long2<short2*1.10*1.10||long2>short2*2.5*2.5)return 0;
    }
    if(!c->landscape)return 0;
    if(c->area<GRID_SLOT_WIDTH*GRID_SLOT_HEIGHT*0.18 || c->pixels<c->area*0.07)return 0;
    return 1;
}

static int board_border_ok(const CardQuad *c,int threshold)
{
    int k,j,hit=0;
    for(k=0;k<4;k++)for(j=1;j<=8;j++) {
        Point a=c->q[k],b=c->q[(k+1)%4];
        double u=(double)j/9;
        int x=(int)(a.x+(b.x-a.x)*u+0.5),y=(int)(a.y+(b.y-a.y)*u+0.5);
        int dx,dy,ok=0;
        for(dy=-1;dy<=1;dy++)for(dx=-1;dx<=1;dx++)
            if(x+dx>=0&&x+dx<W&&y+dy>=0&&y+dy<H&&gray[(y+dy)*W+x+dx]>threshold)ok=1;
        hit+=ok;
    }
    return hit>=(int)board_border_min_hits;
}

static unsigned board_components(BoardROI r,int threshold,CardQuad *out,unsigned capacity)
{
    int x,y; unsigned found=0;
    for(y=r.y0;y<=r.y1;y++)memset(visited+y*W+r.x0,0,(size_t)(r.x1-r.x0+1));
    for(y=r.y0;y<=r.y1;y++)for(x=r.x0;x<=r.x1;x++) {
        unsigned i=(unsigned)(y*W+x),head=0,tail=0;
        int mins=100000,maxs=-1,mind=100000,maxd=-100000,touched=0;
        CardQuad c;
        if(visited[i]||gray[i]<=threshold)continue;
        memset(&c,0,sizeof(c)); queue[tail++]=i; visited[i]=1;
        while(head<tail) {
            unsigned p=queue[head++],j,nn=0,nb[4];
            int px=(int)(p%W),py=(int)(p/W),s=px+py,d=px-py;
            Point a={(double)px,(double)py};
            if(s<mins){mins=s;c.q[0]=a;} if(d>maxd){maxd=d;c.q[1]=a;}
            if(s>maxs){maxs=s;c.q[2]=a;} if(d<mind){mind=d;c.q[3]=a;}
            if(px==r.x0||px==r.x1||py==r.y0||py==r.y1)touched=1;
            if(px>r.x0)nb[nn++]=p-1;
            if(px<r.x1)nb[nn++]=p+1;
            if(py>r.y0)nb[nn++]=p-W;
            if(py<r.y1)nb[nn++]=p+W;
            for(j=0;j<nn;j++)if(!visited[nb[j]]&&gray[nb[j]]>threshold) {
                visited[nb[j]]=1; queue[tail++]=nb[j];
            }
        }
        c.pixels=tail;
        if(touched||tail<BOARD_MIN_PIXELS||!quad_geometry(&c)||!board_border_ok(&c,threshold))continue;
        if(found<capacity)out[found]=c;
        found++;
    }
    return found;
}

static int board_threshold(void)
{
    int t=make_gray_and_threshold();
    if(t<60)t=60;
    if(t>200)t=200;
    return t;
}

static void board_record(unsigned cell,const CardQuad *c)
{
    unsigned k;
    board_meta[cell][0]=1;
    for(k=0;k<4;k++) {
        board_meta[cell][1+2*k]=(uint32_t)c->q[k].x;
        board_meta[cell][2+2*k]=(uint32_t)c->q[k].y;
    }
}

/* 32-bit Q8 warp retained from the proven V15.3/V15.5 board path. */
static int board_rectify(const CardQuad *c)
{
    unsigned ox,oy,k;
    unsigned offset=c->landscape?(BOARD_LANDSCAPE_ROTATION==1?3u:1u):0u;
    int qx[4],qy[4]; unsigned frac[64];
    for(k=0;k<4;k++) {
        qx[k]=(int)c->q[(k+offset)%4].x;
        qy[k]=(int)c->q[(k+offset)%4].y;
        if(qx[k]<0||qx[k]>=W||qy[k]<0||qy[k]>=H)return 0;
    }
    for(k=0;k<64;k++)frac[k]=(k*256u+31u)/63u;
    for(oy=0;oy<64;oy++) {
        int ty=(int)frac[oy];
        int lx=(qx[0]<<8)+(qx[3]-qx[0])*ty;
        int ly=(qy[0]<<8)+(qy[3]-qy[0])*ty;
        int rx=(qx[1]<<8)+(qx[2]-qx[1])*ty;
        int ry=(qy[1]<<8)+(qy[2]-qy[1])*ty;
        for(ox=0;ox<64;ox++) {
            int tx=(int)frac[ox];
            int xfp=lx+(((rx-lx)*tx+((rx>=lx)?128:-128))>>8);
            int yfp=ly+(((ry-ly)*tx+((ry>=ly)?128:-128))>>8);
            int ix,iy; unsigned fx,fy,p00,p01,p10,p11,top,bot,value;
            if(xfp<0||yfp<0||xfp>((W-1)<<8)||yfp>((H-1)<<8))return 0;
            ix=xfp>>8; iy=yfp>>8; fx=(unsigned)xfp&255u; fy=(unsigned)yfp&255u;
            if(ix>=W-1){ix=W-2;fx=255u;} if(iy>=H-1){iy=H-2;fy=255u;}
            p00=gray[iy*W+ix];p01=gray[iy*W+ix+1];p10=gray[(iy+1)*W+ix];p11=gray[(iy+1)*W+ix+1];
            top=p00*(256u-fx)+p01*fx; bot=p10*(256u-fx)+p11*fx;
            value=(top*(256u-fy)+bot*fy+32768u)>>16;
            crop64[oy*64u+ox]=(uint8_t)value;
        }
    }
    memcpy(corners,c->q,sizeof(corners));
    return 1;
}

static int board_warp(unsigned cell,const CardQuad *c)
{
    if(cell>=BOARD_ACTIVE_CELLS||!board_rectify(c))return 0;
    memcpy(board_crops[cell],crop64,4096); board_record(cell,c); return 1;
}

static void board_init(void)
{
    unsigned i;
    for(i=0;i<16;i++) {
        BoardROI *r=&board_roi[i];
        r->x0=GRID_X0+(int)(i%4)*GRID_STEP_X;
        r->y0=GRID_Y0+(int)(i/4)*GRID_STEP_Y;
        r->x1=r->x0+GRID_SLOT_WIDTH-1; r->y1=r->y0+GRID_SLOT_HEIGHT-1;
    }
    board_roi[BOARD_DRAW_CELL].x0=GRID_DRAW_X0;
    board_roi[BOARD_DRAW_CELL].y0=GRID_DRAW_Y0;
    board_roi[BOARD_DRAW_CELL].x1=GRID_DRAW_X0+GRID_SLOT_WIDTH-1;
    board_roi[BOARD_DRAW_CELL].y1=GRID_DRAW_Y0+GRID_SLOT_HEIGHT-1;
    memset(board_detected,0,sizeof(board_detected));
    memset(board_detect_state,0,sizeof(board_detect_state));
    memset(board_meta,0,sizeof(board_meta)); memset(board_crops,0,sizeof(board_crops));
    board_ready=1;
}

/* Scan the 4x5 physical extent, but only activate the 4x4 board plus D5/DRAW. */
/* Local contrast threshold: unlike the global Otsu cutoff, this adapts to
 * shadowed individual cells.  Calculated only for missed cells. */
static int board_local_threshold(BoardROI r,int global_t)
{
    unsigned hist[256]={0},total=0,lo=0,hi=255,sum=0,v;
    int x,y,t;
    for(y=r.y0;y<=r.y1;y+=2)for(x=r.x0;x<=r.x1;x+=2) {
        hist[gray[y*W+x]]++;total++;
    }
    for(v=0;v<256;v++) {sum+=hist[v];if(sum>=total/5u){lo=v;break;}}
    sum=0;
    for(v=0;v<256;v++) {sum+=hist[v];if(sum>=(total*85u)/100u){hi=v;break;}}
    if(hi<=lo+18u)return -1; /* insufficient separation */
    t=(int)(lo+(hi-lo)*35u/100u);
    if(t>=global_t-5)t=global_t-8;
    if(t<18)t=18;
    return t;
}

/* Retry ONLY empty cells, never overwrite a strong global detection.  A
 * local search ROI can include a neighbour: centre ownership still governs. */
static void board_retry_dim_slots(int global_t,unsigned active_cells)
{
    unsigned cell;
    for(cell=0;cell<active_cells;cell++) {
        BoardROI r=board_roi[cell];
        CardQuad fallback[8];unsigned n,k,matched=0;int t;
        if(board_detect_state[cell]!=BOARD_SLOT_BAD)continue;
        r.x0-=9;r.y0-=9;r.x1+=9;r.y1+=9;
        if(r.x0<2)r.x0=2;
        if(r.y0<2)r.y0=2;
        if(r.x1>=W-2)r.x1=W-3;
        if(r.y1>=H-2)r.y1=H-3;
        t=board_local_threshold(r,global_t);
        if(t<0)continue;
        board_border_min_hits=16;
        n=board_components(r,t,fallback,8u);
        board_border_min_hits=24;
        if(n>8u)n=8u;
        for(k=0;k<n;k++) {
            int col,row,assigned=-1;
            if(fallback[k].cx<GRID_X0||fallback[k].cy<GRID_Y0)continue;
            col=((int)fallback[k].cx-GRID_X0)/GRID_STEP_X;
            row=((int)fallback[k].cy-GRID_Y0)/GRID_STEP_Y;
            if(row>=0&&row<4&&col>=0&&col<4)assigned=row*4+col;
            else if(row==BOARD_DRAW_ROW&&col==4)assigned=BOARD_DRAW_CELL;
            if(assigned!=(int)cell)continue;
            if(matched++==0u)board_detected[cell]=fallback[k];
        }
        if(matched==1u)board_detect_state[cell]=BOARD_SLOT_GOOD;
        else if(matched>1u)board_detect_state[cell]=BOARD_SLOT_AMBIG;
    }
}

/* Run the connected-component detector in either the 4x4 board region or
 * the full 4x4+DRAW region.  Round-start joker checks use board-only mode:
 * they never search, classify, or update the DRAW slot. */
static unsigned board_scan_frame_region(int threshold,int include_draw)
{
    BoardROI r; CardQuad *found=board_component_scratch;
    unsigned n,k,good=0;
    unsigned active_cells=include_draw?BOARD_ACTIVE_CELLS:BOARD_CELLS;
    int grid_x1=GRID_X0+(include_draw?5:4)*GRID_STEP_X;
    int grid_y1=GRID_Y0+4*GRID_STEP_Y;
    if(!board_ready)return 0;
    memset(board_detected,0,sizeof(board_detected));
    memset(board_detect_state,0,sizeof(board_detect_state));
    r.x0=GRID_X0-BOARD_SEARCH_MARGIN;if(r.x0<0)r.x0=0;
    r.y0=GRID_Y0-BOARD_SEARCH_MARGIN;if(r.y0<0)r.y0=0;
    r.x1=grid_x1-1+BOARD_SEARCH_MARGIN;if(r.x1>=W)r.x1=W-1;
    r.y1=grid_y1-1+BOARD_SEARCH_MARGIN;if(r.y1>=H)r.y1=H-1;
    n=board_components(r,threshold,found,BOARD_MAX_COMPONENTS);
    if(n>BOARD_MAX_COMPONENTS)n=BOARD_MAX_COMPONENTS;
    for(k=0;k<n;k++) {
        int col,row,cell=-1;
        if(found[k].cx<GRID_X0||found[k].cx>=grid_x1||found[k].cy<GRID_Y0||found[k].cy>=grid_y1)continue;
        col=((int)found[k].cx-GRID_X0)/GRID_STEP_X;
        row=((int)found[k].cy-GRID_Y0)/GRID_STEP_Y;
        if(row>=0&&row<4&&col>=0&&col<4)cell=row*4+col;
        else if(include_draw&&row==BOARD_DRAW_ROW&&col==4)cell=BOARD_DRAW_CELL;
        else continue; /* DRAW excluded in board-only mode; HG cells unused */
        if(board_detect_state[cell]==BOARD_SLOT_BAD) {
            board_detected[cell]=found[k]; board_detect_state[cell]=BOARD_SLOT_GOOD;
        } else board_detect_state[cell]=BOARD_SLOT_AMBIG;
    }
    board_retry_dim_slots(threshold,active_cells);
    good=0;
    for(k=0;k<active_cells;k++)if(board_detect_state[k]==BOARD_SLOT_GOOD)good++;
    return good;
}

/* Original full camera detection remains available for DRAW and manual A. */
static unsigned board_scan_frame(int threshold)
{
    return board_scan_frame_region(threshold,1);
}

/* Board-only detection used for all incoming-joker checks. */
static unsigned board_scan_board_only(int threshold)
{
    return board_scan_frame_region(threshold,0);
}

static int board_locate(unsigned cell,int threshold,CardQuad *card)
{
    (void)threshold;
    if(cell>=BOARD_ACTIVE_CELLS||!board_ready||board_detect_state[cell]!=BOARD_SLOT_GOOD)return 0;
    *card=board_detected[cell]; return 1;
}

static unsigned board_prepare_all(int threshold)
{
    unsigned i,valid=0; CardQuad c;
    memset(board_meta,0,sizeof(board_meta)); memset(board_crops,0,sizeof(board_crops));
    (void)board_scan_frame(threshold);
    for(i=0;i<BOARD_ACTIVE_CELLS;i++) {
        if(board_locate(i,threshold,&c)&&board_warp(i,&c))valid++;
        else if(board_detect_state[i]==BOARD_SLOT_GOOD)board_detect_state[i]=BOARD_SLOT_BAD;
    }
    return valid;
}

static int board_crop_cell(unsigned cell,int threshold)
{
    CardQuad c;
    if(cell>=BOARD_ACTIVE_CELLS||!board_ready)return 0;
    memset(board_crops[cell],0,4096); memset(board_meta[cell],0,sizeof(board_meta[cell]));
    if(!board_locate(cell,threshold,&c))return 0;
    return board_warp(cell,&c);
}

static void board_slot_corners(unsigned cell)
{
    unsigned k;
    if(cell>=BOARD_ACTIVE_CELLS)return;
    for(k=0;k<4;k++) {
        corners[k].x=(double)board_meta[cell][1+2*k];
        corners[k].y=(double)board_meta[cell][2+2*k];
    }
}

/* High-resolution crop of a detected card (DRAW): the quad found in the 480x270
 * snapshot is mapped into the hires[] window (snapshot pixel x = source x*4) and
 * warped exactly like board_rectify, but every output pixel is the AVERAGE of a
 * 4x4 grid of bilinear samples (area averaging, like the cv2 INTER_AREA training
 * crops) instead of one sample, so the corner rank/suit symbols are not aliased.
 * Writes board_crops[cell] only on success. */
static unsigned hires_gray(int x,int y)
{
    uint32_t p=hires[(unsigned)y*(hires_w>>HIRES_STEP)+(unsigned)x];
    return (77u*((p>>16)&255u)+150u*((p>>8)&255u)+29u*(p&255u)+128u)>>8;
}
static int board_hires_crop(unsigned cell)
{
    static uint8_t out[64*64];
    const CardQuad *c;double qx[4],qy[4];unsigned k,offset,ox,oy;
    int bw=(int)(hires_w>>HIRES_STEP),bh=(int)(hires_h>>HIRES_STEP);
    if(cell>=BOARD_ACTIVE_CELLS||board_detect_state[cell]!=BOARD_SLOT_GOOD)return 0;
    c=&board_detected[cell];
    offset=c->landscape?(BOARD_LANDSCAPE_ROTATION==1?3u:1u):0u;
    for(k=0;k<4;k++) {
        qx[k]=(c->q[(k+offset)%4].x*4.0-(double)hires_x0)/(double)(1u<<HIRES_STEP);
        qy[k]=(c->q[(k+offset)%4].y*4.0-(double)hires_y0)/(double)(1u<<HIRES_STEP);
    }
    for(oy=0;oy<64;oy++)for(ox=0;ox<64;ox++) {
        unsigned sx,sy,acc=0;
        for(sy=0;sy<4;sy++)for(sx=0;sx<4;sx++) {
            double u=((double)ox+((double)sx-1.5)/4.0)/63.0,v=((double)oy+((double)sy-1.5)/4.0)/63.0;
            double lx,ly,rx,ry,x,y,fx,fy;int ix,iy;
            u=u<0?0:u>1?1:u;
            v=v<0?0:v>1?1:v;
            lx=qx[0]+(qx[3]-qx[0])*v; ly=qy[0]+(qy[3]-qy[0])*v;
            rx=qx[1]+(qx[2]-qx[1])*v; ry=qy[1]+(qy[2]-qy[1])*v;
            x=lx+(rx-lx)*u; y=ly+(ry-ly)*u;
            if(x<0||y<0||x>bw-1||y>bh-1)return 0;       /* card not inside the window */
            ix=(int)x; iy=(int)y;
            if(ix>=bw-1)ix=bw-2;
            if(iy>=bh-1)iy=bh-2;
            fx=x-ix; fy=y-iy;
            acc+=(unsigned)((1-fy)*((1-fx)*hires_gray(ix,iy)+fx*hires_gray(ix+1,iy))+
                            fy*((1-fx)*hires_gray(ix,iy+1)+fx*hires_gray(ix+1,iy+1))+0.5);
        }
        out[oy*64u+ox]=(uint8_t)((acc+8u)/16u);
    }
    memcpy(board_crops[cell],out,sizeof(out));
    return 1;
}

/* Capture a window around the detected card at 2x resolution and re-crop it.
 * Keeps the normal crop if the window cannot be captured (old SOF, error). */
static int board_hires_refine(unsigned cell)
{
    const CardQuad *c;unsigned m=1u<<HIRES_STEP,x0,y0,x1,y1;
    if(!hires_supported||cell>=BOARD_ACTIVE_CELLS||board_detect_state[cell]!=BOARD_SLOT_GOOD)return 0;
    c=&board_detected[cell];
    x0=(unsigned)(c->xmin>4?c->xmin-4:0)*4u; y0=(unsigned)(c->ymin>4?c->ymin-4:0)*4u;
    x1=(unsigned)(c->xmax+5)*4u;            y1=(unsigned)(c->ymax+5)*4u;
    if(x1>W*4u)x1=W*4u;
    if(y1>H*4u)y1=H*4u;
    x0&=~(m-1u);y0&=~(m-1u);x1=(x1+m-1u)&~(m-1u);y1=(y1+m-1u)&~(m-1u);
    if(x1-x0>480u)return 0;                  /* keep the IP FIFO well within its limit */
    if(!snapshot_window(x0,y0,x1-x0,y1-y0))return 0;
    return board_hires_crop(cell);
}

/* Reject only gross exposure failures; do not normalize pixels because the CNN
 * was trained on the existing grayscale pipeline. */
static int board_crop_exposure_ok(const uint8_t *im)
{
    /* A uniformly dark/saturated crop is invalid; do NOT demand hundreds of
     * pixels above an absolute brightness (e.g. 165) because that rejected
     * good card crops in shadowed board regions before CNN could run. */
    unsigned i,minv=255u,maxv=0u; unsigned long sum=0;
    for(i=0;i<4096u;i++) {
        unsigned v=im[i];sum+=v;
        if(v<minv)minv=v;
        if(v>maxv)maxv=v;
    }
    if(sum/4096u<35u || sum/4096u>250u)return 0;
    if(maxv-minv<32u)return 0;
    return 1;
}

/* V16.4.2: Corner-index RGB colour cue, NOT whole-card redness.
 * J/Q illustrations can contain red/yellow regardless of suit. Read just the
 * two small corner index areas in the rectified, portrait-oriented image.
 * Source RGB is sampled directly from the original 480x270 frame, so the CNN
 * grayscale pixels and their upsampled interpolation do not fabricate colour.
 * The card must have a good detected quad. Return UINT32_MAX if there are too
 * few index-ink pixels to determine colour. Such an unknown cue never overrides
 * the CNN majority in the caller.
 *
 * If using a deck with different index geometry, tune the two ROI loops below.
 */
#define BOARD_PIP_COLOUR_UNKNOWN UINT32_MAX
#define BOARD_PIP_RED_THRESHOLD   1700u /* >=17% of dark index ink is red */
#define BOARD_PIP_BLACK_THRESHOLD  250u /* <=2.5% of dark index ink is red */

static unsigned board_corner_pip_red_score(unsigned cell)
{
    const CardQuad *c;
    int xq[4],yq[4];
    unsigned offset,red=0,ink=0,k;
    int corner,px,py;
    if(cell>=BOARD_ACTIVE_CELLS || board_detect_state[cell]!=BOARD_SLOT_GOOD)
        return BOARD_PIP_COLOUR_UNKNOWN;
    c=&board_detected[cell];
    offset=c->landscape?(BOARD_LANDSCAPE_ROTATION==1?3u:1u):0u;
    for(k=0;k<4u;k++) {
        xq[k]=(int)c->q[(k+offset)%4u].x;
        yq[k]=(int)c->q[(k+offset)%4u].y;
    }
    /* Both printed card indexes: upper-left and upside-down lower-right.
     * x is deliberately confined to the outer index strip; do not sample
     * centre illustrations, which contain bright red clothing on Q/J cards. */
    for(corner=0;corner<2;corner++)
    for(py=5;py<=25;py+=2)
    for(px=4;px<=18;px+=2) {
        int u=corner?(63-px):px,v=corner?(63-py):py;
        int tx=(u*256+31)/63,ty=(v*256+31)/63;
        int lx=(xq[0]<<8)+(xq[3]-xq[0])*ty;
        int ly=(yq[0]<<8)+(yq[3]-yq[0])*ty;
        int rx=(xq[1]<<8)+(xq[2]-xq[1])*ty;
        int ry=(yq[1]<<8)+(yq[2]-yq[1])*ty;
        int sx=(lx+(((rx-lx)*tx+((rx>=lx)?128:-128))>>8)+128)>>8;
        int sy=(ly+(((ry-ly)*tx+((ry>=ly)?128:-128))>>8)+128)>>8;
        uint32_t pix;
        unsigned r,g,bb,hi,lo,luma;
        if(sx<0||sy<0||sx>=W||sy>=H)continue;
        pix=frame[sy*W+sx];
        r=(pix>>16)&255u;g=(pix>>8)&255u;bb=pix&255u;
        hi=r>g?r:g;if(bb>hi)hi=bb;
        lo=r<g?r:g;if(bb<lo)lo=bb;
        luma=(77u*r+150u*g+29u*bb+128u)>>8;
        /* Remove the bright paper and the mild red tint caused by lighting.
         * Ink is darker or strongly coloured; near-white pixels never vote. */
        if(luma>=195u && hi-lo<45u)continue;
        if(luma>=225u)continue;
        ink++;
        if(r>=65u && r>g+27u && r>bb+22u &&
           r*10u>g*14u && r*10u>bb*13u)red++;
    }
    if(ink<10u)return BOARD_PIP_COLOUR_UNKNOWN;
    return (red*10000u+ink/2u)/ink;
}

/* Compatibility name for existing draw voting; changed semantics in V16.4.2. */
static unsigned board_red_score(unsigned cell)
{
    return board_corner_pip_red_score(cell);
}

#endif
