#include "asset_scene_yaml.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "utils/logger.h"

static int path_has_suffix_ci(const char *path, const char *suffix_lower)
{
    if (!path || !suffix_lower)
        return 0;
    size_t n = strlen(path);
    size_t m = strlen(suffix_lower);
    if (n < m)
        return 0;
    const char *s = path + (n - m);
    for (size_t i = 0; i < m; ++i)
    {
        char a = s[i];
        char b = suffix_lower[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return 1;
}

static bool asset_scene_can_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr)
{
    (void)am;
    if (path_is_ptr)
        return false;
    if (!path || !path[0])
        return false;

    if (path_has_suffix_ci(path, ".scene"))
        return true;
    if (path_has_suffix_ci(path, ".scene.yaml"))
        return true;
    if (path_has_suffix_ci(path, ".scene.yml"))
        return true;

    return false;
}

static bool asset_scene_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr, asset_any_t *out_asset, ihandle_t *out_handle)
{
    (void)am;
    if (out_handle)
        *out_handle = ihandle_invalid();
    if (!out_asset || !path || path_is_ptr)
        return false;

    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0)
    {
        fclose(f);
        return false;
    }

    vector_t bytes = create_vector(uint8_t);
    uint8_t z = 0;
    vector_resize(&bytes, (uint32_t)sz + 1u, &z);

    size_t got = 0;
    if (sz > 0)
        got = fread(bytes.data, 1, (size_t)sz, f);
    fclose(f);

    if (got != (size_t)sz)
    {
        vector_free(&bytes);
        return false;
    }

    ((uint8_t *)bytes.data)[sz] = 0;

    out_asset->type = ASSET_SCENE;
    out_asset->state = ASSET_STATE_LOADING;
    out_asset->as.scene.text = bytes;
    return true;
}

static void asset_scene_cleanup(asset_manager_t *am, asset_any_t *asset)
{
    (void)am;
    if (!asset || asset->type != ASSET_SCENE)
        return;
    vector_free(&asset->as.scene.text);
}

asset_module_desc_t asset_module_scene_yaml(void)
{
    asset_module_desc_t m = {0};
    m.type = ASSET_SCENE;
    m.name = "ASSET_SCENE_YAML";
    m.load_fn = asset_scene_load;
    m.cleanup_fn = asset_scene_cleanup;
    m.can_load_fn = asset_scene_can_load;
    return m;
}

