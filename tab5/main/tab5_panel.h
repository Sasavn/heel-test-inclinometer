#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Observes the ACTUAL configurations passed by the pinned BSP to LCD/touch
 * constructors. It never probes, toggles reset, changes a profile or retries. */
bool tab5_panel_ready(void);
/* Clear observer state before a fresh native-port startup attempt. */
void tab5_panel_observer_reset(void);
void tab5_panel_describe(char *dest, size_t capacity);
