/* Independent C11 retained RGB565 toolkit. No LVGL headers or runtime.
 * Caller serializes UI access. Objects own copied text, but borrow fonts,
 * shared styles, image descriptors/data and external chart arrays.
 * Flush callback is synchronous; stride is the full framebuffer width. */
#ifndef NATIVE_UI_H
#define NATIVE_UI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef NUI_PROFILE_FINE_COUNTERS
#define NUI_PROFILE_FINE_COUNTERS 0
#endif
#ifndef NUI_PROFILE_PRIMITIVE_TIMING
#define NUI_PROFILE_PRIMITIVE_TIMING 0
#endif
#define NUI_VERSION_MAJOR 9
#define NUI_VERSION_MINOR 0
#define NUI_VERSION_CHECK(a,b,c) ((a)<=9)
#define NUI_ATTRIBUTE_LARGE_CONST
#define NUI_ATTRIBUTE_MEM_ALIGN
#define NUI_FONT_SUBPX_NONE 0
#define NUI_FONT_DECLARE(name) extern const nui_font_t name
#define NUI_COORD_MAX INT32_MAX
#define NUI_SIZE_CONTENT (INT32_MIN+1)
#define NUI_RADIUS_CIRCLE INT32_MAX
#define NUI_CHART_POINT_NONE INT32_MAX
#define NUI_IMAGE_HEADER_MAGIC 0x19
#define NUI_COLOR_FORMAT_RGB565 1
#define NUI_ANIM_OFF 0
#define NUI_GRAD_DIR_NONE 0
#define NUI_GRAD_DIR_HOR 1
#define NUI_GRAD_DIR_VER 2
#define NUI_SYMBOL_BACKSPACE "\xEF\x95\x9A"
#define NUI_SYMBOL_CLOSE "\xEF\x80\x8D"
#define NUI_SYMBOL_LEFT "\xEF\x81\x93"
#define NUI_SYMBOL_RIGHT "\xEF\x81\x94"
#define NUI_SYMBOL_KEYBOARD "\xEF\x84\x9C"
#define NUI_SYMBOL_UP "\xEF\x81\xB7"
#define NUI_SYMBOL_NEW_LINE "\xEF\xA2\xA2"
#define NUI_SYMBOL_OK "\xEF\x80\x8C"
#define NUI_PART_MAIN 0u
#define NUI_PART_ITEMS (1u<<16)
#define NUI_PART_INDICATOR (2u<<16)
#define NUI_PART_KNOB (3u<<16)
#define NUI_STATE_CHECKED 1u
#define NUI_STATE_PRESSED 2u
#define NUI_STATE_DISABLED 4u
#define NUI_STATE_FOCUSED 8u
#define NUI_OBJ_FLAG_HIDDEN 1u
#define NUI_OBJ_FLAG_CLICKABLE 2u
#define NUI_OBJ_FLAG_SCROLLABLE 4u
#define NUI_OBJ_FLAG_SEND_DRAW_TASK_EVENTS 8u
/* Audited subtree callbacks do not change paint when this button is PRESSED.
 * State selectors still veto the skip. Unknown callbacks remain conservative. */
#define NUI_OBJ_FLAG_PRESSED_PAINT_INERT 16u
/* Audited subtree draw callbacks may be omitted when completely occluded.
 * Separate contract from PRESSED paint invariance; never implied by it. */
#define NUI_OBJ_FLAG_OCCLUSION_PAINT_INERT 32u
#define NUI_TEXT_FLAG_NONE 0
#define NUI_TEXT_FLAG_EXPAND 1
#define NUI_STYLE_RES_FOUND 1
#define NUI_STYLE_RES_NOT_FOUND 0
#define NUI_OPA_COVER 255
#define NUI_OPA_TRANSP 0
#define NUI_ALIGN_TOP_LEFT 0
#define NUI_ALIGN_CENTER 1
#define NUI_TEXT_ALIGN_LEFT 0
#define NUI_TEXT_ALIGN_CENTER 1
#define NUI_TEXT_ALIGN_RIGHT 2
#define NUI_LABEL_LONG_DOT 0
#define NUI_LABEL_LONG_SCROLL_CIRCULAR 1
#define NUI_LABEL_LONG_WRAP 2
#define NUI_KEYBOARD_MODE_TEXT_LOWER 0
#define NUI_KEYBOARD_MODE_TEXT_UPPER 1
#define NUI_KEYBOARD_MODE_NUMBER 2
#define NUI_KEYBOARD_MODE_USER_1 3
#define NUI_CHART_AXIS_PRIMARY_Y 0
#define NUI_CHART_TYPE_LINE 0
#define NUI_BUTTONMATRIX_CTRL_WIDTH_MASK 15u
typedef int32_t nui_coord_t;
typedef uint32_t nui_style_prop_t;
typedef uint32_t nui_style_selector_t;
typedef uint16_t nui_buttonmatrix_ctrl_t;
typedef uint32_t nui_color_t;
typedef struct {int32_t x,y;} nui_point_t;
typedef struct {float x,y;} nui_point_precise_t;
typedef struct {int32_t x1,y1,x2,y2;} nui_area_t;
typedef union {int32_t num;const void *ptr;nui_color_t color;} nui_style_value_t;
typedef struct nui_style_entry nui_style_entry_t;
typedef struct {nui_style_entry_t *entries;} nui_style_t;
typedef struct nui_obj nui_obj_t;
typedef struct nui_timer nui_timer_t;
typedef struct nui_layer {nui_area_t clip;uint8_t opa;} nui_layer_t;
typedef struct nui_indev {nui_point_t point;bool pressed;} nui_indev_t;
typedef struct {unsigned kind;} nui_obj_class_t;
extern const nui_obj_class_t nui_obj_class,nui_label_class,nui_button_class,nui_switch_class,nui_textarea_class,nui_keyboard_class,nui_chart_class,nui_image_class,nui_bar_class;
typedef enum {NUI_EVENT_ALL=0,NUI_EVENT_CLICKED,NUI_EVENT_PRESSED,NUI_EVENT_PRESSING,NUI_EVENT_RELEASED,NUI_EVENT_PRESS_LOST,NUI_EVENT_VALUE_CHANGED,NUI_EVENT_FOCUSED,NUI_EVENT_READY,NUI_EVENT_CANCEL,NUI_EVENT_DRAW_MAIN,NUI_EVENT_DRAW_POST,NUI_EVENT_DRAW_TASK_ADDED} nui_event_code_t;
typedef struct nui_event {nui_obj_t *target;void *user_data,*param;nui_layer_t *layer;nui_event_code_t code;} nui_event_t;
typedef void (*nui_event_cb_t)(nui_event_t*);
/* Synchronous primitive notifications for diagnostics; there is no draw-task
 * scheduler or per-segment heap allocation. param is borrowed during callback. */
typedef enum {NUI_DRAW_TASK_TYPE_NONE,NUI_DRAW_TASK_TYPE_FILL,NUI_DRAW_TASK_TYPE_BORDER,NUI_DRAW_TASK_TYPE_BOX_SHADOW,NUI_DRAW_TASK_TYPE_LABEL,NUI_DRAW_TASK_TYPE_IMAGE,NUI_DRAW_TASK_TYPE_LINE,NUI_DRAW_TASK_TYPE_ARC,NUI_DRAW_TASK_TYPE_TRIANGLE,NUI_DRAW_TASK_TYPE_MASK_RECTANGLE,NUI_DRAW_TASK_TYPE_MASK_BITMAP} nui_draw_task_type_t;
typedef struct {nui_draw_task_type_t type;} nui_draw_task_t;
nui_draw_task_t *nui_event_get_draw_task(const nui_event_t*);
nui_draw_task_type_t nui_draw_task_get_type(const nui_draw_task_t*);

typedef struct {uint32_t bitmap_index;uint16_t adv_w,box_w,box_h;int16_t ofs_x,ofs_y;} nui_font_fmt_txt_glyph_dsc_t;
typedef struct {uint32_t range_start,range_length,glyph_id_start;const uint16_t *unicode_list;const void *glyph_id_ofs_list;uint16_t list_length;uint8_t type;} nui_font_fmt_txt_cmap_t;
enum {NUI_FONT_FMT_TXT_CMAP_FORMAT0_TINY,NUI_FONT_FMT_TXT_CMAP_FORMAT0_FULL,NUI_FONT_FMT_TXT_CMAP_SPARSE_TINY,NUI_FONT_FMT_TXT_CMAP_SPARSE_FULL};
typedef struct {const int8_t *class_pair_values;const uint8_t *left_class_mapping,*right_class_mapping;uint16_t left_class_cnt,right_class_cnt;} nui_font_fmt_txt_kern_classes_t;
typedef struct {const uint8_t *glyph_bitmap;const nui_font_fmt_txt_glyph_dsc_t *glyph_dsc;const nui_font_fmt_txt_cmap_t *cmaps;const void *kern_dsc;uint16_t kern_scale,cmap_num;uint8_t bpp,kern_classes,bitmap_format;} nui_font_fmt_txt_dsc_t;
typedef struct {uint32_t dummy;} nui_font_fmt_txt_glyph_cache_t;
typedef struct nui_font nui_font_t;
typedef struct {uint32_t adv_w;uint16_t box_w,box_h;int16_t ofs_x,ofs_y;uint8_t bpp;bool is_placeholder;uint32_t gid;const nui_font_t *resolved_font;} nui_font_glyph_dsc_t;
typedef bool (*nui_font_get_glyph_dsc_fmt_t)(const nui_font_t*,nui_font_glyph_dsc_t*,uint32_t,uint32_t);
typedef const uint8_t *(*nui_font_get_bitmap_fmt_t)(const nui_font_t*,uint32_t);
struct nui_font {nui_font_get_glyph_dsc_fmt_t get_glyph_dsc;nui_font_get_bitmap_fmt_t get_glyph_bitmap;uint16_t line_height;int16_t base_line,underline_position;uint8_t underline_thickness,subpx;const void *dsc;const nui_font_t *fallback;void *user_data;};
extern const nui_font_t nui_font_fallback_14;
#define NUI_FONT_DEFAULT (&nui_font_fallback_14)
bool nui_font_get_glyph_dsc_fmt_txt(const nui_font_t*,nui_font_glyph_dsc_t*,uint32_t,uint32_t);
const uint8_t *nui_font_get_bitmap_fmt_txt(const nui_font_t*,uint32_t);
bool nui_font_get_glyph_dsc(const nui_font_t*,nui_font_glyph_dsc_t*,uint32_t,uint32_t);
void nui_text_get_size(nui_point_t*,const char*,const nui_font_t*,int32_t,int32_t,int32_t,unsigned);
uint32_t nui_text_utf8_next(const char*,uint32_t*);
typedef struct {uint32_t magic,cf,w,h,stride,flags;} nui_image_header_t;
typedef struct {nui_image_header_t header;uint32_t data_size;const uint8_t *data;void *reserved;} nui_image_dsc_t;
typedef struct {nui_point_precise_t p1,p2;nui_color_t color;int32_t width,dash_width,dash_gap;uint8_t opa,raw_end;} nui_draw_line_dsc_t;
typedef struct {nui_point_t center;int32_t radius,start_angle,end_angle,width;nui_color_t color;uint8_t opa,rounded;} nui_draw_arc_dsc_t;
typedef struct {int32_t radius,border_width;nui_color_t bg_color,border_color;uint8_t bg_opa,border_opa;} nui_draw_rect_dsc_t;
typedef struct {nui_point_precise_t p[3];nui_color_t color;uint8_t opa;} nui_draw_triangle_dsc_t;
typedef struct nui_chart_series {int32_t *y_points;bool hidden;bool owned;nui_color_t color;struct nui_chart_series *next;} nui_chart_series_t;

enum {
 NUI_STYLE_BG_COLOR=1,
 NUI_STYLE_BG_OPA=2,
 NUI_STYLE_BG_GRAD_COLOR=3,
 NUI_STYLE_BG_GRAD_DIR=4,
 NUI_STYLE_BORDER_COLOR=5,
 NUI_STYLE_BORDER_WIDTH=6,
 NUI_STYLE_RADIUS=7,
 NUI_STYLE_PAD_TOP=8,
 NUI_STYLE_PAD_BOTTOM=9,
 NUI_STYLE_PAD_LEFT=10,
 NUI_STYLE_PAD_RIGHT=11,
 NUI_STYLE_PAD_ROW=12,
 NUI_STYLE_PAD_COLUMN=13,
 NUI_STYLE_TEXT_COLOR=14,
 NUI_STYLE_TEXT_FONT=15,
 NUI_STYLE_TEXT_LETTER_SPACE=16,
 NUI_STYLE_TEXT_ALIGN=17,
 NUI_STYLE_TEXT_LINE_SPACE=18,
 NUI_STYLE_SHADOW_COLOR=19,
 NUI_STYLE_SHADOW_WIDTH=20,
 NUI_STYLE_SHADOW_OFFSET_Y=21,
 NUI_STYLE_SHADOW_OPA=22,
 NUI_STYLE_OPA=23,
 NUI_STYLE_LINE_COLOR=24,
 NUI_STYLE_LINE_WIDTH=25,
 NUI_STYLE_LINE_OPA=26,
 NUI_STYLE_OUTLINE_COLOR=27,
 NUI_STYLE_OUTLINE_WIDTH=28,
 NUI_STYLE_OUTLINE_OPA=29,
 NUI_STYLE_OUTLINE_PAD=30,
 NUI_STYLE_WIDTH=31,
 NUI_STYLE_HEIGHT=32,
 NUI_STYLE_IMAGE_OPA=33
};

typedef enum {NUI_PRIM_FILL,NUI_PRIM_BORDER,NUI_PRIM_LABEL,NUI_PRIM_IMAGE,NUI_PRIM_LINE,NUI_PRIM_ARC,NUI_PRIM_TRIANGLE,NUI_PRIM_SHADOW,NUI_PRIM_COUNT} nui_primitive_t;
typedef struct {uint64_t calls,us,pixels;} nui_primitive_stats_t;
/* Fine activity counters (primitive calls/pixels, cache hits/misses/evictions)
 * are disabled unless NUI_PROFILE_FINE_COUNTERS=1; zero then means unavailable,
 * not measured inactivity. Primitive us requires NUI_PROFILE_PRIMITIVE_TIMING=1.
 * Coarse frames/render/dirty totals and live/peak allocation/cache bytes always
 * remain available. No claim of per-frame allocation counts is made.
 * The gradient cache is one static bounded LUT; its full struct size is reported
 * outside heap allocation bytes. Profile reset retains its valid warm data.
 * Cache byte gauges include resident descriptors: shadow capacity is a fixed 64 KiB
 * mask budget plus eight static descriptors. The object cache is fixed per live
 * retained object; its payload is already part of allocation_bytes (not additive).
 * Style eviction counts replacement of a populated part/state property set;
 * layout eviction counts replacement of a populated coordinate record.
 * Peak gauges have process lifetime, like allocation_peak; profile_reset clears
 * activity counters only. Miss/build work runs inside render_us, including cold
 * first paint. No hidden prewarm and no cached final framebuffer pixels. */
typedef struct {uint64_t frames,dirty_rects,dirty_pixels,render_us; nui_primitive_stats_t primitive[NUI_PRIM_COUNT];size_t allocation_bytes,allocation_peak,allocation_count;uint64_t allocation_failures;uint64_t shadow_cache_hits,shadow_cache_misses,shadow_cache_build_pixels,style_cache_hits,style_cache_misses,style_cache_evictions;size_t shadow_cache_bytes,shadow_cache_peak_bytes,shadow_cache_capacity_bytes,shadow_cache_static_bytes;uint64_t shadow_cache_evictions;size_t object_cache_bytes,object_cache_peak_bytes,object_cache_bytes_per_object;uint64_t layout_cache_hits,layout_cache_misses,layout_cache_evictions,skipped_state_invalidations,clear_pixels_skipped;size_t gradient_cache_bytes,gradient_cache_peak_bytes,gradient_cache_capacity_bytes;uint64_t gradient_cache_hits,gradient_cache_misses,gradient_cache_build_pixels,gradient_cache_evictions;} nui_native_stats_t;
typedef void (*nui_native_flush_cb_t)(const nui_area_t*,const uint16_t*,size_t stride,void*);
bool nui_native_init(int width,int height,uint16_t *framebuffer);
void nui_native_deinit(void);
size_t nui_native_render(void);
void nui_native_touch(int x,int y,bool pressed);
void nui_native_tick(uint32_t ms);
void nui_native_set_flush_cb(nui_native_flush_cb_t,void*);
void nui_native_set_clock_cb(uint64_t (*clock_us)(void));
size_t nui_native_get_dirty(nui_area_t *out,size_t capacity);
void nui_native_invalidate(const nui_area_t*);
void nui_native_profile_snapshot(nui_native_stats_t*);
void nui_native_profile_reset(void);
uint32_t nui_tick_get(void);
void nui_tick_inc(uint32_t);
nui_obj_t *nui_screen_active(void);
void nui_init(void);
void nui_deinit(void);
void nui_obj_delete(nui_obj_t*);
nui_obj_t *nui_obj_create(nui_obj_t*);
nui_obj_t *nui_label_create(nui_obj_t*);
nui_obj_t *nui_button_create(nui_obj_t*);
nui_obj_t *nui_switch_create(nui_obj_t*);
nui_obj_t *nui_textarea_create(nui_obj_t*);
nui_obj_t *nui_keyboard_create(nui_obj_t*);
nui_obj_t *nui_chart_create(nui_obj_t*);
nui_obj_t *nui_image_create(nui_obj_t*);
nui_obj_t *nui_bar_create(nui_obj_t*);
void nui_obj_set_pos(nui_obj_t*,int32_t,int32_t);
void nui_obj_set_size(nui_obj_t*,int32_t,int32_t);
void nui_obj_set_width(nui_obj_t*,int32_t);
void nui_obj_set_height(nui_obj_t*,int32_t);
void nui_obj_set_x(nui_obj_t*,int32_t);
void nui_obj_set_y(nui_obj_t*,int32_t);
void nui_obj_set_align(nui_obj_t*,int);
void nui_obj_center(nui_obj_t*);
void nui_obj_update_layout(nui_obj_t*);
int32_t nui_obj_get_width(const nui_obj_t*);
int32_t nui_obj_get_height(const nui_obj_t*);
void nui_obj_get_coords(const nui_obj_t*,nui_area_t*);
nui_obj_t *nui_obj_get_parent(const nui_obj_t*);
nui_obj_t *nui_obj_get_child(const nui_obj_t*,int32_t);
uint32_t nui_obj_get_child_count(const nui_obj_t*);
bool nui_obj_check_type(const nui_obj_t*,const nui_obj_class_t*);
void nui_obj_move_foreground(nui_obj_t*);
void nui_obj_add_flag(nui_obj_t*,uint32_t);
void nui_obj_remove_flag(nui_obj_t*,uint32_t);
bool nui_obj_has_flag(const nui_obj_t*,uint32_t);
void nui_obj_add_state(nui_obj_t*,uint32_t);
void nui_obj_remove_state(nui_obj_t*,uint32_t);
bool nui_obj_has_state(const nui_obj_t*,uint32_t);
bool nui_obj_is_visible(const nui_obj_t*);
void nui_obj_set_ext_click_area(nui_obj_t*,int32_t);
void nui_obj_invalidate(nui_obj_t*);
void nui_obj_add_event_cb(nui_obj_t*,nui_event_cb_t,nui_event_code_t,void*);
void nui_obj_send_event(nui_obj_t*,nui_event_code_t,void*);
nui_event_code_t nui_event_get_code(const nui_event_t*);
nui_obj_t *nui_event_get_target(const nui_event_t*);
void *nui_event_get_user_data(const nui_event_t*);
void *nui_event_get_param(const nui_event_t*);
nui_layer_t *nui_event_get_layer(const nui_event_t*);
void nui_style_init(nui_style_t*);
void nui_style_reset(nui_style_t*);
void nui_style_set_prop(nui_style_t*,nui_style_prop_t,nui_style_value_t);
nui_style_prop_t nui_style_register_prop(unsigned);
unsigned nui_style_get_num_custom_props(void);
void nui_obj_add_style(nui_obj_t*,const nui_style_t*,nui_style_selector_t);
void nui_obj_remove_style(nui_obj_t*,const nui_style_t*,nui_style_selector_t);
void nui_obj_remove_style_all(nui_obj_t*);
void nui_obj_report_style_change(const nui_style_t*);
int nui_obj_get_local_style_prop(const nui_obj_t*,nui_style_prop_t,nui_style_value_t*,nui_style_selector_t);
void nui_obj_set_local_style_prop(nui_obj_t*,nui_style_prop_t,nui_style_value_t,nui_style_selector_t);
nui_style_value_t nui_obj_get_style_prop(const nui_obj_t*,nui_style_selector_t,nui_style_prop_t);
void nui_style_set_bg_color(nui_style_t*,nui_color_t);
void nui_obj_set_style_bg_color(nui_obj_t*,nui_color_t,nui_style_selector_t);
nui_color_t nui_obj_get_style_bg_color(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_bg_opa(nui_style_t*,int32_t);
void nui_obj_set_style_bg_opa(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_bg_opa(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_bg_grad_color(nui_style_t*,nui_color_t);
void nui_obj_set_style_bg_grad_color(nui_obj_t*,nui_color_t,nui_style_selector_t);
nui_color_t nui_obj_get_style_bg_grad_color(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_bg_grad_dir(nui_style_t*,int32_t);
void nui_obj_set_style_bg_grad_dir(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_bg_grad_dir(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_border_color(nui_style_t*,nui_color_t);
void nui_obj_set_style_border_color(nui_obj_t*,nui_color_t,nui_style_selector_t);
nui_color_t nui_obj_get_style_border_color(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_border_width(nui_style_t*,int32_t);
void nui_obj_set_style_border_width(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_border_width(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_radius(nui_style_t*,int32_t);
void nui_obj_set_style_radius(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_radius(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_pad_top(nui_style_t*,int32_t);
void nui_obj_set_style_pad_top(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_pad_top(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_pad_bottom(nui_style_t*,int32_t);
void nui_obj_set_style_pad_bottom(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_pad_bottom(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_pad_left(nui_style_t*,int32_t);
void nui_obj_set_style_pad_left(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_pad_left(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_pad_right(nui_style_t*,int32_t);
void nui_obj_set_style_pad_right(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_pad_right(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_pad_row(nui_style_t*,int32_t);
void nui_obj_set_style_pad_row(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_pad_row(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_pad_column(nui_style_t*,int32_t);
void nui_obj_set_style_pad_column(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_pad_column(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_text_color(nui_style_t*,nui_color_t);
void nui_obj_set_style_text_color(nui_obj_t*,nui_color_t,nui_style_selector_t);
nui_color_t nui_obj_get_style_text_color(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_text_font(nui_style_t*,const nui_font_t*);
void nui_obj_set_style_text_font(nui_obj_t*,const nui_font_t*,nui_style_selector_t);
const nui_font_t* nui_obj_get_style_text_font(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_text_letter_space(nui_style_t*,int32_t);
void nui_obj_set_style_text_letter_space(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_text_letter_space(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_text_align(nui_style_t*,int32_t);
void nui_obj_set_style_text_align(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_text_align(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_text_line_space(nui_style_t*,int32_t);
void nui_obj_set_style_text_line_space(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_text_line_space(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_shadow_color(nui_style_t*,nui_color_t);
void nui_obj_set_style_shadow_color(nui_obj_t*,nui_color_t,nui_style_selector_t);
nui_color_t nui_obj_get_style_shadow_color(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_shadow_width(nui_style_t*,int32_t);
void nui_obj_set_style_shadow_width(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_shadow_width(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_shadow_offset_y(nui_style_t*,int32_t);
void nui_obj_set_style_shadow_offset_y(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_shadow_offset_y(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_shadow_opa(nui_style_t*,int32_t);
void nui_obj_set_style_shadow_opa(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_shadow_opa(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_opa(nui_style_t*,int32_t);
void nui_obj_set_style_opa(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_opa(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_line_color(nui_style_t*,nui_color_t);
void nui_obj_set_style_line_color(nui_obj_t*,nui_color_t,nui_style_selector_t);
nui_color_t nui_obj_get_style_line_color(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_line_width(nui_style_t*,int32_t);
void nui_obj_set_style_line_width(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_line_width(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_line_opa(nui_style_t*,int32_t);
void nui_obj_set_style_line_opa(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_line_opa(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_outline_color(nui_style_t*,nui_color_t);
void nui_obj_set_style_outline_color(nui_obj_t*,nui_color_t,nui_style_selector_t);
nui_color_t nui_obj_get_style_outline_color(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_outline_width(nui_style_t*,int32_t);
void nui_obj_set_style_outline_width(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_outline_width(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_outline_opa(nui_style_t*,int32_t);
void nui_obj_set_style_outline_opa(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_outline_opa(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_outline_pad(nui_style_t*,int32_t);
void nui_obj_set_style_outline_pad(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_outline_pad(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_width(nui_style_t*,int32_t);
void nui_obj_set_style_width(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_width(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_height(nui_style_t*,int32_t);
void nui_obj_set_style_height(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_height(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_image_opa(nui_style_t*,int32_t);
void nui_obj_set_style_image_opa(nui_obj_t*,int32_t,nui_style_selector_t);
int32_t nui_obj_get_style_image_opa(const nui_obj_t*,nui_style_selector_t);
void nui_style_set_pad_all(nui_style_t*,int32_t);
void nui_obj_set_style_pad_all(nui_obj_t*,int32_t,nui_style_selector_t);
void nui_style_set_pad_hor(nui_style_t*,int32_t);
void nui_obj_set_style_pad_hor(nui_obj_t*,int32_t,nui_style_selector_t);
void nui_style_set_pad_ver(nui_style_t*,int32_t);
void nui_obj_set_style_pad_ver(nui_obj_t*,int32_t,nui_style_selector_t);

void nui_obj_set_style_size(nui_obj_t*,int32_t,int32_t,nui_style_selector_t);
nui_color_t nui_color_hex(uint32_t);
nui_color_t nui_color_white(void);
bool nui_color_eq(nui_color_t,nui_color_t);
nui_color_t nui_color_mix(nui_color_t,nui_color_t,uint8_t);
uint16_t nui_color_to_u16(nui_color_t);
uint32_t nui_color_to_u32(nui_color_t);
void nui_label_set_text(nui_obj_t*,const char*);
const char *nui_label_get_text(const nui_obj_t*);
void nui_label_set_long_mode(nui_obj_t*,int);
#define NUI_TEST_HAS_TEXTAREA_BOUNDS 1
bool nui_native_textarea_get_text_bounds(const nui_obj_t*,nui_area_t*);
void nui_textarea_set_text(nui_obj_t*,const char*);
const char *nui_textarea_get_text(const nui_obj_t*);
void nui_textarea_set_one_line(nui_obj_t*,bool);
void nui_textarea_set_password_mode(nui_obj_t*,bool);
void nui_textarea_set_max_length(nui_obj_t*,uint32_t);
void nui_textarea_set_accepted_chars(nui_obj_t*,const char*);
void nui_textarea_add_text(nui_obj_t*,const char*);
void nui_textarea_delete_char(nui_obj_t*);
void nui_textarea_set_cursor_pos(nui_obj_t*,int32_t);
void nui_keyboard_set_map(nui_obj_t*,int,const char *const[],const nui_buttonmatrix_ctrl_t[]);
void nui_keyboard_set_mode(nui_obj_t*,int);
void nui_keyboard_set_textarea(nui_obj_t*,nui_obj_t*);
const char *nui_buttonmatrix_get_button_text(nui_obj_t*,uint32_t);
size_t nui_native_keyboard_get_keys(nui_obj_t*,nui_area_t *areas,const char **texts,size_t cap);
void nui_image_set_src(nui_obj_t*,const void*);
const void *nui_image_get_src(const nui_obj_t*);
void nui_image_cache_drop(const void*);
void nui_bar_set_range(nui_obj_t*,int32_t,int32_t);
void nui_bar_set_value(nui_obj_t*,int32_t,int);
int32_t nui_bar_get_value(const nui_obj_t*);
void nui_chart_set_type(nui_obj_t*,int);
void nui_chart_set_point_count(nui_obj_t*,uint32_t);
uint32_t nui_chart_get_point_count(const nui_obj_t*);
void nui_chart_set_axis_range(nui_obj_t*,int,int32_t,int32_t);
void nui_chart_set_div_line_count(nui_obj_t*,uint8_t,uint8_t);
nui_chart_series_t *nui_chart_add_series(nui_obj_t*,nui_color_t,int);
void nui_chart_set_series_ext_y_array(nui_obj_t*,nui_chart_series_t*,int32_t*);
void nui_chart_set_series_color(nui_obj_t*,nui_chart_series_t*,nui_color_t);
nui_color_t nui_chart_get_series_color(nui_obj_t*,const nui_chart_series_t*);
void nui_chart_hide_series(nui_obj_t*,nui_chart_series_t*,bool);
nui_chart_series_t *nui_chart_get_series_next(nui_obj_t*,const nui_chart_series_t*);
int32_t *nui_chart_get_series_y_array(nui_obj_t*,nui_chart_series_t*);
void nui_chart_refresh(nui_obj_t*);
nui_timer_t *nui_timer_create(void (*)(nui_timer_t*),uint32_t,void*);
void nui_timer_delete(nui_timer_t*);
void nui_timer_set_period(nui_timer_t*,uint32_t);
void *nui_timer_get_user_data(nui_timer_t*);
uint32_t nui_timer_handler(void);
nui_indev_t *nui_indev_active(void);
void nui_indev_get_point(nui_indev_t*,nui_point_t*);
void nui_draw_rect_dsc_init(nui_draw_rect_dsc_t*);
void nui_draw_line_dsc_init(nui_draw_line_dsc_t*);
void nui_draw_arc_dsc_init(nui_draw_arc_dsc_t*);
void nui_draw_triangle_dsc_init(nui_draw_triangle_dsc_t*);
void nui_draw_rect(nui_layer_t*,const nui_draw_rect_dsc_t*,const nui_area_t*);
void nui_draw_line(nui_layer_t*,const nui_draw_line_dsc_t*);
void nui_draw_arc(nui_layer_t*,const nui_draw_arc_dsc_t*);
void nui_draw_triangle(nui_layer_t*,const nui_draw_triangle_dsc_t*);
int32_t nui_area_get_width(const nui_area_t*);
int32_t nui_area_get_height(const nui_area_t*);
#ifdef __cplusplus
}
#endif
#endif
