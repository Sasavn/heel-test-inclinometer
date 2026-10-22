#define main frozen_ui_suite_main
#ifndef NB_FROZEN_TEST
#error explicit frozen include required
#endif
#include NB_FROZEN_TEST
#undef main
LV_FONT_DECLARE(font_tab5_24);
static void dump(const char *context,unsigned index,lv_obj_t *obj){
 assert(obj);lv_obj_update_layout(obj);lv_area_t area;lv_obj_get_coords(obj,&area);
 const lv_font_t *font=lv_obj_get_style_text_font(obj,LV_PART_MAIN);
 printf("{\"context\":\"%s\",\"index\":%u,\"rect\":[%d,%d,%d,%d],\"height\":%d,\"width\":%d,\"pad_left\":%d,\"pad_right\":%d,\"pad_top\":%d,\"pad_bottom\":%d,\"border_width\":%d,\"radius\":%d,\"opa\":%u,\"bg_opa\":%u,\"border_opa\":%u,\"line_height\":%d,\"base_line\":%d,\"font_is_tab5_24\":%s}\n",context,index,area.x1,area.y1,area.x2,area.y2,lv_obj_get_height(obj),lv_obj_get_width(obj),lv_obj_get_style_pad_left(obj,0),lv_obj_get_style_pad_right(obj,0),lv_obj_get_style_pad_top(obj,0),lv_obj_get_style_pad_bottom(obj,0),lv_obj_get_style_border_width(obj,0),lv_obj_get_style_radius(obj,0),lv_obj_get_style_opa(obj,0),lv_obj_get_style_bg_opa(obj,0),lv_obj_get_style_border_opa(obj,0),font->line_height,font->base_line,font==&font_tab5_24?"true":"false");
}
static void json_text(const char *text){
 putchar('"');for(const unsigned char *p=(const unsigned char*)text;*p;p++){
  if(*p=='"'||*p=='\\'){putchar('\\');putchar(*p);}
  else if(*p<32)printf("\\u%04x",*p);else putchar(*p);
 }putchar('"');
}
static void dump_keyboard(const char *context,lv_obj_t *obj){
 assert(obj);pump();lv_obj_update_layout(obj);
 lv_buttonmatrix_t *matrix=(lv_buttonmatrix_t*)obj;lv_area_t area;lv_obj_get_coords(obj,&area);
 unsigned normal_bg=lv_obj_get_style_bg_opa(obj,LV_PART_ITEMS);
 unsigned item_radius=lv_obj_get_style_radius(obj,LV_PART_ITEMS),item_border=lv_obj_get_style_border_width(obj,LV_PART_ITEMS);
 unsigned normal_color=lv_color_to_u32(lv_obj_get_style_bg_color(obj,LV_PART_ITEMS))&0xffffff;
 unsigned normal_text=lv_color_to_u32(lv_obj_get_style_text_color(obj,LV_PART_ITEMS))&0xffffff;
 lv_obj_add_state(obj,LV_STATE_CHECKED);
 unsigned checked_bg=lv_obj_get_style_bg_opa(obj,LV_PART_ITEMS);
 unsigned checked_color=lv_color_to_u32(lv_obj_get_style_bg_color(obj,LV_PART_ITEMS))&0xffffff;
 unsigned checked_text=lv_color_to_u32(lv_obj_get_style_text_color(obj,LV_PART_ITEMS))&0xffffff;
 lv_obj_remove_state(obj,LV_STATE_CHECKED);
 printf("{\"item_radius\":%u,\"item_border_width\":%u,\"item_bg_rgb_normal\":%u,\"item_bg_rgb_checked\":%u,",item_radius,item_border,normal_color,checked_color);
 printf("\"context\":\"%s\",\"keyboard_rect\":[%d,%d,%d,%d],\"rows\":%u,\"key_count\":%u,\"pad_left\":%d,\"pad_right\":%d,\"pad_top\":%d,\"pad_bottom\":%d,\"pad_row\":%d,\"pad_column\":%d,\"items_bg_opa_normal\":%u,\"items_bg_opa_checked\":%u,\"items_text_rgb_normal\":%u,\"items_text_rgb_checked\":%u,\"map\":[",context,area.x1,area.y1,area.x2,area.y2,(unsigned)matrix->row_cnt,(unsigned)matrix->btn_cnt,lv_obj_get_style_pad_left(obj,0),lv_obj_get_style_pad_right(obj,0),lv_obj_get_style_pad_top(obj,0),lv_obj_get_style_pad_bottom(obj,0),lv_obj_get_style_pad_row(obj,0),lv_obj_get_style_pad_column(obj,0),normal_bg,checked_bg,normal_text,checked_text);
 for(unsigned i=0;matrix->map_p[i][0];i++){if(i)putchar(',');json_text(matrix->map_p[i]);}
 printf("],\"keys\":[");unsigned row=0;int previous_y=-1;
 for(unsigned i=0;i<matrix->btn_cnt;i++){
  lv_area_t a=matrix->button_areas[i];unsigned ctrl=matrix->ctrl_bits[i];
  if(i&&a.y1!=previous_y)row++;previous_y=a.y1;
  if(i)putchar(',');printf("{\"id\":%u,\"row\":%u,\"text\":",i,row);json_text(lv_buttonmatrix_get_button_text(obj,i));
  printf(",\"local_rect\":[%d,%d,%d,%d],\"screen_rect\":[%d,%d,%d,%d],\"width\":%d,\"height\":%d,\"ctrl\":%u,\"weight\":%u,\"hidden\":%s,\"checked\":%s,\"disabled\":%s}",a.x1,a.y1,a.x2,a.y2,area.x1+a.x1,area.y1+a.y1,area.x1+a.x2,area.y1+a.y2,lv_area_get_width(&a),lv_area_get_height(&a),ctrl,ctrl&15,(ctrl&LV_BUTTONMATRIX_CTRL_HIDDEN)?"true":"false",(ctrl&LV_BUTTONMATRIX_CTRL_CHECKED)?"true":"false",(ctrl&LV_BUTTONMATRIX_CTRL_DISABLED)?"true":"false");
 }printf("]}\n");
}
int main(void){
 setbuf(stdout,NULL);rgb565_mode=true;lv_init();display=lv_display_create(1280,720);
 lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_flush_cb(display,flush);
 lv_display_set_buffers(display,drawbuf,NULL,sizeof(drawbuf)/2,LV_DISPLAY_RENDER_MODE_PARTIAL);
 touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(touch,read_touch);
 tab5_ui_init(action,NULL);tab5_ui_state_t s={.light_theme=true,.now_known=true,.now_us=100000000,.datetime_known=true,.datetime={2026,10,22,14,32,5}};
 tab5_measure_config_default(&s.measurement.config);tab5_ui_update(&s);pump();
 nav(4);tap(400,204);dump("open_text",0,textarea(0));
 lv_obj_t *keys=keyboard(lv_screen_active());
 lv_keyboard_set_mode(keys,LV_KEYBOARD_MODE_TEXT_LOWER);dump_keyboard("keyboard_en_lower",keys);
 lv_keyboard_set_mode(keys,LV_KEYBOARD_MODE_TEXT_UPPER);dump_keyboard("keyboard_en_upper",keys);
 lv_keyboard_set_mode(keys,LV_KEYBOARD_MODE_SPECIAL);dump_keyboard("keyboard_special",keys);
 lv_keyboard_set_mode(keys,LV_KEYBOARD_MODE_TEXT_LOWER);
 click("EN \xE2\x86\x92 \xD0\xA0\xD0\xA3\xD0\xA1");dump_keyboard("keyboard_custom_ru",keys);
 lv_keyboard_set_mode(keys,LV_KEYBOARD_MODE_USER_2);dump_keyboard("keyboard_user_2_default",keys);
 lv_keyboard_set_mode(keys,LV_KEYBOARD_MODE_USER_3);dump_keyboard("keyboard_user_3_default",keys);
 lv_keyboard_set_mode(keys,LV_KEYBOARD_MODE_USER_4);dump_keyboard("keyboard_user_4_default",keys);
 /* Close via public logical button hit, then open clock on Settings page. */
 click("\xD0\x9E\xD1\x82\xD0\xBC\xD0\xB5\xD0\xBD\xD0\xB0");nav(3);tap(1160,584);
 if(!textarea(0)){
   /* SPEC clock button: Settings page card at y=388 + local y=144. */
   tap(1160,560);
 }
 for(unsigned i=0;i<6;i++)dump("open_time",i,textarea(i));
 keys=keyboard(lv_screen_active());lv_keyboard_set_mode(keys,LV_KEYBOARD_MODE_NUMBER);dump_keyboard("keyboard_number",keys);
 tab5_ui_destroy();lv_display_delete(display);lv_deinit();return 0;
}
