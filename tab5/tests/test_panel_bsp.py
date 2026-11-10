"""Host executable tests of unmodified BSP functions. Requires Python 3 and GCC."""
import argparse
import hashlib
import pathlib
import re
import shutil
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
# Canonical stream: cmd:declared_length:delay:literal_length (8-digit hex), payload hex, LF.
# Fixed reference values were calculated separately from literal records, not harness output.
EXPECTED = {'fw1': 'ac36d9bcc3a40fcc568966c4d4d8dcca31617f3b5e4fa1d5015c6016279c1bb9', 'fw3': '47eefc3b74a4c78098f634da647857098431028780422c3f0a3c29122da7734c', 'gt911': '09bba76beaf70d9065729acb302dbf05646f76b7ef3a8aebb8dafcd5c9c0caca', 'readfail': '7cc56ae813810c0c17f72177bd9770564a360b8b5e078aa16fa1b37371a43549', 'unknown': '7cc56ae813810c0c17f72177bd9770564a360b8b5e078aa16fa1b37371a43549', 'iofail': '7cc56ae813810c0c17f72177bd9770564a360b8b5e078aa16fa1b37371a43549'} # pinned independently; never regenerated during a test run

WRAPPED_FW3 = 'cf967b925edf9323af079bd0a3af9429309c7515b45126857b12762e36ba3fb5'

DRIVER_TX = {'st1': '6c10188a83f4c0e89120991fec495370025c788bf1924dbc2fa475278ca904a2', 'st3': '86a45eb2fc04f9c310a2075130da838cebd1c18288fcfc4b623206fe8df59860', 'ili': '8fd152fc78b6b99fe1ca27f06cfe752991b9ae6f900ce45043e3081b00753a2f'}

FUNCTIONS = ('bsp_get_board_version', 'bsp_display_new', 'bsp_display_new_with_handles', 'bsp_touch_new')

def function(text, name):
    m = re.search(r'^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\s*\{', text, re.M)
    if not m:
        raise AssertionError('Missing production function: ' + name)
    start = m.start()
    # Brace counting after removing comments and literals, preserving offsets.
    masked = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', lambda x: ' ' * len(x[0]), text, flags=re.S)
    depth = 1
    for i in range(m.end(), len(text)):
        depth += (masked[i] == '{') - (masked[i] == '}')
        if depth == 0:
            return text[start:i+1]
    raise AssertionError('Unbalanced function: ' + name)

def typedefs(path):
    return '\n'.join(re.findall(r'typedef struct\s*\{.*?^\}\s*\w+;', path.read_text(), re.M | re.S))

def build(root, out, wrapped=False):
    mc = root / 'tab5/managed_components'
    bsp = root / 'tab5/components/tab5_board_support'
    src = (bsp / 'src/bsp_display.c').read_text()
    enum = re.search(r'typedef enum\s*\{.*?\} bsp_tab5_board_version_t;', src, re.S)[0]
    display = (bsp / 'include/bsp/display.h').read_text()
    defs = '\n'.join(x for x in display.splitlines() if x.startswith('#define '))
    lane = re.search(r'^#define BSP_LCD_ST712X_MIPI_DSI_LANE_BITRATE_MBPS .*', src, re.M)[0]
    symbols = ('esp_lcd_new_dsi_bus','esp_lcd_new_panel_st7123','esp_lcd_new_panel_ili9881c','esp_lcd_touch_new_i2c_st7123','esp_lcd_touch_new_i2c_gt911','esp_lcd_panel_reset','esp_lcd_panel_init','esp_lcd_panel_disp_on_off')
    stubs = (HERE / 'panel_stubs.h').read_text()
    if wrapped:
        for symbol in symbols:
            stubs = re.sub(r'\b'+symbol+r'\b', '__real_'+symbol, stubs)
    code = '#define PANEL_WRAPPED '+str(int(wrapped))+'\n#define test_panel_ready() '+('tab5_panel_ready()' if wrapped else 'false')+'\n' + stubs + '\n' + defs + '\n' + lane + '\n' + enum
    for component, header in [('esp_lcd_st7123','esp_lcd_st7123.h'), ('esp_lcd_ili9881c','esp_lcd_ili9881c.h')]:
        code += '\n' + typedefs(mc / ('espressif__'+component) / 'include' / header)
    code += '\n' + typedefs(bsp / 'include/bsp/display.h')
    for header in ('disp_init_data.h','disp_init_data_1.h','disp_init_data_st7121.h'):
        code += '\n#include "' + (bsp / 'priv_include' / header).as_posix() + '"\n'
        raw = re.sub(r'//[^\n]*|/\*.*?\*/', '', (bsp / 'priv_include' / header).read_text(), flags=re.S)
        sizes = [len([v for v in m.split(',') if v.strip()]) for m in re.findall(r'(?:\(uint8_t\s*\[\]\)\s*\{([^}]*)\}|NULL)\s*,\s*\d+\s*,\s*\d+\s*\}', raw)]
        symbol = {'disp_init_data.h':'ili','disp_init_data_1.h':'st3','disp_init_data_st7121.h':'st1'}[header]
        code += '\nstatic const size_t sizes_' + symbol + '[]={' + ','.join(map(str,sizes)) + '};\n'
    harness = (HERE / 'panel_harness.c').read_text()
    # Exercise the production low-level entry point used by native_port (zero
    # bitrate selects the detected panel), then its explicit display-on step.
    # The former graphics-only bsp_display_lcd_init no longer exists.
    harness = harness.replace('bsp_display_cfg_t c={.buffer_size=720};',
                              'bsp_display_config_t c={0}; bsp_lcd_handles_t h={0};')
    harness = harness.replace('!bsp_display_lcd_init(&c)',
                              'bsp_display_new_with_handles(&c,&h)!=ESP_OK')
    harness = harness.replace('assert(bsp_display_lcd_init(&c));',
        'int result=bsp_display_new_with_handles(&c,&h);'
        'assert(result==((resetfail||initfail)?-1:ESP_OK));'
        'if(result==ESP_OK)assert(esp_lcd_panel_disp_on_off(h.panel,true)==(onfail?-1:ESP_OK));')
    # Failed touch creation now correctly deletes its freshly allocated IO.
    harness = harness.replace('dels==(board==1?0:1)', 'dels==((board==1?0:1)+touchfail)')
    driver_source = (mc / 'espressif__esp_lcd_st7123/esp_lcd_st7123.c').read_text()
    driver_type = re.search(r'typedef struct\s*\{.*?^\} st7123_panel_t;', driver_source, re.M | re.S)[0]
    driver_default = re.search(r'static const st7123_lcd_init_cmd_t vendor_specific_init_default\[\] = \{.*?^\};', driver_source, re.M | re.S)[0]
    driver = (HERE / 'panel_driver.c').read_text().replace('/* DRIVER_PRODUCTION_TYPE */', driver_type)
    driver = driver.replace('/* DRIVER_PRODUCTION_DEFAULT */', driver_default)
    driver = driver.replace('/* DRIVER_PRODUCTION_INIT */', function(driver_source, 'panel_st7123_init'))
    ili_source = (mc / 'espressif__esp_lcd_ili9881c/esp_lcd_ili9881c.c').read_text()
    ili_type = re.search(r'typedef struct\s*\{.*?^\} ili9881c_panel_t;', ili_source, re.M | re.S)[0]
    ili_defines = '\n'.join(x for x in ili_source.splitlines() if x.startswith('#define ILI9881C_'))
    ili_default = re.search(r'static const ili9881c_lcd_init_cmd_t vendor_specific_init_default\[\] = \{.*?^\};', ili_source, re.M | re.S)[0]
    ili_default = ili_default.replace('vendor_specific_init_default', 'ili_vendor_specific_init_default')
    ili_init = function(ili_source, 'panel_ili9881c_init').replace('vendor_specific_init_default', 'ili_vendor_specific_init_default')
    driver = driver.replace('/* ILI_PRODUCTION */', ili_defines+'\n'+ili_type+'\n'+ili_default)
    driver = driver.replace('/* ILI_PRODUCTION_INIT */', ili_init)
    harness = harness.replace('/* DRIVER_FIXTURE */', driver)
    production = '\n'.join(function(src,n) for n in FUNCTIONS)
    if wrapped:
        for symbol in symbols:
            harness = re.sub(r'\b'+symbol+r'\b', '__real_'+symbol, harness)
            production = re.sub(r'\b'+symbol+r'\b', '__wrap_'+symbol, production)
        harness = harness.replace('__real_esp_lcd_panel_disp_on_off(h.panel,true)', '__wrap_esp_lcd_panel_disp_on_off(h.panel,true)')
        wrapper = (root / 'tab5/main/tab5_panel.c').read_text()
        wrapper = re.sub(r'^#include "[^"\n]+"\s*$', '', wrapper, flags=re.M)
        production = wrapper + '\n' + production
        harness = harness.replace('void *t=NULL;assert(bsp_touch_new', 'void *t=NULL;assert(!tab5_panel_ready());assert(bsp_touch_new')
        harness = harness.replace('assert(touches==1&&', 'assert(tab5_panel_ready()==!(touchfail||resetfail||initfail||onfail));assert(touches==1&&')
    code += harness.replace('/* PRODUCTION_FUNCTIONS */', production)
    c = out / 'panel.c'
    c.write_text(code)
    exe = out / 'panel.exe'
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror=implicit-function-declaration','-Werror=incompatible-pointer-types',str(c),'-o',str(exe)], check=True, capture_output=True, text=True)
    return exe

def run(exe, scenario):
    p = subprocess.run([str(exe),scenario], capture_output=True, timeout=10)
    if p.returncode:
        raise AssertionError(f'{scenario}: executable failed ({p.returncode}): {p.stderr.decode(errors="replace")}')
    data = p.stdout.replace(b'\r\n',b'\n')
    return hashlib.sha256(data).hexdigest()

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source-root', type=pathlib.Path, default=HERE.parents[1])
    args = ap.parse_args()
    root = args.source_root.resolve()
    with tempfile.TemporaryDirectory(prefix='panel-bsp-') as td:
        temp = pathlib.Path(td)
        exe = build(root,temp)
        for scenario in ('fw1','fw3','gt911','readfail','unknown','iofail','touchfail','gtfail'):
            digest = run(exe,scenario)
            key = {'touchfail':'fw1','gtfail':'gt911','resetfail':'fw1','initfail':'fw1','onfail':'fw1'}.get(scenario,scenario)
            assert digest == EXPECTED[key], f'{scenario}: full init SHA mismatch: {digest}'
            print('PASS',scenario,digest)
        wrapped_dir = temp / 'wrapped'
        wrapped_dir.mkdir()
        wrapped = build(root, wrapped_dir, wrapped=True)
        for scenario in ('fw1','fw3','gt911','readfail','unknown','iofail','touchfail','gtfail','resetfail','initfail','onfail'):
            key = {'touchfail':'fw1','gtfail':'gt911','resetfail':'fw1','initfail':'fw1','onfail':'fw1'}.get(scenario,scenario)
            assert run(wrapped, scenario) == (WRAPPED_FW3 if key == 'fw3' else EXPECTED[key])
            print('PASS wrapped', scenario)
        for scenario,command in [('drivera3raw','A3'),('drivere8raw','E8')]:
            assert run(exe, scenario) == hashlib.sha256(('BOUNDS_REJECT_'+command+'\n').encode()).hexdigest()
            print('PASS actual driver raw bounds rejection', command)
        for scenario,key in [('driverst1','st1'),('driverfixed','st3'),('driverili','ili')]:
            assert run(wrapped, scenario) == DRIVER_TX[key], 'Full corrected driver TX trace mismatch: '+key
            print('PASS actual driver full TX trace', key)
        mutations = {
            'ST7121 payload byte': ('0x60, (uint8_t[]){0x71, 0x21, 0xA2}', '0x60, (uint8_t[]){0x71, 0x21, 0xA3}'),
            'ST7121 timings': ('.vsync_back_porch  = 24,','.vsync_back_porch  = 8,'),
            'ST7121 DPI selection': ('.dpi_config = &dpi_config_st7121,','.dpi_config = &dpi_config_st7123,'),
            'ST7121 init selection': ('.init_cmds      = disp_init_data_st7121,','.init_cmds      = disp_init_data_st7123,'),
        }
        for index,(label,(old,new)) in enumerate(mutations.items()):
            copy = temp / ('mutation'+str(index))
            shutil.copytree(root/'tab5/components/tab5_board_support',copy/'tab5/components/tab5_board_support')
            for component in ('espressif__esp_lcd_st7123','espressif__esp_lcd_ili9881c'):
                shutil.copytree(root/'tab5/managed_components'/component,copy/'tab5/managed_components'/component)
            source = copy/'tab5/components/tab5_board_support/src/bsp_display.c'
            if label == 'ST7121 payload byte':
                source = copy/'tab5/components/tab5_board_support/priv_include/disp_init_data_st7121.h'
            text = source.read_text()
            assert text.count(old)==1, 'Mutation anchor changed: '+label
            source.write_text(text.replace(old,new))
            mutated = build(copy,copy)
            try:
                digest=run(mutated,'fw1')
                assert digest==EXPECTED['fw1'], 'init SHA mismatch'
            except AssertionError:
                print('PASS mutation rejected:',label)
            else:
                raise AssertionError('Surviving mutation: '+label)
        for label,old,new in [
            ('e8','{0xE8, (uint8_t[]){0x20, 0x6F','{0xE8, (uint8_t[]){0x21, 0x6F'),
            ('a3','{0xA3, (uint8_t[]){0x80, 0x01','{0xA3, (uint8_t[]){0x81, 0x01')]:
            copy = temp / (label+'negative')
            shutil.copytree(root/'tab5/components/tab5_board_support',copy/'tab5/components/tab5_board_support')
            for component in ('espressif__esp_lcd_st7123','espressif__esp_lcd_ili9881c'):
                shutil.copytree(root/'tab5/managed_components'/component,copy/'tab5/managed_components'/component)
            (copy/'tab5/main').mkdir(parents=True)
            shutil.copy2(root/'tab5/main/tab5_panel.c',copy/'tab5/main/tab5_panel.c')
            source = copy/'tab5/components/tab5_board_support/priv_include/disp_init_data_1.h'
            text = source.read_text()
            assert text.count(old)==1
            source.write_text(text.replace(old,new))
            negative = build(copy, copy, wrapped=True)
            assert run(negative,label+'changed') == hashlib.sha256(b'REJECTED\n').hexdigest()
            print('PASS changed '+label.upper()+' payload rejected before real constructor')
    print('PASS production BSP regression suite')

if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as e:
        raise SystemExit(e.stderr)
