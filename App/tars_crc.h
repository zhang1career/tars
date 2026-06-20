#ifndef TARS_CRC_H
#define TARS_CRC_H

#include <stdint.h>

uint32_t TarsCrc32(const uint8_t *data, uint32_t length, uint32_t crc);

#endif /* TARS_CRC_H */
