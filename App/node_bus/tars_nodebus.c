#include "node_bus/tars_nodebus.h"
#include "node_bus/tnb_crc.h"
#include "i2c.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Shell：`nodebus scan|list|health <a>|caps <a>|mux <a> <ch>`
 * 总线：DISC1 电机版 I2C2（PB10=SCL, PB11=SDA），见 MX_I2C2_Init / pinmap。
 */

#define NB_TIMEOUT_MS   200U

static I2C_HandleTypeDef *s_hi2c;
static tars_node_t        s_nodes[TARS_NODEBUS_MAX_NODES];
static uint8_t            s_count;

void TarsNodeBus_Init(I2C_HandleTypeDef *hi2c)
{
  s_hi2c = hi2c;
  memset(s_nodes, 0, sizeof(s_nodes));
  s_count = 0U;
}

/*
 * After a timed-out HAL transfer, I2C2 can sit BUSY with SB/START pending and
 * both lines driven low. Cube MspDeInit only knows I2C3, so recover via
 * MX_I2C2_BusRelease + 9 SCL clocks (unwedge soft slaves) + re-init.
 */
static void nb_bus_recover(void)
{
  MX_I2C2_BusUnstick();
  MX_I2C2_Init();
  /* Rebind handle only — do not wipe s_nodes (scan/list must survive recover). */
  s_hi2c = &hi2c2;
}

static uint8_t nb_bus_lines_low(void)
{
  uint32_t idr = GPIOB->IDR;
  return ((((idr >> 10) & 1U) == 0U) || (((idr >> 11) & 1U) == 0U)) ? 1U : 0U;
}

static uint8_t nb_bus_needs_recover(void)
{
  if (s_hi2c == NULL)
  {
    return 0U;
  }
  if (__HAL_I2C_GET_FLAG(s_hi2c, I2C_FLAG_BUSY) != RESET)
  {
    return 1U;
  }
  if (s_hi2c->State != HAL_I2C_STATE_READY)
  {
    return 1U;
  }
  /*
   * Do not treat low IDR alone as "stuck": PB10/PB11 are open-drain with
   * GPIO_NOPULL — without external pull-ups (io-mux R5/R6) the lines float
   * near 0 V while still Hi-Z. Only recover on low lines after a HAL error.
   */
  if ((s_hi2c->ErrorCode != HAL_I2C_ERROR_NONE) && (nb_bus_lines_low() != 0U))
  {
    return 1U;
  }
  return 0U;
}

static void nb_bus_ensure(void)
{
  if (nb_bus_needs_recover() != 0U)
  {
    nb_bus_recover();
  }
}

/* AF (NACK) alone is normal when probing empty addresses; recover if lines stay low. */
static void nb_clear_af(void)
{
  if ((s_hi2c != NULL) && (s_hi2c->ErrorCode == HAL_I2C_ERROR_AF))
  {
    s_hi2c->ErrorCode = HAL_I2C_ERROR_NONE;
    if (s_hi2c->State != HAL_I2C_STATE_READY)
    {
      s_hi2c->State = HAL_I2C_STATE_READY;
    }
  }
}

static tars_status_t nb_after_hal_fail(void)
{
  if ((s_hi2c != NULL) && (s_hi2c->ErrorCode == HAL_I2C_ERROR_AF))
  {
    nb_clear_af();
    /* NACK is normal on empty addresses; recover only if HW still BUSY. */
    if (__HAL_I2C_GET_FLAG(s_hi2c, I2C_FLAG_BUSY) != RESET)
    {
      nb_bus_recover();
    }
    return TARS_ERR_STATE;
  }
  nb_bus_recover();
  return TARS_ERR_STATE;
}

/* Caps: known node → from PROFILE at identity; unknown → LITE (normative default). */
static void nb_caps_lookup(uint8_t addr, uint8_t *max_w, uint8_t *max_r, uint8_t *flags)
{
  const tars_node_t *n = TarsNodeBus_Find(addr);

  if ((n != NULL) && (n->present != 0U) && (n->max_write_payload != 0U))
  {
    *max_w = n->max_write_payload;
    *max_r = n->max_read_burst;
    *flags = n->xfer_flags;
    return;
  }
  *max_w = TNB_MAX_WRITE_PAYLOAD_LITE;
  *max_r = TNB_MAX_READ_BURST_LITE;
  *flags = TNB_XFER_FLAGS_LITE;
}

static void nb_caps_from_profile(uint8_t profile, tars_node_t *out)
{
  if (profile == TNB_PROFILE_LITE)
  {
    out->max_write_payload = TNB_MAX_WRITE_PAYLOAD_LITE;
    out->max_read_burst = TNB_MAX_READ_BURST_LITE;
    out->xfer_flags = TNB_XFER_FLAGS_LITE;
  }
  else
  {
    out->max_write_payload = TNB_MAX_WRITE_PAYLOAD_FULL;
    out->max_read_burst = TNB_MAX_READ_BURST_FULL;
    out->xfer_flags = TNB_XFER_FLAGS_FULL;
  }
}

/* ---- 底层寄存器读写 ---- */
tars_status_t TarsNodeBus_ReadReg(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
  uint16_t dev = (uint16_t)(addr << 1);
  uint8_t max_w;
  uint8_t max_r;
  uint8_t flags;
  uint16_t gap_ms;

  if ((s_hi2c == NULL) || (buf == NULL) || (len == 0U))
  {
    return TARS_ERR_PARAM;
  }
  nb_caps_lookup(addr, &max_w, &max_r, &flags);
  if (max_r == 0U)
  {
    max_r = TNB_MAX_READ_BURST_LITE;
  }
  gap_ms = ((flags & TNB_XFER_STOP_FLUSH_WRITE) != 0U) ? 5U : 1U;
  nb_bus_ensure();

  /*
   * Unified wire: pointer write + STOP + gap + read (NO_SR path).
   * Long reads are split to max_read_burst.
   */
  while (len > 0U)
  {
    uint16_t chunk = len;
    if (chunk > (uint16_t)max_r)
    {
      chunk = max_r;
    }
    if (HAL_I2C_Master_Transmit(s_hi2c, dev, &reg, 1U, NB_TIMEOUT_MS) != HAL_OK)
    {
      return nb_after_hal_fail();
    }
    HAL_Delay(gap_ms);
    if (HAL_I2C_Master_Receive(s_hi2c, dev, buf, chunk, NB_TIMEOUT_MS) != HAL_OK)
    {
      return nb_after_hal_fail();
    }
    buf += chunk;
    reg = (uint8_t)(reg + (uint8_t)chunk);
    len = (uint16_t)(len - chunk);
  }
  return TARS_OK;
}

tars_status_t TarsNodeBus_WriteReg(uint8_t addr, uint8_t reg, const uint8_t *buf, uint16_t len)
{
  uint8_t frame[17];
  uint16_t i;
  uint16_t dev = (uint16_t)(addr << 1);
  uint8_t max_w;
  uint8_t max_r;
  uint8_t flags;
  uint16_t gap_ms;

  if ((s_hi2c == NULL) || ((len > 0U) && (buf == NULL)))
  {
    return TARS_ERR_PARAM;
  }
  nb_caps_lookup(addr, &max_w, &max_r, &flags);
  if (len > (uint16_t)max_w)
  {
    return TARS_ERR_PARAM;
  }
  if ((1U + len) > sizeof(frame))
  {
    return TARS_ERR_PARAM;
  }
  gap_ms = ((flags & TNB_XFER_STOP_FLUSH_WRITE) != 0U) ? 2U : 1U;
  nb_bus_ensure();
  /* One START/STOP write: [reg || payload]. Slave buffers bytes until STOP. */
  frame[0] = reg;
  for (i = 0U; i < len; i++)
  {
    frame[1U + i] = buf[i];
  }
  if (HAL_I2C_Master_Transmit(s_hi2c, dev, frame, (uint16_t)(1U + len), NB_TIMEOUT_MS) != HAL_OK)
  {
    return nb_after_hal_fail();
  }
  HAL_Delay(gap_ms);
  return TARS_OK;
}

/* ---- 地址池 ---- */
static uint8_t addr_in_use(uint8_t addr)
{
  uint8_t i;
  for (i = 0U; i < s_count; i++)
  {
    if (s_nodes[i].present && (s_nodes[i].addr == addr))
    {
      return 1U;
    }
  }
  return 0U;
}

static uint8_t alloc_addr(uint8_t preferred)
{
  uint8_t a;

  /* FULL ARP 仅分配 FULL 池 0x40..0x5F */
  if (TNB_ADDR_IS_FULL_POOL(preferred) && (addr_in_use(preferred) == 0U))
  {
    return preferred;
  }
  for (a = TNB_ADDR_FULL_BASE; a <= TNB_ADDR_FULL_MAX; a++)
  {
    if (addr_in_use(a) == 0U)
    {
      return a;
    }
  }
  return 0U; /* 无可用地址 */
}

/* ---- 枚举（LITE：仅静态扫描；ARP 会打乱 soft-I2C） ---- */
static void enumerate_try_addr(uint8_t a, uint8_t expect_profile)
{
  tars_node_t probe;

  if (s_count >= TARS_NODEBUS_MAX_NODES)
  {
    return;
  }
  if (HAL_I2C_IsDeviceReady(s_hi2c, (uint16_t)(a << 1), 1U, 20U) != HAL_OK)
  {
    nb_clear_af();
    return;
  }
  /* Let soft-I2C leave any partial txn from the ready probe before identity. */
  HAL_Delay(5);
  memset(&probe, 0, sizeof(probe));
  if (TarsNodeBus_ReadIdentity(a, &probe) != TARS_OK)
  {
    return;
  }
  if (probe.vendor_id != TNB_VENDOR_TARS)
  {
    return;
  }
  /* 只认新池：LITE 段仅 PROFILE_LITE，FULL 段仅 PROFILE_FULL（拒旧 FULL 占 LITE 址） */
  if (probe.profile != expect_profile)
  {
    return;
  }
  s_nodes[s_count] = probe;
  s_nodes[s_count].conflict = 0U;
  s_count++;
}

int TarsNodeBus_Enumerate(void)
{
  uint8_t a;

  if (s_hi2c == NULL)
  {
    return -1;
  }

  memset(s_nodes, 0, sizeof(s_nodes));
  s_count = 0U;
  nb_bus_ensure();

  /*
   * Skip General-Call / ARP for now: LITE ignores them, and the NACK +
   * recover sequence desyncs ATtiny soft-I2C before identity reads succeed.
   *
   * Prefer IsDeviceReady (same as probe) before touching ReadReg — empty-addr
   * Master_Transmit storms were still upsetting the soft slave.
   *
   * LITE 烧录区 0x10..0x3F → PROFILE_LITE only；
   * FULL 池 0x40..0x5F → PROFILE_FULL only（不兼容旧 FULL 占 0x10..0x2F）。
   */
  for (a = TNB_ADDR_LITE_BURN_BASE; a <= TNB_ADDR_LITE_MAX; a++)
  {
    enumerate_try_addr(a, TNB_PROFILE_LITE);
  }
  for (a = TNB_ADDR_FULL_BASE; a <= TNB_ADDR_FULL_MAX; a++)
  {
    enumerate_try_addr(a, TNB_PROFILE_FULL);
  }

  return (int)s_count;
}

uint8_t TarsNodeBus_Count(void) { return s_count; }

const tars_node_t *TarsNodeBus_Get(uint8_t index)
{
  if (index >= s_count)
  {
    return NULL;
  }
  return &s_nodes[index];
}

const tars_node_t *TarsNodeBus_Find(uint8_t addr)
{
  uint8_t i;
  for (i = 0U; i < s_count; i++)
  {
    if (s_nodes[i].present && (s_nodes[i].addr == addr))
    {
      return &s_nodes[i];
    }
  }
  return NULL;
}

/* ---- 身份 / 能力 / 健康 ---- */
tars_status_t TarsNodeBus_ReadIdentity(uint8_t addr, tars_node_t *out)
{
  uint8_t buf[24];
  tars_status_t st;
  uint8_t i;

  if (out == NULL)
  {
    return TARS_ERR_PARAM;
  }
  /* Soft-I2C LITE: one-byte frames (multi-byte from 0x00 was unreliable). */
  for (i = 0U; i < 24U; i++)
  {
    st = TarsNodeBus_ReadReg(addr, (uint8_t)(TNB_REG_PROTO_VER + i), &buf[i], 1U);
    if (st != TARS_OK)
    {
      return st;
    }
  }
  out->addr = addr;
  out->present = 1U;
  out->proto_ver = buf[0];
  out->vendor_id = (uint16_t)(buf[2] | (buf[3] << 8));
  out->product_id = (uint16_t)(buf[4] | (buf[5] << 8));
  out->fw_ver = (uint16_t)(buf[6] | (buf[7] << 8));
  out->board_id = buf[8];
  out->cap_count = buf[10];
  out->profile = buf[11];
  memcpy(out->uid, &buf[12], 12);
  /* Xfer caps come only from PROFILE constants (LITE never has 0x2D–0x2F). */
  nb_caps_from_profile(out->profile, out);
  return TARS_OK;
}

tars_status_t TarsNodeBus_ReadCap(uint8_t addr, uint8_t index, tars_node_cap_t *out)
{
  uint8_t buf[TNB_CAP_DESC_HDR_LEN + TNB_CAP_NAME_MAX];
  uint8_t hdr = TNB_CAP_DESC_HDR_LEN;
  uint8_t name_len;
  tars_status_t st;

  if (out == NULL)
  {
    return TARS_ERR_PARAM;
  }
  /* 选择索引 */
  st = TarsNodeBus_WriteReg(addr, TNB_REG_CAP_INDEX, &index, 1U);
  if (st != TARS_OK)
  {
    return st;
  }
  /* 读描述符块 */
  st = TarsNodeBus_ReadReg(addr, TNB_REG_CAP_DESC, buf, sizeof(buf));
  if (st != TARS_OK)
  {
    return st;
  }
  memcpy(&out->desc, buf, hdr);
  name_len = out->desc.name_len;
  if (name_len > TNB_CAP_NAME_MAX)
  {
    name_len = TNB_CAP_NAME_MAX;
  }
  memcpy(out->name, &buf[hdr], name_len);
  out->name[name_len] = '\0';
  return TARS_OK;
}

tars_status_t TarsNodeBus_ReadHealth(uint8_t addr, tars_node_health_t *out)
{
  uint8_t st_byte;
  uint8_t h[12];
  tars_status_t st;

  if (out == NULL)
  {
    return TARS_ERR_PARAM;
  }
  st = TarsNodeBus_ReadReg(addr, TNB_REG_STATUS, &st_byte, 1U);
  if (st != TARS_OK)
  {
    return st;
  }
  out->status = st_byte;
  /* 0x20..0x2B: fault(2),uptime(4),temp(2),vdda(2),heartbeat(1),lasterr(1) */
  st = TarsNodeBus_ReadReg(addr, TNB_REG_FAULT_FLAGS, h, 12U);
  if (st != TARS_OK)
  {
    return st;
  }
  out->fault_flags = (uint16_t)(h[0] | (h[1] << 8));
  out->uptime_s = (uint32_t)h[2] | ((uint32_t)h[3] << 8) |
                  ((uint32_t)h[4] << 16) | ((uint32_t)h[5] << 24);
  out->temp_c10 = (int16_t)(h[6] | (h[7] << 8));
  out->vdda_mv = (uint16_t)(h[8] | (h[9] << 8));
  out->heartbeat = h[10];
  out->last_err = h[11];
  return TARS_OK;
}

/* ---- 控制 ---- */
tars_status_t TarsNodeBus_MuxSelect(uint8_t addr, uint8_t channel)
{
  return TarsNodeBus_WriteReg(addr, TNB_REG_MUX_CH, &channel, 1U);
}

tars_status_t TarsNodeBus_MuxEnable(uint8_t addr, uint8_t enable)
{
  uint8_t v = (enable != 0U) ? 1U : 0U;
  return TarsNodeBus_WriteReg(addr, TNB_REG_MUX_EN, &v, 1U);
}

tars_status_t TarsNodeBus_AdcReadAll(uint8_t addr, uint16_t out[4])
{
  uint8_t b[8];
  tars_status_t st;
  uint8_t i;

  if (out == NULL)
  {
    return TARS_ERR_PARAM;
  }
  st = TarsNodeBus_ReadReg(addr, TNB_REG_ADC_ALL, b, 8U);
  if (st != TARS_OK)
  {
    return st;
  }
  for (i = 0U; i < 4U; i++)
  {
    out[i] = (uint16_t)(b[i * 2U] | (b[(i * 2U) + 1U] << 8));
  }
  return TARS_OK;
}

tars_status_t TarsNodeBus_PwmSet(uint8_t addr, uint8_t ch, uint8_t enable,
                                 uint32_t freq_hz, uint16_t duty_pct100)
{
  uint8_t buf[6];
  tars_status_t st;

  st = TarsNodeBus_WriteReg(addr, TNB_REG_PWM_CH, &ch, 1U);
  if (st != TARS_OK)
  {
    return st;
  }
  buf[0] = (uint8_t)(freq_hz & 0xFFU);
  buf[1] = (uint8_t)((freq_hz >> 8) & 0xFFU);
  buf[2] = (uint8_t)((freq_hz >> 16) & 0xFFU);
  buf[3] = (uint8_t)((freq_hz >> 24) & 0xFFU);
  st = TarsNodeBus_WriteReg(addr, TNB_REG_PWM_FREQ_HZ, buf, 4U);
  if (st != TARS_OK)
  {
    return st;
  }
  buf[0] = (uint8_t)(duty_pct100 & 0xFFU);
  buf[1] = (uint8_t)(duty_pct100 >> 8);
  st = TarsNodeBus_WriteReg(addr, TNB_REG_PWM_DUTY, buf, 2U);
  if (st != TARS_OK)
  {
    return st;
  }
  {
    uint8_t en = (enable != 0U) ? 1U : 0U;
    return TarsNodeBus_WriteReg(addr, TNB_REG_PWM_EN, &en, 1U);
  }
}

tars_status_t TarsNodeBus_AlertClear(uint8_t addr)
{
  uint8_t v = 1U;
  return TarsNodeBus_WriteReg(addr, TNB_REG_ALERT_CLEAR, &v, 1U);
}

/* ---- Shell ---- */
static uint8_t parse_hex_or_dec(const char *s)
{
  return (uint8_t)strtoul(s, NULL, 0);
}

void TarsNodeBus_ShellCmd(const char *args, char *out, uint32_t out_size)
{
  char cmd[16];
  int n;

  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }
  while ((*args == ' ') || (*args == '\t')) { args++; }

  if (sscanf(args, "%15s%n", cmd, &n) < 1)
  {
    (void)snprintf(out, out_size,
                   "usage: nodebus scan|list|health <a>|id <a>|caps <a>|mux <a> <ch>|probe|bus\r\n");
    return;
  }
  args += n;

  if (strcmp(cmd, "scan") == 0)
  {
    int c = TarsNodeBus_Enumerate();
    (void)snprintf(out, out_size, "nodebus: found %d node(s)\r\n", c);
  }
  else if (strcmp(cmd, "list") == 0)
  {
    uint32_t used = 0U;
    uint8_t i;
    used += (uint32_t)snprintf(out, out_size, "nodes=%u\r\n", s_count);
    for (i = 0U; (i < s_count) && (used < out_size); i++)
    {
      const tars_node_t *nd = &s_nodes[i];
      used += (uint32_t)snprintf(out + used, out_size - used,
                                 "  addr=0x%02X board=%u prof=%u%s vid=%04X pid=%04X fw=%u.%u caps=%u\r\n",
                                 nd->addr, nd->board_id, nd->profile,
                                 (nd->conflict != 0U) ? " CONFLICT" : "",
                                 nd->vendor_id, nd->product_id,
                                 (nd->fw_ver >> 8), (nd->fw_ver & 0xFFU), nd->cap_count);
    }
  }
  else if (strcmp(cmd, "health") == 0)
  {
    tars_node_health_t hp;
    uint8_t a = parse_hex_or_dec(args);
    if (TarsNodeBus_ReadHealth(a, &hp) == TARS_OK)
    {
      (void)snprintf(out, out_size,
                     "0x%02X: st=0x%02X flt=0x%04X up=%lus temp=%d.%dC vdda=%umV hb=%u err=%u\r\n",
                     a, hp.status, hp.fault_flags, (unsigned long)hp.uptime_s,
                     hp.temp_c10 / 10, (hp.temp_c10 % 10 + 10) % 10, hp.vdda_mv,
                     hp.heartbeat, hp.last_err);
    }
    else
    {
      (void)snprintf(out, out_size, "0x%02X: read failed\r\n", a);
    }
  }
  else if (strcmp(cmd, "rd") == 0)
  {
    char addr_tok[16];
    unsigned reg = 0U;
    unsigned len = 1U;
    int n2 = 0;
    uint8_t buf[16];
    uint8_t a;
    uint8_t i;
    tars_status_t st;
    uint32_t used = 0U;

    if (sscanf(args, "%15s%n", addr_tok, &n2) != 1)
    {
      (void)snprintf(out, out_size, "usage: nodebus rd <addr> <reg> [len]\r\n");
      return;
    }
    {
      char reg_tok[16];
      char len_tok[16];
      int n3 = 0;
      if (sscanf(args + n2, "%15s%n", reg_tok, &n3) != 1)
      {
        (void)snprintf(out, out_size, "usage: nodebus rd <addr> <reg> [len]\r\n");
        return;
      }
      n2 += n3;
      reg = (unsigned)parse_hex_or_dec(reg_tok);
      if (sscanf(args + n2, "%15s", len_tok) == 1)
      {
        len = (unsigned)strtoul(len_tok, NULL, 0);
      }
      else
      {
        len = 1U;
      }
    }
    if ((len == 0U) || (len > 16U) || (reg > 255U))
    {
      (void)snprintf(out, out_size, "rd: bad reg/len\r\n");
      return;
    }
    a = parse_hex_or_dec(addr_tok);
    st = TarsNodeBus_ReadReg(a, (uint8_t)reg, buf, (uint16_t)len);
    if (st != TARS_OK)
    {
      (void)snprintf(out, out_size, "rd 0x%02X@0x%02X len=%u fail st=%d err=0x%08lX\r\n",
                     a, (unsigned)reg, len, (int)st, (unsigned long)s_hi2c->ErrorCode);
      return;
    }
    used = (uint32_t)snprintf(out, out_size, "rd 0x%02X@0x%02X:", a, (unsigned)reg);
    for (i = 0U; (i < (uint8_t)len) && (used + 4U < out_size); i++)
    {
      used += (uint32_t)snprintf(out + used, out_size - used, " %02X", buf[i]);
    }
    (void)snprintf(out + used, out_size - used, "\r\n");
  }
  else if (strcmp(cmd, "id") == 0)
  {
    tars_node_t nd;
    uint8_t a = parse_hex_or_dec(args);
    uint8_t buf[24];
    uint8_t i;
    uint8_t fail_at = 0xFFU;
    tars_status_t st = TARS_OK;

    for (i = 0U; i < 24U; i++)
    {
      st = TarsNodeBus_ReadReg(a, (uint8_t)(TNB_REG_PROTO_VER + i), &buf[i], 1U);
      if (st != TARS_OK)
      {
        fail_at = i;
        break;
      }
    }
    if (st == TARS_OK)
    {
      nd.vendor_id = (uint16_t)(buf[2] | (buf[3] << 8));
      nd.product_id = (uint16_t)(buf[4] | (buf[5] << 8));
      nd.fw_ver = (uint16_t)(buf[6] | (buf[7] << 8));
      (void)snprintf(out, out_size,
                     "0x%02X: id ok vid=0x%04X pid=0x%04X board=%u prof=%u caps=%u "
                     "fw=%u.%u proto=%u\r\n",
                     a, nd.vendor_id, nd.product_id, buf[8], buf[11], buf[10],
                     (nd.fw_ver >> 8), (nd.fw_ver & 0xFFU), buf[0]);
    }
    else
    {
      (void)snprintf(out, out_size, "0x%02X: id fail at +%u (reg 0x%02X) st=%d err=0x%08lX\r\n",
                     a, (unsigned)fail_at, (unsigned)fail_at, (int)st,
                     (unsigned long)s_hi2c->ErrorCode);
    }
  }
  else if (strcmp(cmd, "caps") == 0)
  {
    uint8_t a = parse_hex_or_dec(args);
    const tars_node_t *nd = TarsNodeBus_Find(a);
    uint8_t cc = (nd != NULL) ? nd->cap_count : 0U;
    uint32_t used = 0U;
    uint8_t i;
    used += (uint32_t)snprintf(out, out_size, "0x%02X caps=%u\r\n", a, cc);
    for (i = 0U; (i < cc) && (used < out_size); i++)
    {
      tars_node_cap_t cap;
      if (TarsNodeBus_ReadCap(a, i, &cap) == TARS_OK)
      {
        used += (uint32_t)snprintf(out + used, out_size - used,
                                   "  [%u] type=%u %s acc=%u cnt=%u reg=0x%02X range=%u..%u unit=%u\r\n",
                                   i, cap.desc.type, cap.name, cap.desc.access, cap.desc.count,
                                   cap.desc.reg_base, cap.desc.range_min, cap.desc.range_max,
                                   cap.desc.unit);
      }
    }
  }
  else if (strcmp(cmd, "mux") == 0)
  {
    char addr_tok[16];
    unsigned ch = 0U;
    int n2 = 0;
    /* newlib-nano often lacks %hhi; parse addr like health/caps (0xNN or decimal). */
    if ((sscanf(args, "%15s%n", addr_tok, &n2) == 1) &&
        (sscanf(args + n2, "%u", &ch) == 1))
    {
      uint8_t a = parse_hex_or_dec(addr_tok);
      uint8_t got = 0xFFU;
      tars_status_t st1 = TarsNodeBus_MuxSelect(a, (uint8_t)ch);
      tars_status_t st2 = TARS_ERR_STATE;
      /* LITE/ATtiny: EN is hardwired; do not follow with MuxEnable write. */
      if (st1 == TARS_OK)
      {
        st2 = TarsNodeBus_ReadReg(a, TNB_REG_MUX_CH, &got, 1U);
      }
      (void)snprintf(out, out_size, "mux 0x%02X ch=%u sel=%d get=%u getst=%d\r\n",
                     a, ch, (int)st1, (unsigned)got, (int)st2);
    }
    else
    {
      (void)snprintf(out, out_size, "usage: nodebus mux <addr> <ch>\r\n");
    }
  }
  else if (strcmp(cmd, "muxen") == 0)
  {
    char addr_tok[16];
    unsigned en = 0U;
    int n2 = 0;
    if ((sscanf(args, "%15s%n", addr_tok, &n2) == 1) &&
        (sscanf(args + n2, "%u", &en) == 1))
    {
      uint8_t a = parse_hex_or_dec(addr_tok);
      uint8_t got = 0xFFU;
      tars_status_t st1 = TarsNodeBus_MuxEnable(a, (uint8_t)((en != 0U) ? 1U : 0U));
      tars_status_t st2 = TARS_ERR_STATE;
      if (st1 == TARS_OK)
      {
        st2 = TarsNodeBus_ReadReg(a, TNB_REG_MUX_EN, &got, 1U);
      }
      (void)snprintf(out, out_size, "muxen 0x%02X en=%u set=%d get=%u getst=%d\r\n",
                     a, (en != 0U) ? 1U : 0U, (int)st1, (unsigned)got, (int)st2);
    }
    else
    {
      (void)snprintf(out, out_size, "usage: nodebus muxen <addr> <0|1>\r\n");
    }
  }
  else if (strcmp(cmd, "bus") == 0)
  {
    char mode[16];
    GPIO_InitTypeDef gpio = {0};
    uint32_t idr;

    if (sscanf(args, "%15s", mode) != 1)
    {
      (void)snprintf(out, out_size,
                     "usage: nodebus bus idle|status|sda0|sda1|scl0|scl1\r\n");
      return;
    }
    if (strcmp(mode, "status") == 0)
    {
      /* Do not re-init — report live pin + I2C2 state (stuck-low diagnosis). */
      idr = GPIOB->IDR;
      (void)snprintf(out, out_size,
                     "bus status: SCL=%u SDA=%u MODER=%08lX OTYPER=%08lX ODR=%08lX "
                     "I2C_SR1=%04lX SR2=%04lX CR1=%04lX busy=%u err=0x%08lX\r\n",
                     (unsigned)((idr >> 10) & 1U),
                     (unsigned)((idr >> 11) & 1U),
                     (unsigned long)GPIOB->MODER,
                     (unsigned long)GPIOB->OTYPER,
                     (unsigned long)GPIOB->ODR,
                     (unsigned long)(hi2c2.Instance ? hi2c2.Instance->SR1 : 0U),
                     (unsigned long)(hi2c2.Instance ? hi2c2.Instance->SR2 : 0U),
                     (unsigned long)(hi2c2.Instance ? hi2c2.Instance->CR1 : 0U),
                     (unsigned)((hi2c2.Instance != NULL) &&
                                (__HAL_I2C_GET_FLAG(&hi2c2, I2C_FLAG_BUSY) != RESET)),
                     (unsigned long)hi2c2.ErrorCode);
      return;
    }
    if (strcmp(mode, "idle") == 0)
    {
      nb_bus_recover();
      TarsNodeBus_Init(&hi2c2);
      idr = GPIOB->IDR;
      (void)snprintf(out, out_size,
                     "bus idle AF: SCL=%u SDA=%u (need ext pull-ups for idle HIGH)\r\n",
                     (unsigned)((idr >> 10) & 1U), (unsigned)((idr >> 11) & 1U));
      return;
    }

    /* Bit-bang hold: must kill I2C2 HW first (MspDeInit is I2C3-only). */
    MX_I2C2_BusRelease();
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL; /* external 5 V pullups only (TNB §0) */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10 | GPIO_PIN_11, GPIO_PIN_SET);
    if (strcmp(mode, "sda0") == 0)
    {
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);
    }
    else if (strcmp(mode, "sda1") == 0)
    {
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET);
    }
    else if (strcmp(mode, "scl0") == 0)
    {
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
    }
    else if (strcmp(mode, "scl1") == 0)
    {
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
    }
    else
    {
      (void)snprintf(out, out_size,
                     "usage: nodebus bus idle|status|sda0|sda1|scl0|scl1\r\n");
      return;
    }
    idr = GPIOB->IDR;
    (void)snprintf(out, out_size, "bus %s OD: SCL=%u SDA=%u (hold until bus idle)\r\n",
                   mode, (unsigned)((idr >> 10) & 1U), (unsigned)((idr >> 11) & 1U));
  }
  else if (strcmp(cmd, "probe") == 0)
  {
    uint32_t used = 0U;
    uint8_t a;
    uint16_t ack = 0U;
    uint32_t idr;
    HAL_StatusTypeDef st;

    if (s_hi2c == NULL)
    {
      (void)snprintf(out, out_size, "probe: i2c null\r\n");
      return;
    }

    nb_bus_ensure();

    idr = GPIOB->IDR;
    used += (uint32_t)snprintf(out, out_size,
                               "probe: PB10(SCL)=%u PB11(SDA)=%u busy=%u err=0x%08lX\r\n",
                               (unsigned)((idr >> 10) & 1U),
                               (unsigned)((idr >> 11) & 1U),
                               (unsigned)(__HAL_I2C_GET_FLAG(s_hi2c, I2C_FLAG_BUSY) != RESET),
                               (unsigned long)s_hi2c->ErrorCode);
    for (a = 0x08U; (a < 0x78U) && (used + 8U < out_size); a++)
    {
      if (nb_bus_needs_recover() != 0U)
      {
        nb_bus_recover();
      }
      st = HAL_I2C_IsDeviceReady(s_hi2c, (uint16_t)(a << 1), 1U, 20U);
      if (st == HAL_OK)
      {
        used += (uint32_t)snprintf(out + used, out_size - used, "  ACK 0x%02X\r\n", a);
        ack++;
        /* SDA stuck low → every addr ACKs; stop and recover. */
        if (ack >= 3U)
        {
          nb_bus_recover();
          used += (uint32_t)snprintf(out + used, out_size - used,
                                     "  (abort: likely SDA stuck low)\r\n");
          break;
        }
      }
      else if (st != HAL_ERROR)
      {
        /* Timeout/busy: clear before next address. */
        nb_bus_recover();
      }
    }
    if (ack == 0U)
    {
      used += (uint32_t)snprintf(out + used, out_size - used, "  (no ACK)\r\n");
    }
    (void)used;
  }
  else
  {
    (void)snprintf(out, out_size, "nodebus: unknown '%s'\r\n", cmd);
  }
}
