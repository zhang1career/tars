#ifndef TARS_TENANT_H
#define TARS_TENANT_H

#include <stdint.h>

#define TARS_TENANT_LEN       16U

#define TARS_TENANT_FOC       "foc"
#define TARS_TENANT_SYSTEM    "system"
#define TARS_TENANT_NONE_NAME "none"

int TarsTenant_IsNone(const char *tenant);
int TarsTenant_IsFoc(const char *tenant);
int TarsTenant_IsSystem(const char *tenant);
int TarsTenant_IsReservedDefault(const char *tenant);
int TarsTenant_ValidateGrantName(const char *tenant);
const char *TarsTenant_Display(const char *tenant);
void TarsTenant_Copy(char *dst, uint32_t dst_size, const char *src);

#endif /* TARS_TENANT_H */
