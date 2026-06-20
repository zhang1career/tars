#ifndef TARS_PROBE_METRICS_H
#define TARS_PROBE_METRICS_H

#include <stdint.h>

/* Provider tokens. Must match the "provider" column in tools/probe/metrics.csv
 * (codegen emits TARS_PROBE_SRC_<token>). Add a token here and a case in
 * TarsProbeMetrics_Read() to expose a new metric source. */
typedef enum {
  TARS_PROBE_SRC_UPTIME = 0,
  TARS_PROBE_SRC_TICK_HZ,
  TARS_PROBE_SRC_HEAP_FREE,
  TARS_PROBE_SRC_HEAP_MIN_FREE,
  TARS_PROBE_SRC_LUA_HEAP_USED,
  TARS_PROBE_SRC_TASK_COUNT,
  TARS_PROBE_SRC_MOTOR_POS,
  TARS_PROBE_SRC_MOTOR_VEL,
  TARS_PROBE_SRC_MOTOR_LOOPS,
  TARS_PROBE_SRC_BUTTON
} tars_probe_src_t;

typedef struct {
  const char *name;
  const char *unit;
  uint8_t     provider;
  uint32_t    arg;
} tars_probe_metric_t;

/* Generated table (generated/probe/metrics.c). */
extern const tars_probe_metric_t g_probe_metrics[];
extern const uint32_t g_probe_metric_count;

/* Read one metric as a float (hand-written provider switch). */
float TarsProbeMetrics_Read(uint8_t provider, uint32_t arg);

#endif /* TARS_PROBE_METRICS_H */
