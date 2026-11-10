/* Exercise actual static engine paths, without replacing production drawing. */
#include "../main/native/native_ui.c"
static uint16_t actual[1400*48], expected[1400*48];
static unsigned main_calls,post_calls,all_calls;
static void audit(nui_event_t *e){
    if(nui_event_get_code(e)==NUI_EVENT_DRAW_MAIN)main_calls++;
    if(nui_event_get_code(e)==NUI_EVENT_DRAW_POST)post_calls++;
    (*(unsigned*)nui_event_get_user_data(e))++;
}
static void callbacks(void){
    assert(nui_native_init(1400,48,actual));nui_obj_t *root=nui_screen_active();
    nui_obj_t *early=nui_obj_create(root);nui_obj_set_size(early,1400,48);
    nui_obj_add_flag(early,NUI_OBJ_FLAG_PRESSED_PAINT_INERT);
    nui_obj_t *child=nui_obj_create(early);nui_obj_set_size(child,1400,48);
    unsigned m=0,p=0;main_calls=post_calls=all_calls=0;
    nui_obj_add_event_cb(child,audit,NUI_EVENT_DRAW_MAIN,&m);
    nui_obj_add_event_cb(child,audit,NUI_EVENT_DRAW_POST,&p);
    nui_obj_add_event_cb(child,audit,NUI_EVENT_ALL,&all_calls);
    nui_obj_t *cover=nui_obj_create(root);nui_obj_set_size(cover,1400,48);
    nui_obj_set_style_bg_opa(cover,255,0);nui_obj_set_style_bg_color(cover,0x123456,0);
    nui_native_render();assert(m==1&&p==1&&all_calls==2&&main_calls==2&&post_calls==2);
    memcpy(expected,actual,sizeof(actual));
    nui_obj_add_flag(early,NUI_OBJ_FLAG_OCCLUSION_PAINT_INERT);nui_obj_invalidate(root);nui_native_render();
    assert(m==1&&p==1&&all_calls==2&&!memcmp(actual,expected,sizeof(actual)));
    nui_obj_remove_flag(early,NUI_OBJ_FLAG_OCCLUSION_PAINT_INERT);nui_obj_invalidate(root);nui_native_render();
    assert(m==2&&p==2&&all_calls==4&&!memcmp(actual,expected,sizeof(actual)));
    nui_native_deinit();puts("FINAL_CALLBACK PASS unknown subtree MAIN/POST/ALL under opaque cover; press flag insufficient; explicit occlusion opt-in and removal exact");
}
static uint16_t blend_reference(uint16_t dst,uint16_t src,unsigned alpha){
    if(alpha>=255||dst==src)return src;if(!alpha)return dst;
    unsigned m=(alpha+4)>>3,i=32-m;
    return (uint16_t)((((((src>>11)&31)*m+((dst>>11)&31)*i)>>5)<<11)|
        (((((src>>5)&63)*m+((dst>>5)&63)*i)>>5)<<5)|(((src&31)*m+(dst&31)*i)>>5));
}
static void spans(void){
    const unsigned widths[]={1,3,16,31,84,127,512,1280};const uint16_t colors[]={0x0000,0xffff,0x1234};unsigned cases=0;
    assert(nui_native_init(1400,48,actual));
    for(unsigned c=0;c<3;c++)for(unsigned w=0;w<8;w++)for(unsigned offset=0;offset<4;offset++)for(unsigned mode=0;mode<3;mode++){
        unsigned opa=mode==2?128:255,layer_opa=mode==1?127:255;
        nui_layer_t layer={{(int)offset,1,(int)(offset+widths[w]-1),6},(uint8_t)layer_opa};
        for(unsigned rect=0;rect<2;rect++){
            memset(actual,0x77,sizeof(actual));memset(expected,0x77,sizeof(expected));
            if(rect)rectangle(&layer,(nui_area_t){(int)offset-2,0,(int)(offset+widths[w]+1),7},0,colors[c],opa);
            else span(&layer,(int)offset-2,(int)(offset+widths[w]+1),3,colors[c],opa);
            for(unsigned y=rect?1:3;y<=(rect?6:3);y++)for(unsigned x=offset;x<offset+widths[w];x++)expected[y*1400+x]=blend_reference(0x7777,colors[c],opa*layer_opa/255);
            assert(!memcmp(actual,expected,sizeof(actual)));cases++;
        }
    }
    nui_native_deinit();printf("FINAL_SPAN PASS %u exact span/rectangle cases widths including84 offsets0..3 colors0000/FFFF/1234 opaque/inherited/partial; untouched pixels exact\n",cases);
}
static void clipped_glyph(void){
    assert(nui_native_init(1400,48,actual));memset(actual,0x52,sizeof(actual));memcpy(expected,actual,sizeof(actual));
    nui_font_glyph_dsc_t d;assert(nui_font_get_glyph_dsc(NUI_FONT_DEFAULT,&d,'W',0)&&d.bpp==4);
    nui_layer_t layer={{0,0,1399,47},255};glyph_palette_t palette;glyph_palette_init(&palette,0xffff,255,255);
    const int x[]={-1000000,1000000,-100000,100000};
    for(unsigned i=0;i<4;i++)glyph_draw(&layer,NUI_FONT_DEFAULT,'W',0,x[i],0,nui_color_hex(0xffffff),255,&palette);
    assert(!memcmp(actual,expected,sizeof(actual)));nui_native_deinit();
    puts("FINAL_GLYPH PASS real4bpp entirely left/right lo>hi framebuffer unchanged");
}
int main(void){callbacks();spans();clipped_glyph();return 0;}
