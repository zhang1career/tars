#include "tars_manifest.h"
#include "tars_app.h"
#include <string.h>

static tars_whitelist_group_t s_whitelist[TARS_WHITELIST_GROUP_MAX];
static uint8_t s_whitelist_count;

static int resource_key_equal(const tars_resource_t *a, const tars_resource_t *b)
{
  return (a->type == b->type) &&
         (a->instance == b->instance) &&
         (a->param == b->param);
}

static int whitelist_allows(const tars_resource_t *res, const char *a, const char *b)
{
  uint32_t i;
  uint32_t j;
  int found_a = 0;
  int found_b = 0;

  for (i = 0U; i < s_whitelist_count; i++)
  {
    if (!resource_key_equal(&s_whitelist[i].resource, res))
    {
      continue;
    }

    found_a = 0;
    found_b = 0;

    for (j = 0U; j < s_whitelist[i].member_count; j++)
    {
      if (strcmp(s_whitelist[i].members[j], a) == 0)
      {
        found_a = 1;
      }

      if (strcmp(s_whitelist[i].members[j], b) == 0)
      {
        found_b = 1;
      }
    }

    if (found_a && found_b)
    {
      return 1;
    }
  }

  return 0;
}

static int manifest_has_resource(const tars_manifest_t *manifest, const tars_resource_t *res)
{
  uint32_t i;

  for (i = 0U; i < manifest->count; i++)
  {
    if (resource_key_equal(&manifest->items[i], res))
    {
      return 1;
    }
  }

  return 0;
}

tars_status_t TarsManifest_Validate(const tars_manifest_t *manifest)
{
  uint32_t i;

  if (manifest == NULL)
  {
    return TARS_ERR_PARAM;
  }

  if (manifest->count > TARS_RESOURCE_MAX)
  {
    return TARS_ERR_RESOURCE;
  }

  for (i = 0U; i < manifest->count; i++)
  {
    if (manifest->items[i].type == TARS_RESOURCE_RESERVED)
    {
      return TARS_ERR_RESOURCE;
    }
  }

  return TARS_OK;
}

tars_status_t TarsManifest_CheckConflict(const char *name,
                                         const tars_manifest_t *manifest,
                                         const tars_app_runtime_t *owners,
                                         uint32_t owner_count)
{
  uint32_t i;
  uint32_t j;
  uint32_t k;

  if ((name == NULL) || (manifest == NULL) || (owners == NULL))
  {
    return TARS_ERR_PARAM;
  }

  for (i = 0U; i < manifest->count; i++)
  {
    for (j = 0U; j < owner_count; j++)
    {
      if (!owners[j].submitted)
      {
        continue;
      }

      if (strcmp(owners[j].name, name) == 0)
      {
        continue;
      }

      for (k = 0U; k < owners[j].manifest.count; k++)
      {
        if (!resource_key_equal(&manifest->items[i], &owners[j].manifest.items[k]))
        {
          continue;
        }

        if (whitelist_allows(&manifest->items[i], name, owners[j].name))
        {
          break;
        }

        return TARS_ERR_CONFLICT;
      }
    }
  }

  return TARS_OK;
}

void TarsManifest_ClearWhitelist(void)
{
  s_whitelist_count = 0U;
  memset(s_whitelist, 0, sizeof(s_whitelist));
}

tars_status_t TarsManifest_AddWhitelistGroup(const tars_whitelist_group_t *group)
{
  if ((group == NULL) || (group->member_count == 0U) ||
      (group->member_count > TARS_WHITELIST_MEMBER_MAX))
  {
    return TARS_ERR_PARAM;
  }

  if (s_whitelist_count >= TARS_WHITELIST_GROUP_MAX)
  {
    return TARS_ERR_STATE;
  }

  s_whitelist[s_whitelist_count++] = *group;
  return TARS_OK;
}

int TarsManifest_ResourceInManifest(const tars_manifest_t *manifest, const tars_resource_t *res)
{
  return manifest_has_resource(manifest, res);
}

int TarsManifest_CanShare(const char *app_a, const char *app_b, const tars_resource_t *res)
{
  return whitelist_allows(res, app_a, app_b);
}
