


#include <lib/png.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <types.h>


typedef struct { const uint8_t *b; uint32_t p,n,bits; int nb; } BR;

static void br_init(BR *r,const uint8_t *b,uint32_t n){r->b=b;r->p=0;r->n=n;r->bits=0;r->nb=0;}

static uint32_t br_read(BR *r,int n){
    while(r->nb<n&&r->p<r->n) r->bits|=(uint32_t)r->b[r->p++]<<r->nb,r->nb+=8;
    uint32_t v=r->bits&((1u<<n)-1); r->bits>>=n; r->nb-=n; return v;
}


static uint8_t br_byte(BR *r){
    if(r->nb>=8){uint8_t b=(uint8_t)r->bits;r->bits>>=8;r->nb-=8;return b;}
    r->bits=0;r->nb=0;
    return r->p<r->n?r->b[r->p++]:0;
}

static void br_align(BR *r){int s=r->nb&7;r->bits>>=s;r->nb-=s;}

static uint16_t br_le16(BR *r){uint8_t a=br_byte(r),b=br_byte(r);return(uint16_t)(a|(b<<8));}


#define HMAXSYM 320
typedef struct{int ml;uint16_t first[16],cnt[16];int base[16];uint16_t s[HMAXSYM];}HT;

static void ht_build(HT *h,const uint8_t *lens,int n){
    int i;uint16_t code=0,cnt[16]={0};
    for(i=0;i<n;i++) if(lens[i])cnt[lens[i]]++;
    h->ml=0;
    for(i=1;i<16;i++) if(cnt[i])h->ml=i;
    int st[16]={0},idx[16]={0};
    for(i=2;i<16;i++) st[i]=st[i-1]+cnt[i-1];
    for(i=0;i<n;i++) if(lens[i])h->s[st[lens[i]]+idx[lens[i]]++]=(uint16_t)i;
    code=0;
    for(i=1;i<16;i++){h->first[i]=code;h->cnt[i]=cnt[i];h->base[i]=st[i]-code;code=(uint16_t)((code+cnt[i])<<1);}
}
static uint16_t ht_dec(HT *h,BR *r){
    int code=0,i;
    for(i=1;i<=h->ml;i++){
        code=(code<<1)|(int)br_read(r,1);
        if((uint16_t)(code-h->first[i])<h->cnt[i]) return h->s[h->base[i]+code];
    }
    return 0xFFFF;
}


static const uint8_t  LX[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t LB[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t  DX[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static const uint16_t DB[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};

static void fixed_huff(HT *hl,HT *hd){
    uint8_t ls[288];int i;
    for(i=0;i<144;i++)ls[i]=8;for(;i<256;i++)ls[i]=9;
    for(;i<280;i++)ls[i]=7;for(;i<288;i++)ls[i]=8;
    ht_build(hl,ls,288);
    for(i=0;i<30;i++)ls[i]=5;ht_build(hd,ls,30);
}


static uint32_t inflate(const uint8_t *src,uint32_t slen,uint8_t *dst,uint32_t dlen){
    BR r;br_init(&r,src,slen);
    
    uint8_t flg=0;br_byte(&r);flg=br_byte(&r);
    if(flg&0x20){br_byte(&r);br_byte(&r);br_byte(&r);br_byte(&r);}
    uint32_t dp=0;int bfinal=0;
    while(!bfinal){
        bfinal=(int)br_read(&r,1);
        int btype=(int)br_read(&r,2);
        if(btype==0){
            br_align(&r);
            uint16_t len=br_le16(&r);br_le16(&r);
            while(len--&&dp<dlen)dst[dp++]=br_byte(&r);
        }else{
            HT hl,hd;
            if(btype==1){fixed_huff(&hl,&hd);}
            else{
                int hlit=(int)br_read(&r,5)+257,hdist=(int)br_read(&r,5)+1,hclen=(int)br_read(&r,4)+4;
                static const int CLO[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                uint8_t cl[19]={0};int i;
                for(i=0;i<hclen;i++)cl[CLO[i]]=(uint8_t)br_read(&r,3);
                HT hcl;ht_build(&hcl,cl,19);
                uint8_t lens[320]={0};int k=0,tot=hlit+hdist;
                while(k<tot){
                    uint16_t s=ht_dec(&hcl,&r);
                    if(s<16){lens[k++]=(uint8_t)s;}
                    else if(s==16){uint8_t rep=k?lens[k-1]:0;int c=(int)br_read(&r,2)+3;while(c--&&k<tot)lens[k++]=rep;}
                    else if(s==17){int c=(int)br_read(&r,3)+3;while(c--&&k<tot)lens[k++]=0;}
                    else{int c=(int)br_read(&r,7)+11;while(c--&&k<tot)lens[k++]=0;}
                }
                ht_build(&hl,lens,hlit);ht_build(&hd,lens+hlit,hdist);
            }
            for(;;){
                uint16_t sym=ht_dec(&hl,&r);
                if(sym<256){if(dp<dlen)dst[dp++]=(uint8_t)sym;}
                else if(sym==256)break;
                else{
                    int li=sym-257,len=(int)LB[li]+(int)br_read(&r,LX[li]);
                    int di=(int)ht_dec(&hd,&r),dist=(int)DB[di]+(int)br_read(&r,DX[di]);
                    int back=(int)dp-dist;
                    while(len--&&dp<dlen){dst[dp]=back>=0?dst[back]:0;dp++;back++;}
                }
            }
        }
    }
    return dp;
}


static uint32_t ru32(const uint8_t *b){return((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];}

static uint8_t paeth(int a,int b,int c){
    int p=a+b-c,pa=p-a,pb=p-b,pc=p-c;
    if(pa<0)pa=-pa;if(pb<0)pb=-pb;if(pc<0)pc=-pc;
    return(pa<=pb&&pa<=pc)?(uint8_t)a:(pb<=pc)?(uint8_t)b:(uint8_t)c;
}


uint32_t *png_decode(const uint8_t *data,uint32_t size,int *ow,int *oh){
    static const uint8_t SIG[8]={137,80,78,71,13,10,26,10};
    if(size<33||memcmp(data,SIG,8)!=0)return 0;

    uint32_t w=0,h=0;uint8_t ctype=0,depth=0,interlace=0;
    uint8_t *idat=0;uint32_t ilen=0,icap=0;

    uint32_t p=8;
    while(p+12<=size){
        uint32_t clen=ru32(data+p);p+=4;
        const uint8_t *tp=data+p;p+=4;
        if(clen>size-p)break;
        if(memcmp(tp,"IHDR",4)==0&&clen>=13){
            w=ru32(data+p);h=ru32(data+p+4);
            depth=data[p+8];ctype=data[p+9];interlace=data[p+12];
        }else if(memcmp(tp,"IDAT",4)==0){
            if(ilen+clen>icap){
                uint32_t nc=(ilen+clen+65535u)&~65535u;
                uint8_t *nd=(uint8_t*)kmalloc(nc);
                if(!nd){kfree(idat);return 0;}
                if(idat){memcpy(nd,idat,ilen);kfree(idat);}
                idat=nd;icap=nc;
            }
            memcpy(idat+ilen,data+p,clen);ilen+=clen;
        }else if(memcmp(tp,"IEND",4)==0)break;
        p+=clen+4;
    }

    if(!w||!h||depth!=8||interlace||(ctype!=0&&ctype!=2&&ctype!=6)){kfree(idat);return 0;}
    if(!idat)return 0;

    int bpp=(ctype==6)?4:(ctype==2)?3:1;
    uint32_t stride=w*(uint32_t)bpp,rlen=(stride+1)*h;
    uint8_t *raw=(uint8_t*)kmalloc(rlen);
    if(!raw){kfree(idat);return 0;}
    memset(raw,0,rlen);

    inflate(idat,ilen,raw,rlen);kfree(idat);

    
    uint32_t y,x;
    for(y=0;y<h;y++){
        uint8_t *row=raw+y*(stride+1)+1;
        uint8_t *prev=(y>0)?raw+(y-1)*(stride+1)+1:0;
        uint8_t ftype=raw[y*(stride+1)];
        for(x=0;x<stride;x++){
            uint8_t a=(x>=(uint32_t)bpp)?row[x-bpp]:0;
            uint8_t b2=prev?prev[x]:0;
            uint8_t c=(prev&&x>=(uint32_t)bpp)?prev[x-bpp]:0;
            switch(ftype){
                case 1:row[x]+=a;break;
                case 2:row[x]+=b2;break;
                case 3:row[x]+=(uint8_t)((a+b2)/2);break;
                case 4:row[x]+=paeth(a,b2,c);break;
                default:break;
            }
        }
    }

    uint32_t *pixels=(uint32_t*)kmalloc(w*h*4);
    if(!pixels){kfree(raw);return 0;}

    for(y=0;y<h;y++){
        uint8_t *row=raw+y*(stride+1)+1;
        for(x=0;x<w;x++){
            uint32_t pix;
            if(bpp==4)      pix=((uint32_t)row[x*4]<<16)|((uint32_t)row[x*4+1]<<8)|row[x*4+2];
            else if(bpp==3) pix=((uint32_t)row[x*3]<<16)|((uint32_t)row[x*3+1]<<8)|row[x*3+2];
            else            {uint8_t g=row[x];pix=((uint32_t)g<<16)|((uint32_t)g<<8)|g;}
            pixels[y*w+x]=pix;
        }
    }
    kfree(raw);
    *ow=(int)w;*oh=(int)h;
    return pixels;
}
