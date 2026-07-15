#include "tars_mcu.h"
#include "tars_resource.h"

int TarsMcu_DacEnable(const char *channel, int enable)
{
  return TarsResource_DacEnable(channel, enable);
}

int TarsMcu_DacSetLevel(const char *channel, float level_pct)
{
  return TarsResource_DacSetLevel(channel, level_pct);
}
