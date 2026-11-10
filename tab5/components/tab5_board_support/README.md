# Local Tab5 hardware support

Derived from Espressif m5stack_tab5 1.3.2, upstream esp-bsp commit f259a6abd5808019b1903a422092a4234ca95a51 (Apache-2.0, original SPDX headers retained).

Only I2C, IO expanders, feature power, SD, USB and low-level LCD/touch sources are built. High-level graphics integration and audio/sensor dependencies are removed. The three panel initialization tables and hardware pin assignments are retained. Native callers can select the original detected-board DSI lane rate with config rate zero (ST7121 965 Mbps; other supported panels 1000 Mbps). Touch constructor failure now releases its panel IO.

Production application panel wrappers continue to validate timings and correct ST7123 A3/E8 payload lengths. No upstream managed source is patched.
