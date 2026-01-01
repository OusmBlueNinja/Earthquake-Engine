#include "CAssetManager.h"

#include <unordered_map>
#include <string>

#include "stb_image.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

static std::unordered_map<std::string, uint32_t> g_icon_cache;

void CAssetManager::Init()
{
    stbi_set_flip_vertically_on_load(1);
}

void CAssetManager::Shutdown()
{
    for (auto &kv : g_icon_cache)
    {
        GLuint tex = (GLuint)kv.second;
        if (tex != 0)
            glDeleteTextures(1, &tex);
    }
    g_icon_cache.clear();
}

uint32_t CAssetManager::GetIconTexture(const char *path)
{
    if (!path || !path[0])
        return 0;

    auto it = g_icon_cache.find(path);
    if (it != g_icon_cache.end())
        return it->second;

    uint32_t tex = LoadIconTexture(path);
    g_icon_cache.emplace(std::string(path), tex);
    return tex;
}

uint32_t CAssetManager::LoadIconTexture(const char *path)
{
    int w = 0, h = 0, comp = 0;
    stbi_set_flip_vertically_on_load(true);
    unsigned char *pixels = stbi_load(path, &w, &h, &comp, 4);
    if (!pixels || w <= 0 || h <= 0)
    {
        if (pixels)
            stbi_image_free(pixels);
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);
    return (uint32_t)tex;
}
