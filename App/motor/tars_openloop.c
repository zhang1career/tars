#include "tars_openloop.h"
#include "tars_hall6.h"
#include "tars_res_mgr.h"
#include "tars_res_pwm.h"
#include "tars_foc.h"

#define TARS_OPENLOOP_TENANT     "openloop"
#define TARS_OPENLOOP_DEFAULT_MS 45U
#define TARS_OPENLOOP_ISR_HZ      20000U
#define TARS_OPENLOOP_MAX_DUTY  10U

typedef enum {
  OL_OFF = 0,
  OL_LOW = 1,
  OL_PWM = 2
} ol_phase_mode_t;

typedef struct {
  ol_phase_mode_t u;
  ol_phase_mode_t v;
  ol_phase_mode_t w;
} ol_step_t;

/* Standard 120 deg Hall -> two-phase-on (U=pwm0/Y, V=pwm1/G, W=pwm2/B). */
static const ol_step_t s_table_cw[8] = {
  { OL_OFF, OL_OFF, OL_OFF },
  { OL_PWM, OL_OFF, OL_LOW },
  { OL_LOW, OL_PWM, OL_OFF },
  { OL_OFF, OL_PWM, OL_LOW },
  { OL_LOW, OL_OFF, OL_PWM },
  { OL_PWM, OL_LOW, OL_OFF },
  { OL_OFF, OL_LOW, OL_PWM },
  { OL_OFF, OL_OFF, OL_OFF },
};

static const uint8_t s_seq_cw[6] = { 5U, 1U, 3U, 2U, 6U, 4U };
static const uint8_t s_seq_ccw[6] = { 5U, 4U, 6U, 2U, 3U, 1U };

static tars_openloop_snapshot_t s_snap;
static volatile uint8_t s_enable;
static uint8_t s_initialized;
static uint8_t s_hall_sync;
static uint8_t s_last_hall_gpio;
static uint8_t s_hall_stable;
static uint8_t s_hall_candidate;
static uint8_t s_hall_debounce;
static uint8_t s_hall_invert;
static uint8_t s_kick_invert;
static int8_t s_hall_spin;
static uint8_t s_hall_wrong;
static uint8_t s_hall_phase;
static uint8_t s_hall_good_edges;
static uint8_t s_hall_locked;
static uint32_t s_step_div;
static uint32_t s_hall_last_edge_loop;

#define OL_HALL_DEBOUNCE     12U
#define OL_HALL_LOCK_EDGES   3U
#define OL_HALL_STALL_MS    120U
#define OL_HALL_STALL_TICKS ((OL_HALL_STALL_MS * TARS_OPENLOOP_ISR_HZ) / 1000U)
static volatile int s_last_enable_err;

static void ol_store(const tars_openloop_snapshot_t *src)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  s_snap = *src;
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static void ol_set_phase(const char *ch, ol_phase_mode_t mode, float duty)
{
  switch (mode)
  {
  case OL_PWM:
    (void)TarsResPwm_SetComplement(ch, 1);
    (void)TarsResPwm_SetDuty(ch, duty);
    break;
  case OL_LOW:
    (void)TarsResPwm_SetComplement(ch, 1);
    (void)TarsResPwm_SetDuty(ch, 0.0f);
    break;
  default:
    (void)TarsResPwm_SetComplement(ch, 0);
    (void)TarsResPwm_SetDuty(ch, 0.0f);
    break;
  }
}

static const ol_step_t *ol_lookup_step(uint8_t hall, uint8_t ccw)
{
  const ol_step_t *st;

  if (hall >= 8U)
  {
    return &s_table_cw[0];
  }

  st = &s_table_cw[hall];
  if (ccw == 0U)
  {
    return st;
  }

  static ol_step_t rev;
  rev.u = st->u;
  rev.v = st->v;
  rev.w = st->w;
  if (rev.u == OL_PWM) { rev.u = OL_LOW; }
  else if (rev.u == OL_LOW) { rev.u = OL_PWM; }
  if (rev.v == OL_PWM) { rev.v = OL_LOW; }
  else if (rev.v == OL_LOW) { rev.v = OL_PWM; }
  if (rev.w == OL_PWM) { rev.w = OL_LOW; }
  else if (rev.w == OL_LOW) { rev.w = OL_PWM; }
  return &rev;
}

static const uint8_t *ol_seq(uint8_t ccw)
{
  return (ccw != 0U) ? s_seq_ccw : s_seq_cw;
}

static void ol_apply_step3(uint8_t step, uint8_t duty_pct)
{
  float d0 = 0.0f;
  float d1 = 0.0f;
  float d2 = 0.0f;
  float d = (float)duty_pct;

  switch (step % 3U)
  {
  case 0U:
    d0 = d;
    break;
  case 1U:
    d1 = d;
    break;
  default:
    d2 = d;
    break;
  }

  (void)TarsResPwm_SetDuty("pwm0", d0);
  (void)TarsResPwm_SetDuty("pwm1", d1);
  (void)TarsResPwm_SetDuty("pwm2", d2);
}

static const uint8_t *ol_seq6(uint8_t ccw)
{
  /*
   * Hall6 labels seq_ccw as electrical CCW, but on this wiring (Y=pwm0, G=pwm1,
   * B=pwm2) advancing seq_ccw produces mechanical CW (opposite of 3-step CCW).
   * Match 3-step CCW by using seq_cw when the host asks for CCW.
   */
  return (ccw != 0U) ? s_seq_cw : s_seq_ccw;
}

static uint8_t ol_seq_index(const uint8_t *seq, uint8_t hall)
{
  uint8_t i;

  for (i = 0U; i < 6U; i++)
  {
    if (seq[i] == hall)
    {
      return i;
    }
  }
  return 0U;
}

static uint8_t ol_map_hall(uint8_t raw)
{
  const uint8_t *seq;

  if ((raw == 0U) || (raw == 7U))
  {
    return raw;
  }

  seq = ol_seq6(s_snap.direction);
  return seq[(ol_seq_index(seq, raw) + s_hall_phase) % 6U];
}

static uint8_t ol_lookup_commute(void)
{
  return s_hall_invert;
}

static const uint8_t *ol_kick_seq(void)
{
  if (s_snap.direction != 0U)
  {
    return s_kick_invert ? s_seq_ccw : s_seq_cw;
  }
  return s_kick_invert ? s_seq_cw : s_seq_ccw;
}

static int8_t ol_hall_spin_detect(uint8_t prev, uint8_t next)
{
  uint8_t ip;
  uint8_t in;

  if ((prev == 0U) || (prev == 7U) || (next == 0U) || (next == 7U))
  {
    return 0;
  }

  ip = ol_seq_index(s_seq_cw, prev);
  in = ol_seq_index(s_seq_cw, next);
  if (in == (uint8_t)((ip + 1U) % 6U))
  {
    return 1;
  }
  if (in == (uint8_t)((ip + 5U) % 6U))
  {
    return -1;
  }
  return 0;
}

static int ol_hall_edge_ok(int8_t spin)
{
  if (spin == 0)
  {
    return 0;
  }

  s_hall_spin = spin;
  if (s_snap.direction != 0U)
  {
    return (spin > 0) ? 1 : 0;
  }
  return (spin < 0) ? 1 : 0;
}

static void ol_hall_wrong_edge(void)
{
  if (s_hall_locked != 0U)
  {
    return;
  }

  s_hall_wrong++;
  if (s_hall_wrong >= 2U)
  {
    s_hall_invert ^= 1U;
    s_kick_invert ^= 1U;
    s_hall_wrong = 0U;
  }
}

static void ol_hall_good_edge(void)
{
  s_hall_last_edge_loop = s_snap.loop_count;

  if (s_hall_good_edges < 255U)
  {
    s_hall_good_edges++;
  }
  if (s_hall_good_edges >= OL_HALL_LOCK_EDGES)
  {
    s_hall_locked = 1U;
  }
}

static void ol_hall_stall_check(void)
{
  if ((s_hall_locked != 0U) &&
      ((s_snap.loop_count - s_hall_last_edge_loop) > OL_HALL_STALL_TICKS))
  {
    s_hall_locked = 0U;
    s_hall_good_edges = 0U;
    s_step_div = 0U;
  }
}

static void ol_apply_hall_commute(uint8_t table_hall)
{
  const ol_step_t *st;
  float d = (float)s_snap.duty_pct;

  if ((table_hall == 0U) || (table_hall == 7U))
  {
    return;
  }

  st = ol_lookup_step(table_hall, ol_lookup_commute());
  ol_set_phase("pwm0", st->u, d);
  ol_set_phase("pwm1", st->v, d);
  ol_set_phase("pwm2", st->w, d);
}

static void ol_apply_step6_kick(uint8_t step_idx, uint8_t duty_pct)
{
  const uint8_t *seq = ol_kick_seq();
  uint8_t hall = seq[step_idx % 6U];
  const ol_step_t *st = ol_lookup_step(hall, 0U);
  float d = (float)duty_pct;

  ol_set_phase("pwm0", st->u, d);
  ol_set_phase("pwm1", st->v, d);
  ol_set_phase("pwm2", st->w, d);
}

static void ol_apply_step6(uint8_t step_idx, uint8_t duty_pct)
{
  const uint8_t *seq = ol_seq6(s_snap.direction);
  uint8_t hall = seq[step_idx % 6U];
  const ol_step_t *st = ol_lookup_step(hall, 0U);
  float d = (float)duty_pct;

  ol_set_phase("pwm0", st->u, d);
  ol_set_phase("pwm1", st->v, d);
  ol_set_phase("pwm2", st->w, d);
}

static void ol_apply_step(uint8_t step, uint8_t duty_pct)
{
  if (s_snap.mode == TARS_OPENLOOP_MODE_6STEP)
  {
    ol_apply_step6(step, duty_pct);
  }
  else
  {
    ol_apply_step3(step, duty_pct);
  }
}

static int ol_start_pwm(void)
{
  if ((TarsResPwm_Enable("pwm0", 1) != 0) ||
      (TarsResPwm_Enable("pwm1", 1) != 0) ||
      (TarsResPwm_Enable("pwm2", 1) != 0))
  {
    (void)TarsResPwm_Enable("pwm0", 0);
    (void)TarsResPwm_Enable("pwm1", 0);
    (void)TarsResPwm_Enable("pwm2", 0);
    return 0;
  }

  if (s_snap.mode != TARS_OPENLOOP_MODE_6STEP && s_hall_sync == 0U)
  {
    if ((TarsResPwm_SetComplement("pwm0", 1) != 0) ||
        (TarsResPwm_SetComplement("pwm1", 1) != 0) ||
        (TarsResPwm_SetComplement("pwm2", 1) != 0))
    {
      (void)TarsResPwm_Enable("pwm0", 0);
      (void)TarsResPwm_Enable("pwm1", 0);
      (void)TarsResPwm_Enable("pwm2", 0);
      return 0;
    }
  }

  return 1;
}

static void ol_stop_pwm(void)
{
  (void)TarsResPwm_Enable("pwm0", 0);
  (void)TarsResPwm_Enable("pwm1", 0);
  (void)TarsResPwm_Enable("pwm2", 0);
  (void)TarsResPwm_Tim1ForceSafe();
}

static uint8_t ol_next_step(uint8_t step)
{
  if (s_snap.mode == TARS_OPENLOOP_MODE_6STEP)
  {
    return (uint8_t)((step + 1U) % 6U);
  }

  if (s_hall_sync != 0U)
  {
    return (uint8_t)((step + 1U) % 6U);
  }

  if (s_snap.direction != 0U)
  {
    return (uint8_t)((step + 1U) % 3U);
  }
  return (uint8_t)((step + 2U) % 3U);
}

void TarsOpenloop_Init(void)
{
  if (s_initialized != 0U)
  {
    return;
  }

  s_snap.duty_pct = 6U;
  s_snap.step_ms = TARS_OPENLOOP_DEFAULT_MS;
  s_snap.ramp_start_ms = 0U;
  s_snap.ramp_end_ms = 0U;
  s_snap.ramp_ms = 0U;
  s_snap.direction = 1U;
  s_snap.mode = TARS_OPENLOOP_MODE_3STEP;
  s_snap.step = 0U;
  s_hall_sync = 0U;
  s_last_hall_gpio = 0xFFU;
  s_hall_stable = 0U;
  s_hall_candidate = 0U;
  s_hall_debounce = 0U;
  s_hall_invert = 0U;
  s_kick_invert = 0U;
  s_hall_spin = 0;
  s_hall_wrong = 0U;
  s_hall_good_edges = 0U;
  s_hall_locked = 0U;
  s_hall_last_edge_loop = 0U;
  s_hall_phase = 3U;
  s_enable = 0U;
  s_initialized = 1U;
}

int TarsOpenloop_LastEnableError(void)
{
  return s_last_enable_err;
}

int TarsOpenloop_IsEnabled(void)
{
  return (s_enable != 0U) ? 1 : 0;
}

void TarsOpenloop_SetDutyPct(uint8_t pct)
{
  if (pct > TARS_OPENLOOP_MAX_DUTY)
  {
    pct = TARS_OPENLOOP_MAX_DUTY;
  }
  s_snap.duty_pct = pct;
}

void TarsOpenloop_SetStepMs(uint16_t ms)
{
  if (ms < 5U)
  {
    ms = 5U;
  }
  if (ms > 500U)
  {
    ms = 500U;
  }
  s_snap.step_ms = ms;
}

void TarsOpenloop_SetRampMs(uint16_t start_ms, uint16_t end_ms, uint16_t ramp_ms)
{
  if (start_ms < 5U) { start_ms = 5U; }
  if (end_ms < 5U) { end_ms = 5U; }
  if (start_ms > 500U) { start_ms = 500U; }
  if (end_ms > 500U) { end_ms = 500U; }
  s_snap.ramp_start_ms = start_ms;
  s_snap.ramp_end_ms = end_ms;
  s_snap.ramp_ms = ramp_ms;
  s_snap.step_ms = end_ms;
}

static uint16_t ol_effective_step_ms(void)
{
  uint32_t elapsed_ms;

  if (s_snap.ramp_ms == 0U)
  {
    return s_snap.step_ms;
  }

  elapsed_ms = s_snap.loop_count / (TARS_OPENLOOP_ISR_HZ / 1000U);
  if (elapsed_ms >= (uint32_t)s_snap.ramp_ms)
  {
    return s_snap.ramp_end_ms;
  }

  {
    uint32_t start = (uint32_t)s_snap.ramp_start_ms;
    uint32_t end = (uint32_t)s_snap.ramp_end_ms;
    uint32_t span = (uint32_t)s_snap.ramp_ms;
    int32_t delta = (int32_t)end - (int32_t)start;
    return (uint16_t)(start + ((delta * (int32_t)elapsed_ms) / (int32_t)span));
  }
}

void TarsOpenloop_SetDirection(int ccw)
{
  s_snap.direction = (ccw != 0) ? 1U : 0U;
}

void TarsOpenloop_SetMode(uint8_t mode)
{
  if (mode == TARS_OPENLOOP_MODE_6STEP)
  {
    s_snap.mode = TARS_OPENLOOP_MODE_6STEP;
  }
  else
  {
    s_snap.mode = TARS_OPENLOOP_MODE_3STEP;
  }
}

uint8_t TarsOpenloop_GetMode(void)
{
  return s_snap.mode;
}

void TarsOpenloop_SetHallSync(int enable)
{
  s_hall_sync = (enable != 0) ? 1U : 0U;
  s_last_hall_gpio = 0xFFU;
  s_hall_stable = 0U;
  s_hall_candidate = 0U;
  s_hall_debounce = 0U;
  s_hall_invert = 0U;
  s_kick_invert = 0U;
  s_hall_spin = 0;
  s_hall_wrong = 0U;
  s_hall_good_edges = 0U;
  s_hall_locked = 0U;
}

void TarsOpenloop_SetHallPhase(uint8_t phase)
{
  s_hall_phase = (uint8_t)(phase % 6U);
}

int TarsOpenloop_IsHallSync(void)
{
  return (s_hall_sync != 0U) ? 1 : 0;
}

void TarsOpenloop_GetSnapshot(tars_openloop_snapshot_t *out)
{
  uint32_t primask;

  if (out == NULL)
  {
    return;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  *out = s_snap;
  if (primask == 0U)
  {
    __enable_irq();
  }
}

int TarsOpenloop_Enable(int enable)
{
  tars_openloop_snapshot_t snap;

  if (enable != 0)
  {
    s_last_enable_err = 0;

    if (TarsFoc_IsEnabled() != 0)
    {
      s_last_enable_err = 1;
      return 0;
    }
    if (TarsHall6_IsEnabled() != 0)
    {
      s_last_enable_err = 2;
      return 0;
    }

    ol_stop_pwm();

    if (ol_start_pwm() == 0)
    {
      ol_stop_pwm();
      s_last_enable_err = 4;
      return 0;
    }

    s_step_div = 0U;
    s_enable = 1U;
    s_hall_invert = 0U;
    s_kick_invert = 0U;
    s_hall_spin = 0;
    s_hall_wrong = 0U;
    s_hall_good_edges = 0U;
    s_hall_locked = 0U;
    s_hall_last_edge_loop = 0U;

    snap = s_snap;
    snap.enabled = 1U;
    snap.step = 0U;
    snap.step_count = 0U;
    snap.loop_count = 0U;
    ol_store(&snap);

    ol_apply_step(0U, s_snap.duty_pct);
    if (s_hall_sync != 0U)
    {
      uint8_t hall = TarsHall6_ReadHallRaw();

      if ((hall != 0U) && (hall != 7U))
      {
        s_hall_stable = hall;
        s_hall_candidate = hall;
        s_hall_debounce = 0U;
        s_last_hall_gpio = hall;
        snap.step = ol_seq_index(ol_seq6(s_snap.direction), hall);
        ol_store(&snap);
        ol_apply_hall_commute(ol_map_hall(hall));
      }
    }
    return 1;
  }

  s_enable = 0U;
  ol_stop_pwm();

  snap = s_snap;
  snap.enabled = 0U;
  ol_store(&snap);
  return 1;
}

void TarsOpenloop_ControlLoopISR(void)
{
  tars_openloop_snapshot_t snap;
  uint32_t ticks;

  if (s_enable == 0U)
  {
    return;
  }

  if (s_hall_sync != 0U)
  {
    uint8_t hall = TarsHall6_ReadHallRaw();
    uint8_t hall_stepped = 0U;

    if ((hall != 0U) && (hall != 7U))
    {
      if (hall == s_hall_stable)
      {
        s_hall_debounce = 0U;
      }
      else if (hall == s_hall_candidate)
      {
        s_hall_debounce++;
        if (s_hall_debounce >= OL_HALL_DEBOUNCE)
        {
          int8_t spin = ol_hall_spin_detect(s_hall_stable, hall);

          if (ol_hall_edge_ok(spin) != 0)
          {
            s_hall_wrong = 0U;
            ol_hall_good_edge();
            s_last_hall_gpio = hall;
            snap = s_snap;
            snap.step = ol_seq_index(ol_seq6(s_snap.direction), hall);
            snap.step_count++;
            ol_store(&snap);
            ol_apply_hall_commute(ol_map_hall(hall));
            hall_stepped = 1U;
          }
          else if (spin != 0)
          {
            ol_hall_wrong_edge();
          }
          s_hall_stable = hall;
          s_hall_debounce = 0U;
        }
      }
      else
      {
        s_hall_candidate = hall;
        s_hall_debounce = 1U;
      }
    }

    ol_hall_stall_check();

    if ((hall_stepped == 0U) && (s_hall_locked == 0U))
    {
      ticks = ((uint32_t)ol_effective_step_ms() * TARS_OPENLOOP_ISR_HZ) / 1000U;
      if (ticks < 1U)
      {
        ticks = 1U;
      }

      s_step_div++;
      if (s_step_div >= ticks)
      {
        s_step_div = 0U;
        snap = s_snap;
        snap.step = ol_next_step(snap.step);
        snap.step_count++;
        ol_store(&snap);
        ol_apply_step6_kick(s_snap.step, s_snap.duty_pct);
      }
    }

    snap = s_snap;
    snap.hall_raw = hall;
    snap.hall_spin = s_hall_spin;
    snap.hall_invert = s_hall_invert;
    snap.hall_sync_on = 1U;
    snap.hall_locked = s_hall_locked;
    snap.loop_count++;
    ol_store(&snap);
    return;
  }

  ticks = ((uint32_t)ol_effective_step_ms() * TARS_OPENLOOP_ISR_HZ) / 1000U;
  if (ticks < 1U)
  {
    ticks = 1U;
  }

  s_step_div++;
  if (s_step_div >= ticks)
  {
    s_step_div = 0U;
    snap = s_snap;
    snap.step = ol_next_step(snap.step);
    snap.step_count++;
    ol_store(&snap);
    ol_apply_step(s_snap.step, s_snap.duty_pct);
  }

  snap = s_snap;
  snap.loop_count++;
  ol_store(&snap);
}
