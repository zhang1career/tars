#ifndef INCOMING_REPORT_H
#define INCOMING_REPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INCOMING_REPORT_MAGIC    0x54494E43u /* 'TINC' */
#define INCOMING_REPORT_VERSION  1u
#define INCOMING_REPORT_ADDR     0x2002F000u

/* Bit numbers in pass / fail / warn / skip masks. */
enum incoming_check {
  INCOMING_MCU_ID      = 0,
  INCOMING_FLASH_SIZE  = 1,
  INCOMING_SRAM        = 2,
  INCOMING_CCM         = 3,
  INCOMING_VDD         = 4,
  INCOMING_SDRAM       = 5,
  INCOMING_GYRO        = 6,
  INCOMING_TOUCH       = 7,
  INCOMING_LCD         = 8,
  INCOMING_CHECK_COUNT = 9
};

enum incoming_clock_src {
  INCOMING_CLK_HSI      = 0,
  INCOMING_CLK_HSE_XTAL = 1,
  INCOMING_CLK_HSE_BYP  = 2
};

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t done;       /* 1 after the suite finishes */
  uint32_t pass_mask;
  uint32_t fail_mask;
  uint32_t warn_mask;
  uint32_t skip_mask;
  uint32_t clock_hz;
  uint32_t clock_src;
  uint32_t idcode;
  uint32_t flash_kb;
  uint32_t uid[3];
  uint32_t vdd_mv;
  uint32_t vrefint_raw;
  uint32_t vrefint_cal;
  uint32_t sdram_fail_addr;
  uint32_t sdram_expect;
  uint32_t sdram_got;
  uint32_t sdram_bytes;
  uint32_t gyro_whoami;
  uint32_t touch_addr7;
  uint32_t touch_id;
  uint32_t lcd_id;
  uint32_t i2c_found[4];
  uint32_t button;
  uint32_t fatal;      /* Error_Handler / HardFault sticky */
  char     summary[32];
} incoming_report_t;

#ifdef __GNUC__
_Static_assert(sizeof(incoming_report_t) <= 256, "report must stay in 0x2002F000 hole");
#endif

#ifdef __cplusplus
}
#endif

#endif /* INCOMING_REPORT_H */
