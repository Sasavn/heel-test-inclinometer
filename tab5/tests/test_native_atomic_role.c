/* Compile the real production UI role() into this test TU. The independent
 * sequential oracle is the accepted remove-known/add-target behavior. No
 * renderer or style lookup is replaced. */
#include "../main/tab5_ui.c"
#include <assert.h>
static uint16_t role_fb[1280*720],role_warm[1280*720],role_partial[1280*720];
static bool count_role_allocations;
static unsigned role_allocations;
void *__real_calloc(size_t,size_t);
void *__wrap_calloc(size_t count,size_t bytes){if(count_role_allocations)role_allocations++;return __real_calloc(count,bytes);}
static void sequential_role(nui_obj_t *o,int target){
    if(nui_obj_get_style_prop(o,NUI_PART_MAIN,role_prop).num==target+1)return;
    for(unsigned i=0;i<STYLE_COUNT;i++)nui_obj_remove_style(o,&u.style[i],NUI_PART_MAIN);
    nui_obj_add_style(o,&u.style[target],NUI_PART_MAIN);
}
static nui_style_t extras[3];
static nui_obj_t *role_child;
static nui_obj_t *role_setup(unsigned variant,bool light){
    assert(nui_native_init(1280,720,role_fb));memset(&u,0,sizeof(u));
    if(!role_prop)role_prop=nui_style_register_prop(0);palette(light);
    if(variant==2)nui_style_set_text_font(&u.style[NAVSEL],F24);
    if(variant==3)nui_style_set_pad_all(&u.style[NAVSEL],13);
    if(variant==4)nui_style_set_border_width(&u.style[NAVSEL],5);
    nui_obj_t *parent=nui_obj_create(nui_screen_active());nui_obj_remove_style_all(parent);
    nui_obj_set_pos(parent,100,100);nui_obj_set_size(parent,520,200);role(parent,NAV);
    for(unsigned i=0;i<3;i++)nui_style_init(&extras[i]);
    /* Unknown styles must survive known-role replacement, with their original
     * selector and order; no implementation-owned style registry assumptions. */
    nui_style_set_border_color(&extras[0],nui_color_hex(0xee2288));if(variant>=5&&variant<=7)nui_obj_add_style(parent,&extras[0],0);
    nui_style_set_text_color(&extras[1],nui_color_hex(0x22ee44));nui_style_set_text_font(&extras[1],F16);
    if(variant>=5&&variant<=7)nui_obj_add_style(parent,&extras[1],variant==5?NUI_STATE_PRESSED:variant==6?NUI_STATE_DISABLED:NUI_STATE_CHECKED);
    nui_style_set_pad_left(&extras[2],9);nui_style_set_text_color(&extras[2],nui_color_hex(0x4488ee));
    if(variant>=5&&variant<=7)nui_obj_add_style(parent,&extras[2],NUI_STATE_PRESSED|NUI_STATE_CHECKED);
    if(variant==5)nui_obj_add_state(parent,NUI_STATE_PRESSED);
    if(variant==6)nui_obj_add_state(parent,NUI_STATE_DISABLED);
    if(variant==7)nui_obj_add_state(parent,NUI_STATE_PRESSED|NUI_STATE_CHECKED);
    role_child=nui_label_create(parent);nui_obj_remove_style_all(role_child);nui_obj_set_pos(role_child,16,12);nui_obj_set_size(role_child,450,NUI_SIZE_CONTENT);nui_label_set_text(role_child,"Inherited role Яю 123");
    return parent;
}
static void role_cleanup(nui_obj_t *parent){
    nui_obj_delete(parent);for(unsigned i=0;i<3;i++)nui_style_reset(&extras[i]);
    for(unsigned i=0;i<STYLE_COUNT;i++)nui_style_reset(&u.style[i]);memset(&u,0,sizeof(u));
    nui_native_deinit();nui_native_stats_t stats;nui_native_profile_snapshot(&stats);assert(stats.allocation_bytes==0&&stats.allocation_count==0&&!stats.allocation_failures);
}
int main(void){
    /* Prove the allocation observer itself is active in this binary. */
    role_allocations=0;count_role_allocations=true;void *observer_probe=calloc(1,17);count_role_allocations=false;
    assert(observer_probe&&role_allocations==1);free(observer_probe);
    unsigned cases=0;
    for(unsigned light=0;light<2;light++)for(unsigned variant=0;variant<9;variant++){
        int target=variant==1?ERR:variant==8?NAV:NAVSEL;
        nui_obj_t *parent=role_setup(variant,light);nui_native_render();
        /* Warm all affected inherited properties and descendant geometry. */
        (void)nui_obj_get_style_prop(role_child,0,NUI_STYLE_TEXT_COLOR);(void)nui_obj_get_style_text_font(role_child,0);
        nui_area_t coords;nui_obj_get_coords(role_child,&coords);
        role_allocations=0;count_role_allocations=true;role(parent,target);count_role_allocations=false;
        if(variant>=5&&variant<=7)assert(role_allocations>0);else assert(role_allocations==0);
        nui_native_render();memcpy(role_partial,role_fb,sizeof(role_fb));
        nui_area_t warm_coords;nui_obj_get_coords(role_child,&warm_coords);
        nui_style_value_t warm_color=nui_obj_get_style_prop(role_child,0,NUI_STYLE_TEXT_COLOR);
        const nui_font_t *warm_font=nui_obj_get_style_text_font(role_child,0);
        nui_native_invalidate(&(nui_area_t){0,0,1279,719});nui_native_render();assert(!memcmp(role_partial,role_fb,sizeof(role_fb)));memcpy(role_warm,role_fb,sizeof(role_fb));
        role_cleanup(parent);
        parent=role_setup(variant,light);sequential_role(parent,target);nui_native_render();
        nui_area_t oracle_coords;nui_obj_get_coords(role_child,&oracle_coords);
        assert(!memcmp(role_warm,role_fb,sizeof(role_fb))&&!memcmp(&warm_coords,&oracle_coords,sizeof(warm_coords)));
        assert(nui_color_eq(warm_color.color,nui_obj_get_style_prop(role_child,0,NUI_STYLE_TEXT_COLOR).color));
        assert(warm_font==nui_obj_get_style_text_font(role_child,0));role_cleanup(parent);cases++;
        printf("ATOMIC_ROLE theme=%u variant=%u path=%s inherited/geometry/incremental/full/sequential-oracle PASS\n",light,variant,variant>=5&&variant<=7?"unknown-styles/selectors-conservative-fallback":"sole-MAIN-no-allocation");
    }
    printf("ATOMIC_ROLE_GATE fine=%d cases=%u PASS\n",NUI_PROFILE_FINE_COUNTERS,cases);return 0;
}
