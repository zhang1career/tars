#ifndef TARS_RES_GPIO_H
#define TARS_RES_GPIO_H

#include <stdint.h>

/* Button edge callback: pressed=1 on press edge, 0 on release edge.
 * Invoked from the resource task context after debounce. */
typedef void (*tars_button_cb_t)(int button_id, int pressed);

/* GPIO output (thread-safe, resolves name via pin map). */
int TarsResGpio_Write(const char *pin_name, int value);
int TarsResGpio_Read(const char *pin_name, int *value_out);

/* Register a debounced input button. Returns button id (>=0) or -1.
 * active_high: 1 if a pressed button reads logic high. */
int TarsResGpio_ButtonRegister(const char *pin_name,
                               uint8_t active_high,
                               uint16_t debounce_ms,
                               tars_button_cb_t cb);

/* Current debounced state of a button (1=pressed). */
int TarsResGpio_ButtonState(int button_id);

/* Sample all registered buttons; call once per resource tick. */
void TarsResGpio_SampleButtons(uint16_t tick_ms);

#endif /* TARS_RES_GPIO_H */
