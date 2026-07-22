#ifndef TARS_NODEBUS_H
#define TARS_NODEBUS_H

#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "tars_app.h"
#include "node_bus/tnb_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TARS Node Bus 主控驱动
 *
 * 通过 I2C 发现/枚举总线上的 TNB 从机节点（如 tars-io-mux），读取其身份、
 * 能力与健康，并下发控制。与具体是哪条 I2C 总线无关——在 Init 时传入句柄。
 * 协议见 tars-io-mux/docs/tars-node-bus.md，共用 node_bus/tnb_protocol.h。
 */

#define TARS_NODEBUS_MAX_NODES   TNB_MAX_NODES

typedef struct {
  uint8_t  addr;        /* 运行地址（7-bit） */
  uint8_t  board_id;
  uint8_t  cap_count;
  uint8_t  proto_ver;
  uint8_t  profile;     /* TNB_PROFILE_FULL / LITE */
  uint8_t  conflict;    /* 静态扫描与 ARP 条目身份不一致 */
  uint8_t  max_write_payload; /* from PROFILE constants at identity */
  uint8_t  max_read_burst;
  uint8_t  xfer_flags;        /* TNB_XFER_* */
  uint16_t vendor_id;
  uint16_t product_id;
  uint16_t fw_ver;
  uint8_t  uid[12];
  uint8_t  present;
} tars_node_t;

typedef struct {
  uint8_t  status;
  uint16_t fault_flags;
  uint32_t uptime_s;
  int16_t  temp_c10;
  uint16_t vdda_mv;
  uint8_t  heartbeat;
  uint8_t  last_err;
} tars_node_health_t;

typedef struct {
  tnb_cap_desc_t desc;
  char           name[TNB_CAP_NAME_MAX + 1];
} tars_node_cap_t;

/* 绑定 I2C 总线句柄（DISC1 电机版：&hi2c2，PB10=SCL / PB11=SDA） */
void          TarsNodeBus_Init(I2C_HandleTypeDef *hi2c);

/*
 * 发现并重建节点表：
 *   1) ARP 收割 PROFILE_FULL 节点（权威）
 *   2) 扫描 0x10..0x2F，纳入未占用地址上的 TARS 节点（lite/静态）
 *   3) 已占用地址若身份不一致 → 置 conflict，不覆盖 ARP 条目
 * 返回节点数（含冲突标记的条目）；<0 为错误。
 */
int           TarsNodeBus_Enumerate(void);

uint8_t       TarsNodeBus_Count(void);
const tars_node_t *TarsNodeBus_Get(uint8_t index);
const tars_node_t *TarsNodeBus_Find(uint8_t addr);

/* 读身份/能力/健康 */
tars_status_t TarsNodeBus_ReadIdentity(uint8_t addr, tars_node_t *out);
tars_status_t TarsNodeBus_ReadCap(uint8_t addr, uint8_t index, tars_node_cap_t *out);
tars_status_t TarsNodeBus_ReadHealth(uint8_t addr, tars_node_health_t *out);

/* 控制 io-mux 资源 */
tars_status_t TarsNodeBus_MuxSelect(uint8_t addr, uint8_t channel);
tars_status_t TarsNodeBus_MuxEnable(uint8_t addr, uint8_t enable);
tars_status_t TarsNodeBus_AdcReadAll(uint8_t addr, uint16_t out[4]);
tars_status_t TarsNodeBus_PwmSet(uint8_t addr, uint8_t ch, uint8_t enable,
                                 uint32_t freq_hz, uint16_t duty_pct100);
tars_status_t TarsNodeBus_AlertClear(uint8_t addr);

/* 通用寄存器读写（供扩展/调试） */
tars_status_t TarsNodeBus_ReadReg(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len);
tars_status_t TarsNodeBus_WriteReg(uint8_t addr, uint8_t reg, const uint8_t *buf, uint16_t len);

/* Shell 挂钩：把 `nodebus ...` 子命令交给本函数处理（见 .c 顶部用法注释） */
void          TarsNodeBus_ShellCmd(const char *args, char *out, uint32_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* TARS_NODEBUS_H */
