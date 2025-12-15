#ifndef RFREETYPE_H_
#define RFREETYPE_H_
#include <raylib.h>
#include <stdint.h>

typedef struct rFontCacheEntryFT rFontCacheEntryFT;
typedef struct rFontCacheFT rFontCacheFT;
typedef struct FT_SizeRec_ FT_SizeRec_;
typedef struct FT_FaceRec_ FT_FaceRec_;

typedef struct GlyphInfoFT {
    unsigned int advanceX;
    unsigned int offsetX;
    unsigned int offsetY;
    unsigned int value;
    struct {
        unsigned int width;
        unsigned int height;
    } image;
} GlyphInfoFT;

typedef struct FontCacheFT {
    rFontCacheFT *cache;
} FontCacheFT;

typedef struct FontFileFT {
    FT_FaceRec_ *face;
} FontFileFT;

typedef struct FontFT {
    uint32_t uid;
    int baseSize;
    FT_SizeRec_ *size;
} FontFT;

FontCacheFT LoadFontCacheFT(uint32_t baseWidth, uint32_t baseHeight, uint32_t nmemb);

FontFileFT LoadFontFileFromMemoryFT(const unsigned char *data, int dataSize);
FontFileFT LoadFontFileFT(const char *filename);
FontFT LoadFontFT(FontFileFT fontFile, unsigned int height, FontCacheFT fontCache);

GlyphInfoFT GetGlyphInfoFT(FontFT font, int codepoint);

Vector2 DrawTextFT(FontFT font, const char *text, int x, int y, Color tint);
Vector2 DrawTextCodepointFT(FontFT font, int codepoint, Vector2 position, Color tint);
Vector2 DrawTextCodepointsFT(FontFT font, const int *codepoints, int codepointCount, Vector2 position, Color tint);

void UnloadFontFT(FontFT font);
void UnloadFontFileFT(FontFileFT fontFile);
void UnloadFontCacheFT(FontCacheFT fontCache);

#endif // RFREETYPE_H_
