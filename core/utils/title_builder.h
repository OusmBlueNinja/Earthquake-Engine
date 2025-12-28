#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "core.h"

static char *app_build_title(Application *app)
{
    if (!app)
        return NULL;

    vector_t *v = &app->layers;

    uint32_t count = 0;
    layer_t *layers = NULL;

    if (v && v->data && v->element_size == sizeof(layer_t) && v->size)
    {
        count = (uint32_t)v->size;
        layers = (layer_t *)v->data;
    }

    size_t len = 0;
    len += strlen(ENGINE_N);
    len += strlen(" | ");

    if (count)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            if ((layers[i].flags & LAYER_FLAG_HIDDEN_IN_TITLE) != 0)
                continue;

            const char *nm = (layers[i].name && layers[i].name[0]) ? layers[i].name : "Unnamed Layer";
            len += strlen(nm);
            len += strlen(" | ");
        }
    }

    len += strlen("v");
    len += strlen(ENGINE_V);

    char *title = (char *)malloc(len + 1);
    if (!title)
        return NULL;

    char *w = title;
    w += sprintf(w, "%s | ", ENGINE_N);

    if (count)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            if (layer_get_flags(&layers[i], LAYER_FLAG_HIDDEN_IN_TITLE))
                continue;

            const char *nm = (layers[i].name && layers[i].name[0]) ? layers[i].name : "Unnamed Layer";

            w += sprintf(w, "%s | ", nm);
        }
    }

    sprintf(w, "v%s", ENGINE_V);
    return title;
}
