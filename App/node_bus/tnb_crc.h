#ifndef TNB_CRC_H
#define TNB_CRC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SMBus PEC —— CRC-8，多项式 0x07，初值 0x00。
 * 增量式接口：从 crc=0 开始，逐字节喂入地址与数据。
 */
uint8_t TnbCrc_Byte(uint8_t crc, uint8_t data);
uint8_t TnbCrc_Buf(uint8_t crc, const uint8_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* TNB_CRC_H */
