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

#define NB_TIMEOUT_MS   50U

static I2C_HandleTypeDef *s_hi2c;
static tars_node_t        s_nodes[TARS_NODEBUS_MAX_NODES];
static uint8_t            s_count;

void TarsNodeBus_Init(I2C_HandleTypeDef *hi2c)
{
  s_hi2c = hi2c;
  memset(s_nodes, 0, sizeof(s_nodes));
  s_count = 0U;
}

/* ---- 底层寄存器读写 ---- */
tars_status_t TarsNodeBus_ReadReg(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
  if ((s_hi2c == NULL) || (buf == NULL))
  {
    return TARS_ERR_PARAM;
  }
  if (HAL_I2C_Mem_Read(s_hi2c, (uint16_t)(addr << 1), reg,
                       I2C_MEMADD_SIZE_8BIT, buf, len, NB_TIMEOUT_MS) != HAL_OK)
  {
    return TARS_ERR_STATE;
  }
  return TARS_OK;
}

tars_status_t TarsNodeBus_WriteReg(uint8_t addr, uint8_t reg, const uint8_t *buf, uint16_t len)
{
  if (s_hi2c == NULL)
  {
    return TARS_ERR_PARAM;
  }
  if (HAL_I2C_Mem_Write(s_hi2c, (uint16_t)(addr << 1), reg,
                        I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, len, NB_TIMEOUT_MS) != HAL_OK)
  {
    return TARS_ERR_STATE;
  }
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

  if ((preferred >= TNB_ADDR_RUNTIME_BASE) && (preferred <= TNB_ADDR_RUNTIME_MAX) &&
      (addr_in_use(preferred) == 0U))
  {
    return preferred;
  }
  for (a = TNB_ADDR_RUNTIME_BASE; a <= TNB_ADDR_RUNTIME_MAX; a++)
  {
    if (addr_in_use(a) == 0U)
    {
      return a;
    }
  }
  return 0U; /* 无可用地址 */
}

/* ---- ARP 枚举 ---- */
int TarsNodeBus_Enumerate(void)
{
  uint8_t gc = TNB_GC_ARP_PREPARE;
  uint8_t rec[TNB_UDID_LEN + 2];
  uint8_t asg[TNB_UDID_LEN + 2];
  uint8_t guard;

  if (s_hi2c == NULL)
  {
    return -1;
  }

  memset(s_nodes, 0, sizeof(s_nodes));
  s_count = 0U;

  /* 1. General Call 让所有节点回到未解析 */
  (void)HAL_I2C_Master_Transmit(s_hi2c, (uint16_t)(TNB_ADDR_GENERAL_CALL << 1),
                                &gc, 1U, NB_TIMEOUT_MS);
  HAL_Delay(2);

  /* 2. 逐个仲裁读 UDID 并分配地址 */
  for (guard = 0U; guard < TARS_NODEBUS_MAX_NODES; guard++)
  {
    uint8_t pref;
    uint8_t assigned;

    if (HAL_I2C_Master_Receive(s_hi2c, (uint16_t)(TNB_ADDR_ARP << 1),
                               rec, sizeof(rec), NB_TIMEOUT_MS) != HAL_OK)
    {
      break; /* NACK：无更多未解析节点 */
    }

    /* 校验 PEC */
    if (TnbCrc_Buf(0U, rec, TNB_UDID_LEN + 1U) != rec[TNB_UDID_LEN + 1U])
    {
      continue; /* 数据损坏，跳过本轮 */
    }

    pref = rec[TNB_UDID_LEN];
    assigned = alloc_addr(pref);
    if (assigned == 0U)
    {
      break;
    }

    memcpy(asg, rec, TNB_UDID_LEN);
    asg[TNB_UDID_LEN] = assigned;
    asg[TNB_UDID_LEN + 1U] = TnbCrc_Buf(0U, asg, TNB_UDID_LEN + 1U);
    if (HAL_I2C_Master_Transmit(s_hi2c, (uint16_t)(TNB_ADDR_ARP << 1),
                                asg, sizeof(asg), NB_TIMEOUT_MS) != HAL_OK)
    {
      continue;
    }

    /* 记录节点（身份细节稍后精读） */
    s_nodes[s_count].addr = assigned;
    s_nodes[s_count].board_id = rec[8];
    memcpy(s_nodes[s_count].uid, &rec[10], 6); /* UDID 尾 6 字节 */
    s_nodes[s_count].present = 1U;
    s_count++;
  }

  /* 3. ARP 节点精读身份 */
  {
    uint8_t i;
    for (i = 0U; i < s_count; i++)
    {
      (void)TarsNodeBus_ReadIdentity(s_nodes[i].addr, &s_nodes[i]);
    }
  }

  /* 4. 静态扫描：纳入 lite / 未参与 ARP 的节点；冲突不覆盖 */
  {
    uint8_t a;
    for (a = TNB_ADDR_RUNTIME_BASE; a <= TNB_ADDR_RUNTIME_MAX; a++)
    {
      tars_node_t probe;
      const tars_node_t *exist;

      if (HAL_I2C_IsDeviceReady(s_hi2c, (uint16_t)(a << 1), 2U, NB_TIMEOUT_MS) != HAL_OK)
      {
        continue;
      }
      memset(&probe, 0, sizeof(probe));
      if (TarsNodeBus_ReadIdentity(a, &probe) != TARS_OK)
      {
        continue;
      }
      if (probe.vendor_id != TNB_VENDOR_TARS)
      {
        continue;
      }

      exist = TarsNodeBus_Find(a);
      if (exist != NULL)
      {
        /* ARP 优先：仅在身份明显不一致时打 conflict */
        if ((exist->board_id != probe.board_id) ||
            (exist->product_id != probe.product_id) ||
            (memcmp(exist->uid, probe.uid, 12) != 0))
        {
          uint8_t i;
          for (i = 0U; i < s_count; i++)
          {
            if (s_nodes[i].addr == a)
            {
              s_nodes[i].conflict = 1U;
              break;
            }
          }
        }
        continue;
      }

      if (s_count >= TARS_NODEBUS_MAX_NODES)
      {
        break;
      }
      s_nodes[s_count] = probe;
      s_nodes[s_count].conflict = 0U;
      s_count++;
    }
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

  if (out == NULL)
  {
    return TARS_ERR_PARAM;
  }
  /* 0x00..0x17：proto..cap_count,profile,uid[12] */
  st = TarsNodeBus_ReadReg(addr, TNB_REG_PROTO_VER, buf, 24U);
  if (st != TARS_OK)
  {
    return st;
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
                   "usage: nodebus scan|list|health <a>|caps <a>|mux <a> <ch>|probe|bus\r\n");
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
      tars_status_t st1 = TarsNodeBus_MuxSelect(a, (uint8_t)ch);
      tars_status_t st2 = TarsNodeBus_MuxEnable(a, 1U);
      (void)snprintf(out, out_size, "mux 0x%02X ch=%u sel=%d en=%d\r\n", a, ch,
                     (int)st1, (int)st2);
    }
    else
    {
      (void)snprintf(out, out_size, "usage: nodebus mux <addr> <ch>\r\n");
    }
  }
  else if (strcmp(cmd, "bus") == 0)
  {
    char mode[16];
    GPIO_InitTypeDef gpio = {0};
    uint32_t idr;

    if (sscanf(args, "%15s", mode) != 1)
    {
      (void)snprintf(out, out_size, "usage: nodebus bus idle|sda0|sda1|scl0|scl1\r\n");
      return;
    }
    if (strcmp(mode, "idle") == 0)
    {
      MX_I2C2_Init();
      TarsNodeBus_Init(&hi2c2);
      idr = GPIOB->IDR;
      (void)snprintf(out, out_size, "bus idle AF: SCL=%u SDA=%u\r\n",
                     (unsigned)((idr >> 10) & 1U), (unsigned)((idr >> 11) & 1U));
      return;
    }

    HAL_I2C_DeInit(&hi2c2);
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
    /* Default both released high; then pull the selected line. */
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
      (void)snprintf(out, out_size, "usage: nodebus bus idle|sda0|sda1|scl0|scl1\r\n");
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

    /* Bus recover if stuck: generate 9 SCL pulses as GPIO then re-init AF. */
    if (__HAL_I2C_GET_FLAG(s_hi2c, I2C_FLAG_BUSY) != RESET)
    {
      GPIO_InitTypeDef gpio = {0};
      uint8_t i;
      HAL_I2C_DeInit(s_hi2c);
      gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
      gpio.Mode = GPIO_MODE_OUTPUT_OD;
      gpio.Pull = GPIO_PULLUP;
      gpio.Speed = GPIO_SPEED_FREQ_LOW;
      HAL_GPIO_Init(GPIOB, &gpio);
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET); /* SDA high */
      for (i = 0U; i < 9U; i++)
      {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
        HAL_Delay(1);
      }
      MX_I2C2_Init();
      TarsNodeBus_Init(&hi2c2);
    }

    idr = GPIOB->IDR;
    used += (uint32_t)snprintf(out, out_size,
                               "probe: PB10(SCL)=%u PB11(SDA)=%u busy=%u err=0x%08lX\r\n",
                               (unsigned)((idr >> 10) & 1U),
                               (unsigned)((idr >> 11) & 1U),
                               (unsigned)(__HAL_I2C_GET_FLAG(s_hi2c, I2C_FLAG_BUSY) != RESET),
                               (unsigned long)s_hi2c->ErrorCode);
    for (a = 0x08U; (a < 0x78U) && (used + 8U < out_size); a++)
    {
      st = HAL_I2C_IsDeviceReady(s_hi2c, (uint16_t)(a << 1), 1U, 5U);
      if (st == HAL_OK)
      {
        used += (uint32_t)snprintf(out + used, out_size - used, "  ACK 0x%02X\r\n", a);
        ack++;
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
