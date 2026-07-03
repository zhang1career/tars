#include "tars_tenant.h"
#include <ctype.h>
#include <string.h>

static int tenant_stricmp(const char *a, const char *b)
{
  unsigned char ca;
  unsigned char cb;

  if (a == NULL)
  {
    a = "";
  }
  if (b == NULL)
  {
    b = "";
  }

  for (;;)
  {
    ca = (unsigned char)tolower((unsigned char)*a++);
    cb = (unsigned char)tolower((unsigned char)*b++);

    if (ca != cb)
    {
      return (int)ca - (int)cb;
    }

    if (ca == '\0')
    {
      return 0;
    }
  }
}

int TarsTenant_IsNone(const char *tenant)
{
  if ((tenant == NULL) || (tenant[0] == '\0'))
  {
    return 1;
  }

  return (tenant_stricmp(tenant, TARS_TENANT_NONE_NAME) == 0) ? 1 : 0;
}

int TarsTenant_IsFoc(const char *tenant)
{
  if (tenant == NULL)
  {
    return 0;
  }

  return (tenant_stricmp(tenant, TARS_TENANT_FOC) == 0) ? 1 : 0;
}

int TarsTenant_IsSystem(const char *tenant)
{
  if (tenant == NULL)
  {
    return 0;
  }

  return (tenant_stricmp(tenant, TARS_TENANT_SYSTEM) == 0) ? 1 : 0;
}

int TarsTenant_IsReservedDefault(const char *tenant)
{
  return (TarsTenant_IsFoc(tenant) != 0) || (TarsTenant_IsSystem(tenant) != 0);
}

int TarsTenant_ValidateGrantName(const char *tenant)
{
  uint32_t i;
  uint32_t len;

  if (tenant == NULL)
  {
    return -1;
  }

  if (TarsTenant_IsNone(tenant) != 0)
  {
    return 0;
  }

  len = (uint32_t)strlen(tenant);
  if ((len == 0U) || (len >= TARS_TENANT_LEN))
  {
    return -1;
  }

  if (TarsTenant_IsFoc(tenant) != 0)
  {
    return -1;
  }

  if (TarsTenant_IsSystem(tenant) != 0)
  {
    return -1;
  }

  for (i = 0U; i < len; i++)
  {
    unsigned char c = (unsigned char)tenant[i];

    if (isalnum(c) || (c == '_'))
    {
      continue;
    }

    return -1;
  }

  return 0;
}

const char *TarsTenant_Display(const char *tenant)
{
  if (TarsTenant_IsNone(tenant) != 0)
  {
    return TARS_TENANT_NONE_NAME;
  }

  return tenant;
}

void TarsTenant_Copy(char *dst, uint32_t dst_size, const char *src)
{
  if ((dst == NULL) || (dst_size == 0U))
  {
    return;
  }

  if (src == NULL)
  {
    src = "";
  }

  if (TarsTenant_IsNone(src) != 0)
  {
    dst[0] = '\0';
    return;
  }

  strncpy(dst, src, dst_size - 1U);
  dst[dst_size - 1U] = '\0';
}
