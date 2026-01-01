#pragma once

#include <stdint.h>

class CAssetManager
{
public:
    static void Init();
    static void Shutdown();

    static uint32_t GetIconTexture(const char* path);

private:
    CAssetManager() = delete;

    static uint32_t LoadIconTexture(const char* path);
};
