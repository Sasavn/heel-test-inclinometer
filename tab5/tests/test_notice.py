"""Real RGB565 renderer: status notice bounds/captures in both themes.

Also documents unchanged sensor error saturation using the production core.
Outputs are host renders, never hardware screenshots.
"""
from pathlib import Path
import subprocess
import tempfile
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "tab5/main"
OUT = ROOT / ".tools/notice-1.7.2"
HARNESS = r'''
#include "tab5_ui.c"
#include <assert.h>
static uint16_t pixels[1280*720];
static bool inside(nui_area_t child,nui_area_t parent) {
    return child.x1>=parent.x1 && child.y1>=parent.y1 &&
           child.x2<=parent.x2 && child.y2<=parent.y2;
}
static nui_obj_t *find_text(nui_obj_t *o,const char *s) {
    if(nui_obj_check_type(o,&nui_label_class)&&!strcmp(nui_label_get_text(o),s))return o;
    for(unsigned i=0;i<nui_obj_get_child_count(o);i++){
        nui_obj_t *found=find_text(nui_obj_get_child(o,i),s);if(found)return found;
    }
    return NULL;
}
int main(int argc,char **argv) {
    assert(argc==2);
    const char *messages[]={"Дата, время и часовой пояс установлены",
        "Прибор готов. Перед записью установите часы",
        "Системные часы установлены; ошибка записи/флагов RTC; пояс не сохранён"};
    /* 255 counts consecutive failed sensor polls, not display frames. */
    tab5_measure_core_t core;tab5_measure_config_t config;
    tab5_measure_config_default(&config);tab5_measure_core_init(&core,&config,0);
    for(unsigned i=0;i<300;i++)for(unsigned sensor=0;sensor<2;sensor++)
        tab5_measure_core_error(&core,sensor,TAB5_MEASURE_TIMEOUT);
    assert(core.state.sensor[0].fail_streak==255);
    assert(core.state.sensor[0].errors==300&&!core.state.sensor[0].valid);
    assert(core.state.sensor[0].last_error==TAB5_MEASURE_TIMEOUT);
    assert(nui_native_init(1280,720,pixels));
    char path[1024];snprintf(path,sizeof(path),"%s/geometry.tsv",argv[1]);
    FILE *geometry=fopen(path,"w");assert(geometry);
    fprintf(geometry,"theme\tcase\tfont_line_height\ttext_width\ttext_height\tlabel_x\tlabel_y\tlabel_w\tlabel_h\tclose_gap\n");
    for(unsigned theme=0;theme<2;theme++) {
        tab5_ui_init(NULL,NULL);
        tab5_ui_state_t state={.light_theme=theme,.measurement=core.state,.now_known=true,.now_us=1000000,
            .clock={.known=true,.time_valid=true,.datetime="27.10.2026 14:32"}};
        for(unsigned i=0;i<3;i++) {
            state.now_us+=100000;strcpy(state.status,messages[i]);tab5_ui_update(&state);
            assert(find_text(nui_screen_active(),"Пропусков подряд: 255 · проверьте питание и RS485"));
            nui_native_render();
            assert(!strcmp(nui_label_get_text(u.notice_text),messages[i]));
            const nui_font_t *font=nui_obj_get_style_text_font(u.notice_text,0);
            assert(font==F36);
            assert(nui_obj_get_height(u.notice_text)>=2*font->line_height);
            nui_area_t banner,label,close;
            nui_obj_get_coords(u.notice,&banner);nui_obj_get_coords(u.notice_text,&label);
            nui_obj_t *close_button=nui_obj_get_child(u.notice,1);
            nui_obj_get_coords(close_button,&close);
            nui_area_t screen={0,0,1279,719};
            assert(inside(banner,screen)&&inside(label,banner)&&inside(close,banner));
            assert(label.x2<close.x1 && close.x2-close.x1+1>=56);
            nui_point_t size;nui_text_get_size(&size,messages[i],font,0,0,
                                             nui_obj_get_width(u.notice_text),0);
            assert(size.x<=nui_obj_get_width(u.notice_text));
            assert(size.y<=nui_obj_get_height(u.notice_text));
            fprintf(geometry,"%s\t%u\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                theme?"light":"dark",i,font->line_height,size.x,size.y,label.x1,label.y1,
                nui_obj_get_width(u.notice_text),nui_obj_get_height(u.notice_text),close.x1-label.x2-1);
            snprintf(path,sizeof(path),"%s/notice_%s_%u.ppm",argv[1],theme?"light":"dark",i);
            FILE *image=fopen(path,"wb");assert(image);fprintf(image,"P6\n1280 720\n255\n");
            for(unsigned p=0;p<1280*720;p++) {
                unsigned v=pixels[p];unsigned char rgb[]={((v>>11)&31)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};
                fwrite(rgb,1,3,image);
            }
            fclose(image);
            nui_obj_send_event(close_button,NUI_EVENT_CLICKED,NULL);assert(!u.notice);
            tab5_ui_update(&state);assert(!u.notice); /* dismissed same message */
        }
        state.status[0]=0;tab5_ui_update(&state);assert(!u.notice);
        tab5_ui_destroy();
    }
    fclose(geometry);nui_native_deinit();
    uint16_t raw[2]={0,0};tab5_measure_core_sample(&core,0,raw,1000000);
    assert(core.state.sensor[0].valid&&core.state.sensor[0].fail_streak==0);
    assert(core.state.sensor[0].errors==300);
    puts("Notice: 36px, six real renders, both themes, full text fits, close bounds/dismissal PASS");
    puts("Unchanged sensor core: 300 timeouts -> fail_streak=255, errors=300; good sample resets streak only PASS");
    return 0;
}
'''


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="tab5-notice-") as directory:
        tmp = Path(directory)
        (tmp / "test.c").write_text(HARNESS, encoding="utf-8")
        sources = [MAIN / "native/native_ui.c", MAIN / "native/font_native_fallback14.c"]
        sources += [MAIN / name for name in (
            "tab5_measure.c", "tab5_ui_graph.c", "tab5_time.c", "tab5_plot.c", "tab5_level.c")]
        sources += sorted(MAIN.glob("font_tab5_*.c"))
        exe = tmp / "test.exe"
        subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra",
                        "-I" + str(MAIN), str(tmp / "test.c"),
                        *map(str, sources), "-lm", "-o", str(exe)], check=True)
        subprocess.run([str(exe), str(OUT)], check=True)
    for path in OUT.glob("notice_*.ppm"):
        with Image.open(path) as img:
            img.save(path.with_suffix(".png"))
    print((OUT / "geometry.tsv").read_text())


if __name__ == "__main__":
    main()
