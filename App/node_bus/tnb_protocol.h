#ifndef TNB_PROTOCOL_H
#define TNB_PROTOCOL_H

/*
 * TARS Node Bus (TNB) — I2C 下位机通用协议 v1
 *
 * 协议唯一真源：本仓库 shared/tnb/（master 与各 MCU 从机共用）。
 * 完整规范见 docs/tars-node-bus.md。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TNB_PROTO_VER            0x01U

#define TNB_VENDOR_TARS          0x5441U /* "TA" */
#define TNB_PRODUCT_IO_MUX       0x0001U /* v0_1: CD74HC4067 16ch (0..VCC) */
#define TNB_PRODUCT_IO_MUX_HV    0x0002U /* ADG1408 8ch bipolar（F030 或 ATtiny） */

/* ---- 节点配置档（PROFILE） ---- */
#define TNB_PROFILE_FULL         0x00U /* 支持 ARP、完整能力/健康 */
#define TNB_PROFILE_LITE         0x01U /* 静态地址、精简寄存器；不参与 ARP */

/* ---- 地址规划（7-bit） ---- */
#define TNB_ADDR_GENERAL_CALL    0x00U
#define TNB_ADDR_ARP             0x61U /* 未解析 full 节点的枚举应答地址 */
#define TNB_ADDR_ALERT_RESPONSE  0x0CU /* 可选 ARA */
#define TNB_ADDR_RUNTIME_BASE    0x10U /* 首选地址 = BASE + board_id */
#define TNB_ADDR_RUNTIME_MAX     0x2FU
#define TNB_MAX_NODES            32U

#define TNB_ADDR_FROM_BOARD_ID(id) ((uint8_t)(TNB_ADDR_RUNTIME_BASE + ((id) & 0x1FU)))

/* ---- General Call 子命令 ---- */
#define TNB_GC_ARP_PREPARE       0x01U /* full 节点 AR=0；lite 可忽略 */
#define TNB_GC_RESET             0x06U /* 软复位（沿用 SMBus 惯例值） */

/* ---- 寄存器映射 ---- */
/* 身份 / 信息（RO） */
#define TNB_REG_PROTO_VER        0x00U /* u8  */
#define TNB_REG_STATUS           0x01U /* u8  */
#define TNB_REG_VENDOR_ID        0x02U /* u16 */
#define TNB_REG_PRODUCT_ID       0x04U /* u16 */
#define TNB_REG_FW_VER           0x06U /* u16 */
#define TNB_REG_BOARD_ID         0x08U /* u8  */
#define TNB_REG_ADDR             0x09U /* u8  当前主地址 */
#define TNB_REG_CAP_COUNT        0x0AU /* u8  */
#define TNB_REG_PROFILE          0x0BU /* u8  TNB_PROFILE_* */
#define TNB_REG_UID              0x0CU /* u8[12] 96-bit UID（MCU 或 EEPROM） */

/* 健康（RO） */
#define TNB_REG_FAULT_FLAGS      0x20U /* u16 */
#define TNB_REG_UPTIME_S         0x22U /* u32 */
#define TNB_REG_TEMP_C10         0x26U /* i16, 0.1C（lite 可固定 0） */
#define TNB_REG_VDDA_MV          0x28U /* u16（lite 可固定 0） */
#define TNB_REG_HEARTBEAT        0x2AU /* u8  */
#define TNB_REG_LAST_ERR         0x2BU /* u8  */
#define TNB_REG_I2C_OK           0x2CU /* u8  上次事务成功计数/标志（可选） */

/* 控制（RW） */
#define TNB_REG_MUX_CH           0x40U /* u8  通道号 */
#define TNB_REG_MUX_EN           0x41U /* u8  0/1；硬件常使能节点读=1、写忽略 */
#define TNB_REG_ADC_SEL          0x42U /* u8  0..3 */
#define TNB_REG_ADC_VAL          0x44U /* u16 所选通道 */
#define TNB_REG_ADC_ALL          0x46U /* u16[4] */
#define TNB_REG_PWM_CH           0x50U /* u8 */
#define TNB_REG_PWM_EN           0x51U /* u8 */
#define TNB_REG_PWM_FREQ_HZ      0x52U /* u32 */
#define TNB_REG_PWM_DUTY         0x56U /* u16 0..10000 */
#define TNB_REG_ALERT_CTRL       0x5EU /* u8 bit0=enable */
#define TNB_REG_ALERT_CLEAR      0x5FU /* u8 写1清故障 */

/* 能力描述（0x60 写索引，0x61 读描述符块） */
#define TNB_REG_CAP_INDEX        0x60U /* u8 (W) */
#define TNB_REG_CAP_DESC         0x61U /* block (R) */

/* 命令邮箱 */
#define TNB_REG_CMD              0x80U /* W: op,len,payload,[PEC] */
#define TNB_REG_RESP             0x81U /* R: status,len,payload,[PEC] */

#define TNB_REG_SPACE_SIZE       0x100U

/* ---- STATUS 位 ---- */
#define TNB_ST_READY             (1U << 0)
#define TNB_ST_FAULT             (1U << 1)
#define TNB_ST_OVERTEMP          (1U << 2)
#define TNB_ST_MUX_FAULT         (1U << 3)
#define TNB_ST_CFG_DIRTY         (1U << 4)
#define TNB_ST_ALERT             (1U << 5)

/* ---- FAULT_FLAGS 位（锁存） ---- */
#define TNB_FLT_OVERTEMP         (1U << 0)
#define TNB_FLT_MUX              (1U << 1)
#define TNB_FLT_ADC              (1U << 2)
#define TNB_FLT_PWM              (1U << 3)
#define TNB_FLT_I2C              (1U << 4)

/* ---- 能力类型 ---- */
#define TNB_CAP_MUX              0x01U
#define TNB_CAP_ADC              0x02U
#define TNB_CAP_PWM              0x03U
#define TNB_CAP_ALERT            0x04U
#define TNB_CAP_GPIO             0x05U

/* ---- 能力访问位 ---- */
#define TNB_ACC_R                (1U << 0)
#define TNB_ACC_W                (1U << 1)
#define TNB_ACC_RW               (TNB_ACC_R | TNB_ACC_W)

/* ---- 单位 ---- */
#define TNB_UNIT_NONE            0x00U
#define TNB_UNIT_CHANNEL         0x01U
#define TNB_UNIT_RAW             0x02U
#define TNB_UNIT_MV              0x03U
#define TNB_UNIT_HZ              0x04U
#define TNB_UNIT_PCT100          0x05U

/* ---- 错误码 ---- */
#define TNB_OK                   0x00U
#define TNB_ERR_PARAM            0x01U
#define TNB_ERR_CRC              0x02U
#define TNB_ERR_STATE            0x03U
#define TNB_ERR_UNSUPPORTED      0x04U
#define TNB_ERR_BUSY             0x05U

/* ---- 命令 op ---- */
#define TNB_CMD_PING             0x01U

/* ---- UDID / ARP ---- */
#define TNB_UDID_LEN             16U
#define TNB_ARP_RECORD_LEN       (TNB_UDID_LEN + 1U)

#define TNB_CAP_NAME_MAX         16U

typedef struct __attribute__((packed)) {
  uint8_t  type;      /* TNB_CAP_* */
  uint8_t  id;        /* 同类型实例编号 */
  uint8_t  access;    /* TNB_ACC_* */
  uint8_t  count;     /* 通道数 */
  uint16_t reg_base;  /* 控制/读取寄存器 */
  uint16_t range_min;
  uint16_t range_max;
  uint8_t  unit;      /* TNB_UNIT_* */
  uint8_t  name_len;  /* 名称字节数（<= TNB_CAP_NAME_MAX） */
  /* char name[name_len] 紧随其后 */
} tnb_cap_desc_t;

#define TNB_CAP_DESC_HDR_LEN     ((uint8_t)sizeof(tnb_cap_desc_t))

/* 设备能力标志（UDID[1] / 身份扩展） */
#define TNB_DEVCAP_PEC           (1U << 0)
#define TNB_DEVCAP_ALERT         (1U << 1)
#define TNB_DEVCAP_ARP           (1U << 2) /* 参与 ARP；lite 无此位 */

/* ---- 无工厂 UID 的 MCU：EEPROM 布局（ATtiny13A 等） ---- */
#define TNB_EE_BOARD_ID          0U  /* u8, 0..31；0xFF → 视为 0 */
#define TNB_EE_UID               1U  /* u8[12]，烧录写入的资产 UID */
#define TNB_EE_UID_LEN           12U

#ifdef __cplusplus
}
#endif

#endif /* TNB_PROTOCOL_H */
