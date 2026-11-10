
#include "../native_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned clicks,changes,repeats,ready;
static void clicked(nui_event_t *e){(void)e;clicks++;}
static void changed(nui_event_t *e){(void)e;changes++;}
static void timer_cb(nui_timer_t *t){repeats++;nui_timer_delete(t);}
static void ready_cb(nui_event_t *e){ready++;nui_obj_delete(nui_event_get_target(e));}
static void delete_cb(nui_event_t *e){nui_obj_delete(nui_event_get_target(e));}
static void key(nui_obj_t *kb,const char *name,bool down){nui_area_t a[96];const char *texts[96];size_t n=nui_native_keyboard_get_keys(kb,a,texts,96);for(size_t i=0;i<n;i++)if(!strcmp(name,texts[i])){nui_native_touch((a[i].x1+a[i].x2)/2,(a[i].y1+a[i].y2)/2,down);return;}assert(!"key not found");}
int main(void){
 static uint16_t fb[1280*720];assert(nui_native_init(1280,720,fb));
 nui_obj_t *root=nui_screen_active();nui_obj_t *b=nui_button_create(root);nui_obj_set_pos(b,20,20);nui_obj_set_size(b,100,60);nui_obj_add_event_cb(b,clicked,NUI_EVENT_CLICKED,NULL);
 nui_native_touch(30,30,true);assert(!clicks);nui_native_touch(30,30,false);assert(clicks==1);nui_obj_add_state(b,NUI_STATE_DISABLED);nui_native_touch(30,30,true);nui_native_touch(30,30,false);assert(clicks==1);nui_obj_delete(b);
 nui_timer_create(timer_cb,400,NULL);nui_native_tick(399);assert(!repeats);nui_native_tick(1);assert(repeats==1);nui_native_tick(1000);assert(repeats==1);
 nui_obj_t *ta=nui_textarea_create(root);nui_obj_set_size(ta,500,64);nui_textarea_set_one_line(ta,true);nui_obj_set_style_pad_top(ta,14,0);nui_obj_set_style_pad_bottom(ta,14,0);nui_obj_add_event_cb(ta,changed,NUI_EVENT_VALUE_CHANGED,NULL);
 nui_obj_t *kb=nui_keyboard_create(root);nui_obj_set_pos(kb,0,360);nui_obj_set_size(kb,1280,360);nui_obj_set_style_pad_all(kb,10,0);nui_obj_set_style_pad_column(kb,10,0);nui_obj_set_style_pad_row(kb,10,0);nui_keyboard_set_textarea(kb,ta);
 key(kb,"q",true);assert(!strcmp(nui_textarea_get_text(ta),"q"));key(kb,"q",false);assert(!strcmp(nui_textarea_get_text(ta),"q"));
 key(kb,"q",true);nui_native_tick(399);assert(!strcmp(nui_textarea_get_text(ta),"qq"));nui_native_tick(1);assert(!strcmp(nui_textarea_get_text(ta),"qqq"));nui_native_tick(100);assert(!strcmp(nui_textarea_get_text(ta),"qqqq"));key(kb,"q",false);
 key(kb,NUI_SYMBOL_BACKSPACE,true);assert(!strcmp(nui_textarea_get_text(ta),"qqq"));nui_native_tick(400);assert(!strcmp(nui_textarea_get_text(ta),"qq"));key(kb,NUI_SYMBOL_BACKSPACE,false);
 nui_obj_add_event_cb(kb,ready_cb,NUI_EVENT_READY,NULL);key(kb,NUI_SYMBOL_OK,true);assert(!ready);key(kb,NUI_SYMBOL_OK,false);assert(ready==1);
 nui_textarea_set_text(ta,"АБ");nui_textarea_set_cursor_pos(ta,-1);nui_textarea_delete_char(ta);assert(!strcmp(nui_textarea_get_text(ta),"А"));nui_textarea_add_text(ta,"Я");assert(!strcmp(nui_textarea_get_text(ta),"АЯ"));
 nui_native_render();assert(!nui_native_render());
 nui_obj_add_event_cb(ta,delete_cb,NUI_EVENT_VALUE_CHANGED,NULL);nui_textarea_add_text(ta,"!"); /* deletion from VALUE_CHANGED */
 nui_native_stats_t before;nui_native_profile_snapshot(&before);
 for(unsigned cycle=0;cycle<64;cycle++){nui_obj_t *o=nui_obj_create(root);nui_style_t st;nui_style_init(&st);nui_style_set_bg_opa(&st,255);nui_obj_add_style(o,&st,0);nui_obj_t *l=nui_label_create(o);nui_label_set_text(l,"UTF-8 Я");nui_native_render();nui_obj_delete(o);nui_style_reset(&st);nui_native_render();nui_native_stats_t after;nui_native_profile_snapshot(&after);assert(after.allocation_bytes==before.allocation_bytes&&after.allocation_count==before.allocation_count);}
 nui_native_deinit();nui_native_stats_t final;nui_native_profile_snapshot(&final);assert(final.allocation_bytes==0&&final.allocation_count==0);
 puts("NATIVE_SELFTEST press/release, disabled, timer deletion, 400/100 repeat, UTF-8, event deletion, idle, 64 lifetimes: PASS");return 0;
}
