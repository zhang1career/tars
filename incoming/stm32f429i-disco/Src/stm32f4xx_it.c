#include "main.h"
#include "incoming_report.h"

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
  incoming_report_t *r = (incoming_report_t *)INCOMING_REPORT_ADDR;
  r->fatal = 2U;
  r->done = 1U;
  for (;;) {
  }
}

void MemManage_Handler(void)
{
  HardFault_Handler();
}

void BusFault_Handler(void)
{
  HardFault_Handler();
}

void UsageFault_Handler(void)
{
  HardFault_Handler();
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
