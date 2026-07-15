#ifndef TARS_PROBE_CAPTURE_H
#define TARS_PROBE_CAPTURE_H

#include <stdint.h>

/* Trigger-capture engine.
 *
 * MVP: the high-rate source is synthesized into the SDRAM capture buffer
 * (TARS_PROBE_CAP_BASE). The pipeline (fill buffer -> stream out over UART DMA
 * as a SCPI definite-length block) is identical to the future real path, where
 * the fill step is replaced by ADC -> DMA at the configured sample rate. */

typedef enum {
  TARS_PROBE_CAP_IDLE = 0,
  TARS_PROBE_CAP_READY = 1
} tars_probe_cap_state_t;

void TarsProbeCapture_Init(void);

void TarsProbeCapture_SetDepth(uint32_t bytes);
void TarsProbeCapture_SetRate(uint32_t hz);
uint32_t TarsProbeCapture_Depth(void);
uint32_t TarsProbeCapture_Rate(void);
tars_probe_cap_state_t TarsProbeCapture_State(void);

/* Acquire one capture into SDRAM. Returns 0 on success. */
int TarsProbeCapture_Trigger(void);

/* Stream the last capture out as a SCPI binary block. Returns 0 on success. */
int TarsProbeCapture_Stream(void);

#endif /* TARS_PROBE_CAPTURE_H */
