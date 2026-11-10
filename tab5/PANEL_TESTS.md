# Production BSP panel regression tests

Verified 2026-10-22: PASS with host GCC and Python 3. Frozen test scope: test_panel_bsp.py, panel_*.c/.h and this document. No production/managed files changed by this test work. No IDF build, GUI, COM or hardware execution.

Run from repository root:

```
python tab5/tests/test_panel_bsp.py
python tab5/tests/test_panel_bsp.py --source-root C:/Projects/ClaudeExp/diplom_project_tab5
```

## Executable coverage

- 8 direct BSP scenarios: FW1, FW3, GT911 backup, read failure, unknown FW, IO-create failure and both touch-constructor failures.
- 11 actual adapter scenarios: the same eight plus reset/init/display-on failures leaving readiness false.
- Actual `bsp_get_board_version`, `bsp_display_new`, `bsp_display_new_with_handles`, `bsp_display_lcd_init` and `bsp_touch_new` bodies extracted verbatim. Constructor boundaries observe selected vendor/DPI/table. Cached identity remains stable after simulated identity changes without further probing/reads/detection delays.
- ST7121: 965 Mb/s, PCLK 70, V=24/20/200. ST7123: 1000 Mb/s, PCLK 70, V=8/2/220. ILI: 1000 Mb/s, PCLK 60, original timing. Touch remains 720x1280, no swap/mirrors, reset NC; GT911 backup-address/IRQ workaround preserved; no blind retry using a different driver.
- All 252 raw table records are audited without stopping at the first mismatch (ST7121=30, ST7123=27, ILI9881C=195). Exactly two expected raw OOB records: ST7123 A3 40 declared / 39 literal and E8 15 / 14. Unexpected additional violations fail the suite. Parsed bound-count must match compiled table-count.
- Two separate actual-driver raw negative tests isolate A3 and E8. The bounds-checking TX stub returns failure before reading beyond either literal. Actual driver propagates the error and does not initialize DPI afterward.
- Three complete actual-driver TX traces pass: `panel_st7123_init` for ST7121 and ST7123, and `panel_ili9881c_init` for ILI (including its six-command preamble and ID reads). Production driver structs, default tables and init bodies are compiled. The ILI default-table symbol alone is renamed to avoid collision in the host translation unit.
- Every transferred vendor record is checked against its original payload pointer and literal bounds BEFORE reading requested bytes. Each complete TX trace is SHA-256 checked against a pinned independent reference; no invented or padded byte is transmitted. Zero-length transfers remain present in the trace.
- Adapted ST7123 constructor receives a separate metadata array. Every command, payload pointer, delay and other length matches the original. Only A3 40->39 and E8 15->14 differ. Original compiled metadata remains 40/15. Entire adapted metadata has its own pinned SHA, separate from unchanged direct BSP baseline.
- Four temporary BSP mutations are rejected without adapter guards: ST7121 payload byte, VBP 24->8, DPI selection -> ST7123, init selection -> ST7123. Compile failure does not count as detection.
- Two temporary ST7123 payload mutations (A3 and E8) are rejected by actual adapter before the real constructor; no panel is created and readiness remains false.

## Method and limits

Actual `main/tab5_panel.c` is compiled with eight constructor/reset/init/on wrapper mappings. Hardware, IDF and LVGL interfaces use host stubs; `esp_lcd_video_timing_t` aliases the host timing struct. Actual managed vendor/BSP display typedefs and init headers are used. Each scenario has a fresh process and static selection cache. Actual init loops are invoked separately with the metadata captured at the production constructor boundary; this validates loop behavior and safe transfer contents, not ESP-IDF ABI compatibility or on-device electrical/timing behavior.

The A3/E8 correction is our tracked metadata adapter derived from official M5GFX command-inclusive lengths (40/15 => 39/14 parameters), confirmed by the parent task. This is not an upstream BSP fix. Tests preserve managed data and do not independently fetch M5GFX.

References are constants calculated separately from literal records, never regenerated during a test run. Metadata records serialize command:declared_length:delay:literal_length as 8-digit hex fields, all literal bytes as hex, and LF. Thus raw fingerprints retain the invalid declared lengths without attempting unsafe reads. TX fingerprints serialize TX:command:actual_transfer_length:actual_payload_hex and LF. Every declared transfer length is independently checked against its literal bound; metadata hashing does not substitute for bounds validation.

## Pinned fingerprints

- Raw ST7121 metadata: `ac36d9bcc3a40fcc568966c4d4d8dcca31617f3b5e4fa1d5015c6016279c1bb9`
- Raw ST7123 metadata: `47eefc3b74a4c78098f634da647857098431028780422c3f0a3c29122da7734c`
- Raw ILI metadata: `09bba76beaf70d9065729acb302dbf05646f76b7ef3a8aebb8dafcd5c9c0caca`
- Adapted ST7123 metadata: `cf967b925edf9323af079bd0a3af9429309c7515b45126857b12762e36ba3fb5`
- Actual TX st1: `6c10188a83f4c0e89120991fec495370025c788bf1924dbc2fa475278ca904a2`
- Actual TX st3: `86a45eb2fc04f9c310a2075130da838cebd1c18288fcfc4b623206fe8df59860`
- Actual TX ili: `8fd152fc78b6b99fe1ca27f06cfe752991b9ae6f900ce45043e3081b00753a2f`

## Source SHA-256 at freeze

- `tab5/managed_components/espressif__m5stack_tab5/src/bsp_display.c`: `4e3c415c22fc3a3222817086218b1f84d03f1f0dea00b67ebcc0a419982905a0`
- `tab5/managed_components/espressif__m5stack_tab5/include/bsp/display.h`: `05309d5848ff5438161621769f653ef28c03f499e3fea7a16ebf1f37dfe8bbe3`
- `tab5/managed_components/espressif__m5stack_tab5/priv_include/disp_init_data.h`: `7571c61a4e80e4e263d1d5e709746d98587b04522fd351c3f86c593393890f69`
- `tab5/managed_components/espressif__m5stack_tab5/priv_include/disp_init_data_1.h`: `748882dad015816d1ca83853297b1751ae3e722f5385911061a56fed927056e2`
- `tab5/managed_components/espressif__m5stack_tab5/priv_include/disp_init_data_st7121.h`: `7b8777fab8953df9d56b0717958542444d8e60513644b66cd78893d55adfd326`
- `tab5/managed_components/espressif__esp_lcd_st7123/esp_lcd_st7123.c`: `3321af30fa2375d20d3d16269c7a2941c218a0334985a388e2629ec930455b9f`
- `tab5/managed_components/espressif__esp_lcd_ili9881c/esp_lcd_ili9881c.c`: `56482051e7e909f16b0c6f2ee44980679625de7e57871611543d022c247dd0a3`
- `tab5/main/tab5_panel.c`: `7dc4c1370beeab086e56a97dba9a9acc7bec8b6f992657272bd927563387cd20`
