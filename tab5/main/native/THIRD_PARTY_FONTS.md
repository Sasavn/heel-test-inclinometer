# Retained fallback font data

`font_native_fallback14.c` contains generated 4bpp bitmap and metrics data
from LVGL 9.3.0's `src/font/lv_font_montserrat_14.c`. The generated data are
retained to preserve the appearance and advance widths of keyboard symbols
and the previous default face. Descriptor names/includes are adapted to the
independent native renderer; no LVGL font engine code is included.

The upstream generated-data distribution's MIT notice is retained at
`../../assets/fonts/LICENSE_NATIVE_GLYPH_DATA_MIT.txt`.
The Montserrat and Font Awesome source-font notices are retained in the
same directory as `LICENSE_NATIVE_Montserrat_*` and
`LICENSE_NATIVE_FontAwesome5_*`.

Main UI faces remain the existing Roboto and DejaVu fonts with their
existing notices in `tab5/assets/fonts` and `pc/assets/fonts`.
