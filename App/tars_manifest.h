#ifndef TARS_MANIFEST_H
#define TARS_MANIFEST_H

#include "tars_app.h"

tars_status_t TarsManifest_Validate(const tars_manifest_t *manifest);
tars_status_t TarsManifest_CheckConflict(const char *name,
                                         const tars_manifest_t *manifest,
                                         const tars_app_runtime_t *owners,
                                         uint32_t owner_count);
void TarsManifest_ClearWhitelist(void);
tars_status_t TarsManifest_AddWhitelistGroup(const tars_whitelist_group_t *group);
int TarsManifest_ResourceInManifest(const tars_manifest_t *manifest, const tars_resource_t *res);
int TarsManifest_CanShare(const char *app_a, const char *app_b, const tars_resource_t *res);

#endif /* TARS_MANIFEST_H */
