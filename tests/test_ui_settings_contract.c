#include "../tab5/main/tab5_ui.h"
#include <assert.h>
int main(void){
 tab5_ui_action_t legacy={.kind=TAB5_UI_SETTINGS};
 tab5_ui_state_t unknown={0};
 assert(!legacy.backlight_timeout_set&&!legacy.record_sync_set);
 assert(!unknown.backlight_timeout_known&&!unknown.record_sync_known);
 tab5_ui_action_t explicit_off={.kind=TAB5_UI_SETTINGS,
 .backlight_timeout_set=true,.backlight_timeout_s=0,
 .record_sync_set=true,.record_sync=false};
 tab5_ui_state_t known_off={.backlight_timeout_known=true,
 .backlight_timeout_s=0,.record_sync_known=true,.record_sync=false};
 assert(explicit_off.backlight_timeout_set&&explicit_off.record_sync_set);
 assert(known_off.backlight_timeout_known&&known_off.record_sync_known);
 assert(explicit_off.backlight_timeout_s==known_off.backlight_timeout_s);
 assert(explicit_off.record_sync==known_off.record_sync);
 return 0;
}
