#ifndef TAB5_INPUT_H
#define TAB5_INPUT_H
#include <stdbool.h>
#include <stdint.h>
/* Single owner; timestamps are monotonic uint32 milliseconds (wrap allowed).
 * Initialize from first physical sample, never synthesize a boot command. */
typedef enum { TAB5_INPUT_NONE, TAB5_INPUT_ON, TAB5_INPUT_OFF_LOCAL } tab5_input_event;
typedef struct { bool stable, candidate; uint32_t since, debounce_ms; } tab5_input_switch;
void tab5_input_switch_init(tab5_input_switch *, bool on, uint32_t now, uint32_t debounce_ms);
tab5_input_event tab5_input_switch_sample(tab5_input_switch *, bool on, uint32_t now);
/* AB encoded as (A << 1)|B; 00->01->11->10->00 is +1 detent.
 * Four valid edges per detent. Diagonal transitions discard partial motion. */
typedef struct { uint8_t previous; int8_t partial; } tab5_input_encoder;
void tab5_input_encoder_init(tab5_input_encoder *, uint8_t ab);
int tab5_input_encoder_sample(tab5_input_encoder *, uint8_t ab);
#endif
