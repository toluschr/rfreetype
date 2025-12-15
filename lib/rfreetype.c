#include "lru-cache.h"
#include "rfreetype.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_SIZES_H

#define INVALID_CODEPOINT 0x1FFFFF

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
InitializeFontCache_(rFontCacheFT *fontCache)
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
Hash_rFontCacheEntryFT_(const void *a_, uint32_t nmemb)
{
    unsigned long hash = 5381;
    rFontCacheEntryFT *a = (rFontCacheEntryFT *)a_;
    hash = ((hash << 5) + hash) + a->font;
    hash = ((hash << 5) + hash) + a->codepoint;
    hash = ((hash << 5) + hash) + a->index;
    return hash % nmemb;
}

static int
Compare_rFontCacheEntryFT_(const void *a_, const void *b_)
{
    rFontCacheEntryFT *a = (rFontCacheEntryFT *)a_;
    rFontCacheEntryFT *b = (rFontCacheEntryFT *)b_;
    unsigned int a_data[] = {a->font, a->codepoint, a->index};
    unsigned int b_data[] = {b->font, b->codepoint, b->index};
    return memcmp(a_data, b_data, sizeof(a_data));
}

static GlyphInfoFT
LoadGlyphFT_(FontFT font, int codepoint, int index, Rectangle *atlasRec_, Vector2 *offset_)
{
    bool put;
    uint32_t id;
    FT_Size size;
    FT_Face face;
    Vector2 offset;
    Rectangle atlasRec;
    GlyphInfoFT *gi;
    rFontCacheFT *fontCache;
    rFontCacheEntryFT entry = (rFontCacheEntryFT){
        .font = font.uid,
        .codepoint = codepoint,
        .index = index,
    };

    if (font.size == NULL || font.size->generic.data == NULL) {
        GlyphInfoFT glyphInfo;
        Font defaultFont = GetFontDefault();
        int index = GetGlyphIndex(defaultFont, codepoint);

        float scaling = font.baseSize / defaultFont.baseSize;
        glyphInfo.image.width = scaling + defaultFont.glyphs[index].image.width;
        glyphInfo.image.height = font.baseSize;
        glyphInfo.advanceX = (codepoint != '\n') ? (defaultFont.recs[index].width * scaling) : 0;
        glyphInfo.offsetX = scaling/2;
        glyphInfo.offsetY = 0;
        glyphInfo.value = codepoint;
        return glyphInfo;
    }

    fontCache = font.size->generic.data;
    size = font.size;
    face = font.size->face;

    if (fontCache->gridWidth * fontCache->gridHeight == 0) {
        InitializeFontCache_(fontCache);
    }

    id = lru_cache_get_or_put(&fontCache->lc, &entry, &put);
    gi = &fontCache->metrics[id];

    if (put) {
        FT_UInt char_index;

        FT_Activate_Size(size);
        char_index = FT_Get_Char_Index(face, codepoint);

        if (face->glyph->glyph_index != char_index) {
            FT_Load_Glyph(face, char_index, FT_LOAD_DEFAULT);

            if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
                FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
            }
        }

        gi->advanceX = (codepoint != '\n') ? (face->glyph->metrics.horiAdvance >> 6) : 0;
        gi->offsetY = (size->metrics.ascender - face->glyph->metrics.horiBearingY) >> 6;
        gi->offsetX = (face->glyph->metrics.horiBearingX) >> 6;
        gi->value = codepoint;

        // ignore the pitch
        gi->image.width = face->glyph->bitmap.width;
        gi->image.height = face->glyph->bitmap.rows;
    }

    uint32_t gridWidth = (gi->image.width + fontCache->cellWidth - 1) / fontCache->cellWidth;
    uint32_t x = gridWidth ? index % gridWidth : 0;
    uint32_t y = gridWidth ? index / gridWidth : 0;

    atlasRec.x = (id % fontCache->gridWidth) * fontCache->cellWidth;
    atlasRec.y = (id / fontCache->gridWidth) * fontCache->cellHeight;
    atlasRec.width = gi->image.width - x * fontCache->cellWidth;
    atlasRec.height = gi->image.height - y * fontCache->cellHeight;

    if (atlasRec.width > fontCache->cellWidth) atlasRec.width = fontCache->cellWidth;
    if (atlasRec.height > fontCache->cellHeight) atlasRec.height = fontCache->cellHeight;

    offset.x = x * fontCache->cellWidth;
    offset.y = y * fontCache->cellHeight;

    if (atlasRec_) *atlasRec_ = atlasRec;
    if (offset_) *offset_ = offset;

    if (put) {
        rlDrawRenderBatchActive();
        glBindTexture(GL_TEXTURE_2D, fontCache->texture.id);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, gi->image.width);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, offset.x);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, offset.y);

        GLint swizzle[] = {GL_ONE, GL_ONE, GL_ONE, GL_ALPHA};
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
        glTexSubImage2D(GL_TEXTURE_2D, 0, atlasRec.x, atlasRec.y, atlasRec.width, atlasRec.height, GL_ALPHA, GL_UNSIGNED_BYTE, face->glyph->bitmap.buffer);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    return *gi;
}

FontCacheFT
LoadFontCacheFT(uint32_t cellWidth, uint32_t cellHeight, uint32_t nmemb)
{
    int rc = 0;
    unsigned char *memory = NULL;

    size_t hashmapSize, cacheSize;
    rc = lru_cache_calc_sizes(sizeof(rFontCacheEntryFT), nmemb, &hashmapSize, &cacheSize);
    if (rc < 0) goto invalid_argument;

    size_t baseSize = sizeof(rFontCacheFT) + (nmemb * sizeof(FT_Glyph_Metrics));
    size_t totalSize = baseSize + hashmapSize + cacheSize;

    memory = malloc(totalSize);
    if (memory == NULL) goto cannot_allocate_memory;

    rFontCacheFT *fontCache = (rFontCacheFT *)&memory[0];
    uint32_t *hashmap = (uint32_t *)&memory[baseSize];
    uint32_t *cache = (uint32_t *)&memory[baseSize + hashmapSize];

    memset(fontCache, 0, sizeof(*fontCache));
    fontCache->cellWidth = cellWidth;
    fontCache->cellHeight = cellHeight;

    rc = lru_cache_init(
        &fontCache->lc,
        sizeof(rFontCacheEntryFT),
        Hash_rFontCacheEntryFT_,
        Compare_rFontCacheEntryFT_,
        NULL
    );
    if (rc < 0) goto invalid_argument;

    rc = lru_cache_set_nmemb(&fontCache->lc, nmemb, NULL, NULL);
    if (rc < 0) goto invalid_argument;

    rc = lru_cache_set_memory(&fontCache->lc, hashmap, cache);
    if (rc < 0) goto invalid_argument;

    return (FontCacheFT){fontCache};

cannot_allocate_memory:
    free(memory);
    TRACELOG(LOG_WARNING, "FONTFT: Failed to allocate memory for cache structure");
    return (FontCacheFT){NULL};

invalid_argument:
    free(memory);
    TRACELOG(LOG_WARNING, "FONTFT: Invalid arguments supplied for cache structure");
    return (FontCacheFT){NULL};
}

FontFileFT
LoadFontFileFT(const char *filename)
{
    FT_Error error;

    if (!ftLibrary && (error = FT_Init_FreeType(&ftLibrary))) {
        goto error_init_freetype;
    }

    FT_Face face = NULL;
    if ((error = FT_New_Face(ftLibrary, filename, 0, &face))) {
        goto error_new_face;
    }

    // face->generic.data = ;
    // face->generic.finalizer = ;
    return (FontFileFT){face};

error_init_freetype:
    TRACELOG(LOG_WARNING, "FONTFT: Failed to initialize freetype (%s)", FT_Error_String(error));
    return (FontFileFT){NULL};

error_new_face:
    TRACELOG(LOG_WARNING, "FONTFT: Failed to create the font face (%s)", FT_Error_String(error));
    return (FontFileFT){NULL};
}

FontFileFT
LoadFontFileFromMemoryFT(const unsigned char *data, int dataSize)
{
    FT_Error error;
    FontFileFT out = (FontFileFT){NULL};

    if (!ftLibrary && (error = FT_Init_FreeType(&ftLibrary))) {
        goto error_init_freetype;
    }

    FT_Face face = NULL;
    if ((error = FT_New_Memory_Face(ftLibrary, data, dataSize, 0, &face))) {
        goto error_new_face;
    }

    out.face = face;
    return out;

error_init_freetype:
    TRACELOG(LOG_WARNING, "FONTFT: Failed to initialize freetype (%s)", FT_Error_String(error));
    return out;

error_new_face:
    TRACELOG(LOG_WARNING, "FONTFT: Failed to create the font face (%s)", FT_Error_String(error));
    return out;
}


FontFT
LoadFontFT(FontFileFT fontFile, unsigned int height, FontCacheFT fontCache)
{
    FT_Error error;
    FontFT out = (FontFT){0, height, NULL};
    FT_Size size = NULL;
    uint32_t uid = 0;

    if (fontFile.face == NULL) {
        return out;
    }

    if (fontCache.cache) {
        for (int i = 0; i < (1 << 11) && !uid; i++) {
            uid = ((~fontCache.cache->uidMap[i]) & -(~fontCache.cache->uidMap[i])) + (32 * i);
        }
    }

    if (fontFile.face == NULL || (error = FT_New_Size(fontFile.face, &size))) {
        goto error_new_size;
    }

    if ((error = FT_Activate_Size(size))) {
        goto error_activate_size;
    }

    if ((error = FT_Set_Pixel_Sizes(fontFile.face, 0, height))) {
        goto error_set_pixel_sizes;
    }

    if (fontCache.cache && uid) {
        size->generic.data = fontCache.cache;
        fontCache.cache->uidMap[uid / 32] |= uid;
    }

    out.uid = uid;
    out.size = size;
    return out;

error_set_pixel_sizes:
    FT_Done_Size(size);
    TRACELOG(LOG_WARNING, "FONTFT: Failed to set the requested pixel size (%s)", FT_Error_String(error));
    return out;

error_activate_size:
    FT_Done_Size(size);
    TRACELOG(LOG_WARNING, "FONTFT: Failed to actiavte the font size (%s)", FT_Error_String(error));
    return out;

error_new_size:
    TRACELOG(LOG_WARNING, "FONTFT: Failed to create the font size (%s)", FT_Error_String(error));
    return out;
}

// Returns advance
Vector2 DrawTextCodepointFT(FontFT font, int codepoint, Vector2 position, Color tint)
{
    uint32_t index = 0;

    if (font.size == NULL || font.size->generic.data == NULL) {
        GlyphInfoFT glyphInfo = LoadGlyphFT_(font, codepoint, index, NULL, NULL);

        DrawTextCodepoint(
            GetFontDefault(),
            codepoint,
            (Vector2){position.x + glyphInfo.offsetX, position.y},
            font.baseSize,
            tint
        );

        return (Vector2){glyphInfo.advanceX, glyphInfo.offsetY + font.baseSize};
    }

    for (;;) {
        rFontCacheFT *fontCache = font.size->generic.data;

        Rectangle atlasRec;
        Vector2 offset;
        GlyphInfoFT glyphInfo = LoadGlyphFT_(font, codepoint, index, &atlasRec, &offset);

        DrawTexturePro(
            fontCache->texture,
            atlasRec,
            (Rectangle){
                position.x + glyphInfo.offsetX + offset.x,
                position.y + glyphInfo.offsetY + offset.y,
                atlasRec.width,
                atlasRec.height
            },
            (Vector2){ 0, 0 },
            0.0f,
            tint
        );

        index++;

        uint32_t gridWidth = (glyphInfo.image.width + fontCache->cellWidth - 1) / fontCache->cellWidth;
        uint32_t gridHeight = (glyphInfo.image.height + fontCache->cellHeight - 1) / fontCache->cellHeight;
        if (index >= gridWidth * gridHeight) {
            return (Vector2){glyphInfo.advanceX, glyphInfo.offsetY + glyphInfo.image.height};
        }
    }
}

GlyphInfoFT
GetGlyphInfoFT(FontFT font, int codepoint)
{
    return LoadGlyphFT_(font, codepoint, 0, NULL, NULL);
}

Vector2
DrawTextFT(FontFT font, const char *text, int x, int y, Color tint)
{
    Vector2 position = (Vector2){x, y};
    Vector2 current = position;
    Vector2 max = position;

    for (int i = 0; text[i]; ) {
        int codepointSize = 0;
        int codepoint = GetCodepointNext(&text[i], &codepointSize);

        Vector2 metrics = DrawTextCodepointFT(font, codepoint, current, tint);

        if (max.x < current.x + metrics.x) {
            max.x = current.x + metrics.x;
        }

        if (max.y < current.y + metrics.y) {
            max.y = current.y + metrics.y;
        }

        if (metrics.x == 0) {
            current.y += metrics.y;
            current.x = position.x;
        } else {
            current.x += metrics.x;
        }

        i += codepointSize;
    }

    return Vector2Subtract(max, position);
}

Vector2
DrawTextCodepointsFT(FontFT font, const int *codepoints, int codepointCount, Vector2 position, Color tint)
{
    Vector2 current = position;
    Vector2 max = position;

    for (int i = 0; i < codepointCount; i++) {
        Vector2 metrics = DrawTextCodepointFT(font, codepoints[i], current, tint);

        if (max.x < current.x + metrics.x) {
            max.x = current.x + metrics.x;
        }

        if (max.y < current.y + metrics.y) {
            max.y = current.y + metrics.y;
        }

        if (metrics.x == 0) {
            current.y += metrics.y;
            current.x = position.x;
        } else {
            current.x += metrics.x;
        }
    }

    return Vector2Subtract(max, position);
}

void
UnloadFontFT(FontFT font)
{
    if (!font.size) return;

    rFontCacheFT *fontCache = font.size->generic.data;

    if (fontCache && font.uid) {
        uint32_t i;
        struct lru_cache_entry *e;
        fontCache->uidMap[font.uid / 32] &= ~font.uid;

        // invalidate entry
        LRU_CACHE_ITERATE_MRU_TO_LRU(&fontCache->lc, i, e) {
            rFontCacheEntryFT *cacheEntry = (rFontCacheEntryFT *)e->key;
            if (cacheEntry->font == font.uid) {
                // @todo: move_chain using new adaptive lru api
                cacheEntry->codepoint = INVALID_CODEPOINT;
            }
        }
    }

    FT_Done_Size(font.size);
}

void
UnloadFontFileFT(FontFileFT fontFile)
{
    if (!fontFile.face) return;

    FT_Done_Face(fontFile.face);
}

void
UnloadFontCacheFT(FontCacheFT fontCache)
{
    if (!fontCache.cache) return;

    UnloadTexture(fontCache.cache->texture);
    free(fontCache.cache);
}
