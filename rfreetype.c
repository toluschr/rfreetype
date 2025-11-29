#include "lru-cache/lru-cache.h"
#include "rfreetype.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_SIZES_H

#define INVALID_CODEPOINT 0x1FFFFF

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <GL/gl.h>
#include <GL/glext.h>

struct rFontCacheEntryFT {
    unsigned int font : 11;
    unsigned int codepoint : 21;
    unsigned int index : 32;
};

struct rFontCacheFT {
    struct lru_cache lc;
    Texture2D texture;

    uint32_t cellWidth;
    uint32_t cellHeight;

    uint32_t gridWidth;
    uint32_t gridHeight;

    uint32_t uidMap[64];
    GlyphInfoFT metrics[];
};

static FT_Library ftLibrary;

static void
InitializeFontCache(rFontCacheFT *fontCache)
{
    GLint maxTextureSize;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);

    unsigned int maxGridWidth = maxTextureSize / fontCache->cellWidth;
    unsigned int maxGridHeight = maxTextureSize / fontCache->cellHeight;

    if (fontCache->lc.nmemb < maxGridWidth) {
        fontCache->gridWidth = fontCache->lc.nmemb;
        fontCache->gridHeight = 1;
    } else if ((fontCache->lc.nmemb + maxGridWidth - 1) / maxGridWidth < maxGridHeight) {
        fontCache->gridWidth = maxGridWidth;
        fontCache->gridHeight = (fontCache->lc.nmemb + maxGridWidth - 1) / maxGridWidth;
    } else {
        fontCache->gridWidth = maxGridWidth;
        fontCache->gridHeight = maxGridHeight;
    }

    if (fontCache->gridWidth * fontCache->gridHeight < fontCache->lc.nmemb) {
        lru_cache_set_nmemb(&fontCache->lc, fontCache->gridWidth * fontCache->gridHeight, NULL, NULL);
    }

    fontCache->texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    fontCache->texture.height = fontCache->gridHeight * fontCache->cellHeight;
    fontCache->texture.width = fontCache->gridWidth * fontCache->cellWidth;
    fontCache->texture.id = rlLoadTexture(
        NULL,
        fontCache->texture.width,
        fontCache->texture.height,
        fontCache->texture.format,
        1
    );
}

static uint32_t
Hash_rFontCacheEntryFT(const void *a_, uint32_t nmemb)
{
    unsigned long hash = 5381;
    rFontCacheEntryFT *a = (rFontCacheEntryFT *)a_;
    hash = ((hash << 5) + hash) + a->font;
    hash = ((hash << 5) + hash) + a->codepoint;
    hash = ((hash << 5) + hash) + a->index;
    return hash % nmemb;
}

static int
Compare_rFontCacheEntryFT(const void *a_, const void *b_)
{
    rFontCacheEntryFT *a = (rFontCacheEntryFT *)a_;
    rFontCacheEntryFT *b = (rFontCacheEntryFT *)b_;

    if (a->font < b->font) return -1;
    if (a->font > b->font) return +1;

    if (a->codepoint < b->codepoint) return -1;
    if (a->codepoint > b->codepoint) return +1;

    if (a->index < b->index) return -1;
    if (a->index > b->index) return +1;

    return 0;
}

FontCacheFT
LoadFontCacheFT(uint32_t cellWidth, uint32_t cellHeight, uint32_t nmemb)
{
    size_t hashmapSize, cacheSize;
    lru_cache_calc_sizes(sizeof(rFontCacheEntryFT), nmemb, &hashmapSize, &cacheSize);

    size_t baseSize = sizeof(rFontCacheFT) + (nmemb * sizeof(FT_Glyph_Metrics));
    size_t totalSize = baseSize + hashmapSize + cacheSize;
    unsigned char *memory = malloc(totalSize);

    rFontCacheFT *fontCache = (rFontCacheFT *)&memory[0];
    uint32_t *hashmap = (uint32_t *)&memory[baseSize];
    uint32_t *cache = (uint32_t *)&memory[baseSize + hashmapSize];

    memset(fontCache, 0, sizeof(*fontCache));
    fontCache->cellWidth = cellWidth;
    fontCache->cellHeight = cellHeight;

    lru_cache_init(
        &fontCache->lc,
        sizeof(rFontCacheEntryFT),
        Hash_rFontCacheEntryFT,
        Compare_rFontCacheEntryFT,
        NULL
    );

    lru_cache_set_nmemb(&fontCache->lc, nmemb, NULL, NULL);
    lru_cache_set_memory(&fontCache->lc, hashmap, cache);
    return (FontCacheFT){fontCache};
}

FontFileFT
LoadFontFileFT(const char *filename)
{
    if (!ftLibrary) {
        FT_Init_FreeType(&ftLibrary);
    }

    FT_Face face;
    FT_New_Face(ftLibrary, filename, 0, &face);
    return (FontFileFT){face};
}

FontFileFT
LoadFontFileFromMemoryFT(const unsigned char *data, int dataSize)
{
    if (!ftLibrary) {
        FT_Init_FreeType(&ftLibrary);
    }

    FT_Face face;
    FT_New_Memory_Face(ftLibrary, data, dataSize, 0, &face);
    return (FontFileFT){face};
}


FontFT
LoadFontFT(FontFileFT fontFile, unsigned int height, FontCacheFT fontCache)
{
    FT_Size size;
    uint32_t uid = 0;

    if (fontCache.cache) {
        for (int i = 0; i < (1 << 11) && !uid; i++) {
            uid = ((~fontCache.cache->uidMap[i]) & -(~fontCache.cache->uidMap[i])) + (32 * i);
        }
    }

    if (fontFile.face == NULL || FT_New_Size(fontFile.face, &size)) {
        return (FontFT){0, NULL};
    }

    FT_Activate_Size(size);
    FT_Set_Pixel_Sizes(fontFile.face, 0, height);

    if (fontCache.cache && uid) {
        size->generic.data = fontCache.cache;
        fontCache.cache->uidMap[uid / 32] |= uid;
    }

    return (FontFT){uid, size};
}

// Returns advance
Vector2 DrawTextCodepointFT(FontFT font, int codepoint, Vector2 position, Color tint)
{
    rFontCacheFT *fontCache = font.size->generic.data;
    FT_Size size = font.size;
    FT_Face face = font.size->face;
    rFontCacheEntryFT entry = (rFontCacheEntryFT){
        .font = font.uid,
        .codepoint = codepoint,
        .index = 0,
    };

    if (fontCache->gridWidth * fontCache->gridHeight == 0) {
        InitializeFontCache(fontCache);
    }

    if (codepoint == '\n') {
        return (Vector2){0, size->metrics.height >> 6};
    }

    FT_Activate_Size(size);

    int i = 0;
    int j = 0;

    for (;;) {
        bool put;
        uint32_t id;

        id = lru_cache_get_or_put(&fontCache->lc, &entry, &put);
        if (put) {
            FT_UInt char_index = FT_Get_Char_Index(face, codepoint);

            if (face->glyph->glyph_index != char_index) {
                FT_Load_Glyph(face, char_index, FT_LOAD_NO_HINTING | FT_LOAD_DEFAULT);

                if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
                    FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
                }
            }

            fontCache->metrics[id].width = face->glyph->bitmap.width;
            fontCache->metrics[id].height = face->glyph->bitmap.rows;
            fontCache->metrics[id].advanceX = face->glyph->metrics.horiAdvance >> 6;
            fontCache->metrics[id].offsetY = (size->metrics.ascender - face->glyph->metrics.horiBearingY) >> 6;
            fontCache->metrics[id].offsetX = (face->glyph->metrics.horiBearingX) >> 6;
            fontCache->metrics[id].value = codepoint;
        }

        GlyphInfoFT metrics = fontCache->metrics[id];
        uint32_t x = (id % fontCache->gridWidth) * fontCache->cellWidth;
        uint32_t y = (id / fontCache->gridWidth) * fontCache->cellHeight;
        uint32_t w = metrics.width - (i * fontCache->cellWidth);
        if (w > fontCache->cellWidth)
            w = fontCache->cellWidth;

        uint32_t h = metrics.height - (j * fontCache->cellHeight);
        if (h > fontCache->cellHeight)
            h = fontCache->cellHeight;

        if (put) {
            glBindTexture(GL_TEXTURE_2D, fontCache->texture.id);

            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, face->glyph->bitmap.pitch);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, i * fontCache->cellWidth);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, j * fontCache->cellHeight);

            GLint swizzle[] = {GL_ONE, GL_ONE, GL_ONE, GL_ALPHA};
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                x,
                y,
                w,
                h,
                GL_ALPHA,
                GL_UNSIGNED_BYTE,
                face->glyph->bitmap.buffer
            );
        }

        DrawTexturePro(
            fontCache->texture,
            (Rectangle){ x, y, w, h },
            (Rectangle){ position.x + metrics.offsetX, position.y + metrics.offsetY, w, h },
            (Vector2){ 0, 0 },
            0.0f,
            tint
        );

        entry.index++;

        if (w == fontCache->cellWidth) {
            i++;
            continue;
        }

        if (h == fontCache->cellHeight) {
            i = 0;
            j = j + 1;
            continue;
        }

        return (Vector2){metrics.advanceX, metrics.offsetY + metrics.height};
    }
}

Vector2
DrawTextFT(FontFT font, const char *text, int x, int y, Color tint)
{
    Vector2 position = (Vector2){x, y};
    Vector2 current = position;

    for (int i = 0; text[i]; ) {
        int codepointSize = 0;
        int codepoint = GetCodepointNext(&text[i], &codepointSize);

        Vector2 metrics = DrawTextCodepointFT(font, codepoint, current, tint);
        if (metrics.x == 0) {
            current.y += metrics.y;
            current.x = position.x;
        } else {
            current.x += metrics.x;
        }

        i += codepointSize;
    }

    return Vector2Subtract(current, position);
}

Vector2
DrawTextCodepointsFT(FontFT font, const int *codepoints, int codepointCount, Vector2 position, Color tint)
{
    Vector2 current = position;

    for (int i = 0; i < codepointCount; i++) {
        Vector2 metrics = DrawTextCodepointFT(font, codepoints[i], current, tint);
        if (metrics.x == 0) {
            current.y += metrics.y;
            current.x = position.x;
        } else {
            current.x += metrics.x;
        }
    }

    return Vector2Subtract(current, position);
}

void
UnloadFontFT(FontFT font)
{
    rFontCacheFT *fontCache = font.size->generic.data;

    if (fontCache && font.uid) {
        uint32_t i;
        struct lru_cache_entry *e;
        fontCache->uidMap[font.uid / 32] &= ~font.uid;

        // invalidate entry
        LRU_CACHE_ITERATE_MRU_TO_LRU(&fontCache->lc, i, e) {
            rFontCacheEntryFT *cacheEntry = (rFontCacheEntryFT *)e->key;
            if (cacheEntry->font == font.uid) {
                cacheEntry->codepoint = INVALID_CODEPOINT;
            }
        }
    }

    FT_Done_Size(font.size);
}

void
UnloadFontFileFT(FontFileFT fontFile)
{
    FT_Done_Face(fontFile.face);
}

void
UnloadFontCacheFT(FontCacheFT fontCache)
{
    UnloadTexture(fontCache.cache->texture);
    free(fontCache.cache);
}
