/* Production glyph AA regression: synthetic bpp1/2/4/8 font fixtures and
 * independent four-coverage RGB565 math. No snapshots or git history needed.
 * The white-box include exposes the real fast palette and generic glyph path. */
#ifndef TEXT_AA_RENDERER
#define TEXT_AA_RENDERER "../tab5/main/native/native_ui.c"
#endif
#include TEXT_AA_RENDERER
enum { TW=32, TH=24, GW=17, GH=16 };
static uint16_t frame[TW*TH],old[TW*TH],seed[TW*TH],saved[TW*TH];
typedef struct {unsigned bpp;uint8_t bitmap[GW*GH];} fixture_t;
static bool description(const nui_font_t *f,nui_font_glyph_dsc_t *d,uint32_t cp,uint32_t next){
    (void)cp;fixture_t *s=f->user_data;
    *d=(nui_font_glyph_dsc_t){.adv_w=23+(next=='B'),.box_w=GW,.box_h=GH,.ofs_x=-2,.ofs_y=1,.bpp=s->bpp,.resolved_font=f};return true;
}
static const uint8_t *bitmap(const nui_font_t *f,uint32_t cp){(void)cp;return ((fixture_t*)f->user_data)->bitmap;}
static unsigned reference_q(unsigned c){return c<43?0:c<128?85:c<213?170:255;}
static uint16_t reference_blend(uint16_t d,uint16_t s,unsigned a){
    unsigned m=(a+4)>>3,v=32-m;
    return (uint16_t)((((((s>>11)&31)*m+((d>>11)&31)*v)>>5)<<11)|
        (((((s>>5)&63)*m+((d>>5)&63)*v)>>5)<<5)|(((s&31)*m+(d&31)*v)>>5));
}
static unsigned value(unsigned i,unsigned bpp,bool equivalent){
    return equivalent?(i%16)*(bpp==8?17:1):i%((1u<<bpp));
}
static void prepare(fixture_t *s,unsigned bpp,bool equivalent){
    memset(s,0,sizeof(*s));s->bpp=bpp;
    for(unsigned i=0;i<GW*GH;i++){
        unsigned bit=i*bpp,val=value(i,bpp,equivalent);
        s->bitmap[bit/8]|=(uint8_t)(val<<(8-bpp-bit%8));
    }
}
static unsigned draws,changes;
static FILE *dump;
static void draw(unsigned bpp,unsigned opa,unsigned layer_opa,unsigned clip,unsigned theme,bool equivalent){
    fixture_t fixture;prepare(&fixture,bpp,equivalent);
    nui_font_t f={.get_glyph_dsc=description,.get_glyph_bitmap=bitmap,.line_height=20,.base_line=3,.user_data=&fixture};
    nui_layer_t l={{0,0,TW-1,TH-1},(uint8_t)layer_opa};
    if(clip==1)l.clip=(nui_area_t){7,9,13,15};
    if(clip==2)l.clip=(nui_area_t){25,21,31,23};
    if(clip==3)l.clip=(nui_area_t){10,10,9,9};
    int x=clip==4?-1:6,y=clip==4?-2:4,gx=x-2,gy=y;
    nui_color_t fg=theme?0x1A1D24:0xE8ECF4;uint16_t c=nui_color_to_u16(fg);
    memcpy(frame,seed,sizeof(frame));memcpy(old,seed,sizeof(old));
    glyph_palette_t palette;glyph_palette_init(&palette,c,opa,layer_opa);
    nui_font_glyph_dsc_t before,after;description(&f,&before,'A','B');
    glyph_draw(&l,&f,'A','B',x,y,fg,opa,&palette);description(&f,&after,'A','B');
    assert(before.adv_w==after.adv_w&&before.box_w==after.box_w&&before.box_h==after.box_h);
    assert(before.ofs_x==after.ofs_x&&before.ofs_y==after.ofs_y&&before.bpp==after.bpp);
    assert(fixture.bpp==bpp&&before.adv_w==24);
    assert(glyph_advance(&f,'A','B')==24&&glyph_advance(&f,'A',0)==23);
    unsigned max=(1u<<bpp)-1;
    for(int py=0;py<TH;py++)for(int px=0;px<TW;px++){
        unsigned at=py*TW+px;bool inside=px>=gx&&px<gx+GW&&py>=gy&&py<gy+GH&&
            px>=l.clip.x1&&px<=l.clip.x2&&py>=l.clip.y1&&py<=l.clip.y2;
        uint16_t expected=seed[at];
        if(inside){
            unsigned val=value((py-gy)*GW+px-gx,bpp,equivalent);
            unsigned q=reference_q(val*255/max),a=(opa*q/255)*layer_opa/255;
            expected=reference_blend(seed[at],c,a);
            old[at]=reference_blend(seed[at],c,(opa*val/max)*layer_opa/255);
            if(!val)assert(frame[at]==seed[at]);
        }else assert(frame[at]==seed[at]);
        assert(frame[at]==expected);
        if(frame[at]!=old[at]){assert(inside);changes++;}
    }
    draws++;
    if(dump)assert(fwrite(frame,sizeof(frame),1,dump)==1);
}
int main(int argc,char **argv){
    if(argc==2){dump=fopen(argv[1],"wb");assert(dump);}
    unsigned seen=0,previous=0;
    for(unsigned c=0;c<256;c++){
        unsigned q=glyph_coverage4(c);assert(q==reference_q(c));assert(q>=previous);previous=q;
        assert(q==0||q==85||q==170||q==255);seen|=1u<<(q/85);
    }
    assert(seen==15);assert(nui_native_init(TW,TH,frame));
    const unsigned opas[]={0,1,42,85,128,170,254,255},bpps[]={1,2,4,8};
    for(unsigned theme=0;theme<2;theme++){
        for(unsigned i=0;i<TW*TH;i++)seed[i]=(uint16_t)(theme?0xffff-i*13:0x1234+i*17);
        for(unsigned a=0;a<8;a++)for(unsigned b=0;b<8;b++)for(unsigned clip=0;clip<5;clip++){
            for(unsigned d=0;d<4;d++)draw(bpps[d],opas[a],opas[b],clip,theme,false);
            /* Equal normalized samples: real fast 4bpp vs real slow 8bpp. */
            draw(4,opas[a],opas[b],clip,theme,true);memcpy(saved,frame,sizeof(frame));
            draw(8,opas[a],opas[b],clip,theme,true);assert(!memcmp(saved,frame,sizeof(frame)));
        }
    }
    assert(changes>0);nui_native_deinit();assert(!n.stats.allocation_count&&!n.stats.allocation_bytes);
    if(dump)fclose(dump);
    printf("PASS four levels 0/85/170/255, 256 coverage inputs, %u draws: bpp1/2/4/8, both themes, opacity+clip; fast4 == slow8; %u expected changed pixels vs old AA, only inside glyph masks\n",draws,changes);
    return 0;
}
