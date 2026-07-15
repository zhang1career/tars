#ifndef TARS_PROBE_H
#define TARS_PROBE_H

/* Probe: a FreeRTOS task that speaks a minimal SCPI dialect over USART1
 * (ST-Link VCP, 115200 8N1). It periodically pushes telemetry metrics and, on
 * a trigger command, acquires a high-rate capture into SDRAM and streams it
 * back as a SCPI definite-length binary block. */

void TarsProbe_Init(void);
void TarsProbe_Task(void const *argument);

#endif /* TARS_PROBE_H */
