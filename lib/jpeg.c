


#include <lib/jpeg.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <types.h>


typedef struct { const uint8_t *b; uint32_t p,n,bits; int nb; } JBR;

static void jbr_init(JBR *r,const uint8_t *b,uint32_t n){r->b=b;r->p=0;r->n=n;r->bits=0;r->nb=0;}

static uint32_t jbr_bits(JBR *r,int n){
    while(r->nb<n&&r->p<r->n){
        uint8_t by=r->b[r->p++];
        if(by==0xFF&&r->p<r->n&&r->b[r->p]==0x00)r->p++;
        r->bits=(r->bits<<8)|by; r->nb+=8;
    }
    r->nb-=n;
    return(r->bits>>r->nb)&((1u<<n)-1);
}


typedef struct{int ml;uint16_t first[17],cnt[17];int base[17];uint8_t sym[256];}JHUF;

static void jhuf_build(JHUF *h,const uint8_t *cnt16,const uint8_t *syms){
    int i,j=0; uint16_t code=0;
    h->ml=0;
    for(i=1;i<=16;i++){
        h->cnt[i]=cnt16[i-1]; h->first[i]=code;
        h->base[i]=j-code;
        for(int k=0;k<cnt16[i-1];k++) h->sym[j++]=(uint8_t)syms[j-j+k]; 
        if(cnt16[i-1]) h->ml=i;
        code=(uint16_t)((code+cnt16[i-1])<<1);
    }
    
    j=0;
    for(i=1;i<=16;i++) for(int k=0;k<cnt16[i-1];k++) h->sym[j++]=syms[j-j+k];
}

static void jhuf_build2(JHUF *h,const uint8_t *cnt16,const uint8_t *syms){
    int i; uint16_t code=0; int j=0;
    h->ml=0;
    for(i=1;i<=16;i++){
        h->cnt[i]=cnt16[i-1]; h->first[i]=code; h->base[i]=j-code;
        if(cnt16[i-1]) h->ml=i;
        code=(uint16_t)((code+cnt16[i-1])<<1);
        j+=cnt16[i-1];
    }
    j=0;
    for(i=1;i<=16;i++) for(int k=0;k<cnt16[i-1];k++) h->sym[j++]=syms[j-j+k];
}

static int jhuf_dec(JHUF *h,JBR *r){
    int code=0,i;
    for(i=1;i<=h->ml;i++){
        code=(code<<1)|(int)jbr_bits(r,1);
        if((uint16_t)(code-h->first[i])<(uint16_t)h->cnt[i])
            return h->sym[h->base[i]+code];
    }
    return -1;
}


static void jhuf_init(JHUF *h,const uint8_t *c,const uint8_t *v){
    int i,k=0; uint16_t code=0;
    memset(h,0,sizeof(*h));
    h->ml=0;
    for(i=1;i<=16;i++){
        h->cnt[i]=c[i-1]; h->first[i]=code; h->base[i]=k-code;
        if(c[i-1]) h->ml=i;
        code=(uint16_t)((code+c[i-1])<<1);
        k+=c[i-1];
    }
    k=0;
    for(i=1;i<=16;i++) for(int j=0;j<(int)c[i-1];j++) h->sym[k++]=v[k-k+j];
    
    k=0;
    for(i=1;i<=16;i++) for(int j=0;j<(int)c[i-1];j++){ h->sym[k]=v[k]; k++; }
}


static int j_smag(int categ,int bits){
    if(categ==0)return 0;
    if(bits&(1<<(categ-1)))return bits;
    return bits-(1<<categ)+1;
}


static const uint8_t ZZ[64]={
     0, 1, 8,16, 9, 2, 3,10,17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34,27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
};


static const int16_t CT[8][8]={
    {1448,1448,1448,1448,1448,1448,1448,1448},
    {2008,1702,1137, 399,-399,-1137,-1702,-2008},
    {1892, 783,-783,-1892,-1892,-783, 783,1892},
    {1702,-399,-2008,-1137,1137,2008, 399,-1702},
    {1448,-1448,-1448,1448,1448,-1448,-1448,1448},
    {1137,-2008, 399,1702,-1702,-399,2008,-1137},
    { 783,-1892,1892,-783,-783,1892,-1892, 783},
    { 399,-1137,1702,-2008,2008,-1702,1137,-399}
};

static void idct_row(const int16_t *F,int32_t *f){
    int x,u;
    for(x=0;x<8;x++){
        int32_t s=0;
        for(u=0;u<8;u++) s+=(int32_t)F[u]*CT[u][x];
        f[x]=(s+2048)>>12;
    }
}

static void idct2d(int16_t blk[8][8]){
    int32_t tmp[8][8]; int i,j;
    
    for(j=0;j<8;j++){
        int16_t col[8]; for(i=0;i<8;i++) col[i]=blk[i][j];
        int32_t out[8]; idct_row(col,out);
        for(i=0;i<8;i++) tmp[i][j]=out[i];
    }
    
    for(i=0;i<8;i++){
        int16_t row[8]; for(j=0;j<8;j++) row[j]=(int16_t)tmp[i][j];
        int32_t out[8]; idct_row(row,out);
        for(j=0;j<8;j++) blk[i][j]=(int16_t)out[j];
    }
}


static uint8_t clamp8(int v){return v<0?0:(v>255?255:(uint8_t)v);}


#define JMAX_COMP 3

typedef struct {
    uint8_t  id,h,v,qt;     
    uint8_t  dct,act;        
    int16_t  dc_pred;
} JComp;

typedef struct {
    int16_t qt[4][64];
    JHUF    dc[2], ac[2];
    JComp   comp[JMAX_COMP];
    int     ncomp;
    uint32_t w,h;
    int     hmax,vmax;
} JCtx;


static void decode_block(JCtx *jc,JBR *br,int ci,int16_t blk[8][8]){
    JComp *c=&jc->comp[ci];
    JHUF  *dh=&jc->dc[c->dct];
    JHUF  *ah=&jc->ac[c->act];
    int16_t *qt=jc->qt[c->qt];
    int i,j;
    memset(blk,0,sizeof(int16_t)*64);

    
    int cat=jhuf_dec(dh,br);
    if(cat<0)return;
    int dc_diff=j_smag(cat,(int)jbr_bits(br,(uint32_t)cat));
    c->dc_pred+=dc_diff;
    blk[0][0]=(int16_t)(c->dc_pred*qt[0]);

    
    int k=1;
    while(k<64){
        int sym=jhuf_dec(ah,br);
        if(sym<0)break;
        if(sym==0){break;} 
        if(sym==0xF0){k+=16;continue;} 
        int run=(sym>>4)&0xF, sz=sym&0xF;
        k+=run;
        if(k>=64)break;
        if(sz>0){
            int ac=j_smag(sz,(int)jbr_bits(br,(uint32_t)sz));
            int pos=ZZ[k];
            blk[pos>>3][pos&7]=(int16_t)(ac*qt[k]);
        }
        k++;
    }
    idct2d(blk);
    
    for(i=0;i<8;i++) for(j=0;j<8;j++) blk[i][j]+=128;
}


static int find_marker(const uint8_t *d,uint32_t p,uint32_t sz,uint8_t *mout){
    while(p+1<sz){
        if(d[p]==0xFF&&d[p+1]!=0x00&&d[p+1]!=0xFF){*mout=d[p+1];return(int)p;}
        p++;
    }
    return -1;
}


uint32_t *jpeg_decode(const uint8_t *data,uint32_t size,int *ow,int *oh){
    if(size<4||data[0]!=0xFF||data[1]!=0xD8)return 0;

    JCtx jc; memset(&jc,0,sizeof(jc));
    uint32_t p=2;
    bool in_scan=false;

    while(p+3<size&&!in_scan){
        if(data[p]!=0xFF){p++;continue;}
        while(p<size&&data[p]==0xFF)p++;
        if(p>=size)break;
        uint8_t mk=data[p++];
        if(mk==0xD9)break; 
        if(mk==0xD8||mk==0x00)continue;
        uint16_t seglen=(uint16_t)(((uint16_t)data[p]<<8)|data[p+1]);
        uint32_t segend=p+(uint32_t)seglen;
        p+=2;

        if(mk==0xDB){ 
            uint32_t q=p;
            while(q<segend){
                uint8_t info=data[q++];
                int ti=info&0xF;
                if(ti>3)break;
                int prec=(info>>4)&1;
                int n=prec?128:64;
                for(int i=0;i<64&&q<segend;i++){
                    uint16_t v=prec?(uint16_t)(((uint16_t)data[q]<<8)|data[q+1]):(uint16_t)data[q];
                    jc.qt[ti][ZZ[i]]=(int16_t)v; q+=prec?2:1;
                }
                (void)n;
            }
        } else if(mk==0xC0){ 
             p++;
            jc.h=(uint32_t)(((uint16_t)data[p]<<8)|data[p+1]);p+=2;
            jc.w=(uint32_t)(((uint16_t)data[p]<<8)|data[p+1]);p+=2;
            jc.ncomp=data[p++];
            if(jc.ncomp>JMAX_COMP)jc.ncomp=JMAX_COMP;
            jc.hmax=1;jc.vmax=1;
            for(int i=0;i<jc.ncomp;i++){
                jc.comp[i].id=data[p++];
                jc.comp[i].h=(data[p]>>4)&0xF;
                jc.comp[i].v=data[p]&0xF; p++;
                jc.comp[i].qt=data[p++];
                if(jc.comp[i].h>jc.hmax)jc.hmax=jc.comp[i].h;
                if(jc.comp[i].v>jc.vmax)jc.vmax=jc.comp[i].v;
            }
        } else if(mk==0xC4){ 
            uint32_t q=p;
            while(q<segend){
                uint8_t info=data[q++];
                int tc=(info>>4)&1,th=info&0xF;
                if(th>1){break;}
                uint8_t cnt[16]; int total=0;
                for(int i=0;i<16;i++){cnt[i]=data[q++];total+=cnt[i];}
                if(total>256||q+total>segend)break;
                const uint8_t *syms=data+q; q+=total;
                JHUF *huf=(tc==0)?&jc.dc[th]:&jc.ac[th];
                jhuf_init(huf,cnt,syms);
            }
        } else if(mk==0xDA){ 
            int ns=data[p++];
            for(int i=0;i<ns;i++){
                uint8_t cid=data[p++];
                uint8_t ht=data[p++];
                for(int j=0;j<jc.ncomp;j++){
                    if(jc.comp[j].id==cid){
                        jc.comp[j].dct=(ht>>4)&0xF;
                        jc.comp[j].act=ht&0xF;
                    }
                }
            }
            p+=3; 
            in_scan=true;
        }
        p=segend;
    }

    if(!jc.w||!jc.h||!in_scan)return 0;
    if(jc.ncomp!=1&&jc.ncomp!=3)return 0;

    
    uint32_t *out=(uint32_t*)kmalloc(jc.w*jc.h*4);
    if(!out)return 0;
    memset(out,0,jc.w*jc.h*4);

    
    int ci;
    for(ci=0;ci<jc.ncomp;ci++) jc.comp[ci].dc_pred=0;

    
    JBR br; jbr_init(&br,data+p,size-p);

    
    int hmcu=jc.hmax*8, vmcu=jc.vmax*8;
    uint32_t mcu_cols=(jc.w+(uint32_t)hmcu-1)/(uint32_t)hmcu;
    uint32_t mcu_rows=(jc.h+(uint32_t)vmcu-1)/(uint32_t)vmcu;

    
    uint32_t pstride=(mcu_cols*(uint32_t)hmcu);
    uint32_t plines =(mcu_rows*(uint32_t)vmcu);
    int16_t *planes[3]={0,0,0};
    for(ci=0;ci<jc.ncomp;ci++){
        planes[ci]=(int16_t*)kmalloc(pstride*plines*2);
        if(!planes[ci]){for(int x=0;x<ci;x++)kfree(planes[x]);kfree(out);return 0;}
        memset(planes[ci],0,pstride*plines*2);
    }

    uint32_t mr,mc;
    for(mr=0;mr<mcu_rows;mr++){
        for(mc=0;mc<mcu_cols;mc++){
            for(ci=0;ci<jc.ncomp;ci++){
                int bh=jc.comp[ci].h,bv=jc.comp[ci].v;
                int bv2,bh2;
                for(bv2=0;bv2<bv;bv2++){
                    for(bh2=0;bh2<bh;bh2++){
                        int16_t blk[8][8];
                        decode_block(&jc,&br,ci,blk);
                        
                        int yscale=jc.vmax/bv, xscale=jc.hmax/bh;
                        uint32_t py=mr*(uint32_t)vmcu+(uint32_t)bv2*8*(uint32_t)yscale;
                        uint32_t px=mc*(uint32_t)hmcu+(uint32_t)bh2*8*(uint32_t)xscale;
                        int bi,bj;
                        for(bi=0;bi<8;bi++){
                            for(bj=0;bj<8;bj++){
                                int16_t v=blk[bi][bj];
                                
                                int si,sj;
                                for(si=0;si<yscale;si++){
                                    for(sj=0;sj<xscale;sj++){
                                        uint32_t ry=py+(uint32_t)bi*(uint32_t)yscale+(uint32_t)si;
                                        uint32_t rx=px+(uint32_t)bj*(uint32_t)xscale+(uint32_t)sj;
                                        if(ry<plines&&rx<pstride)
                                            planes[ci][ry*pstride+rx]=v;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    
    uint32_t x,y;
    for(y=0;y<jc.h;y++){
        for(x=0;x<jc.w;x++){
            uint32_t idx=y*pstride+x;
            uint8_t r2,g2,b2;
            if(jc.ncomp==1){
                uint8_t lum=clamp8(planes[0][idx]);
                r2=g2=b2=lum;
            }else{
                int Y  =planes[0][idx];
                int Cb =planes[1][idx]-128;
                int Cr =planes[2][idx]-128;
                r2=clamp8(Y+(1402*Cr+512)/1024);
                g2=clamp8(Y-(344*Cb+512)/1024-(714*Cr+512)/1024);
                b2=clamp8(Y+(1772*Cb+512)/1024);
            }
            out[y*jc.w+x]=((uint32_t)r2<<16)|((uint32_t)g2<<8)|b2;
        }
    }

    for(ci=0;ci<jc.ncomp;ci++) kfree(planes[ci]);
    *ow=(int)jc.w; *oh=(int)jc.h;
    return out;
}
