/* pbmfont.h - header file for font routines in libpbm
*/

#include "pbm.h"

#ifdef __cplusplus
extern "C" {
#endif
#if 0
} /* to fake out automatic code indenters */
#endif


/* Maximum dimensions for fonts */

#define  pbm_maxfontwidth()  65536
#define  pbm_maxfontheight() 65536
    /* These limits are not in the official Adobe BDF definition, but
       should never be a problem for practical purposes, considering that
       a 65536 x 65536 glyph occupies 4G pixels. 
    */

typedef wchar_t PM_WCHAR;
    /* Precaution to make adjustments, if necessary, for systems with
       unique wchar_t.
    */

#define PM_FONT_MAXGLYPH 255

#define PM_FONT2_MAXGLYPH 65535
    /* Upper limit of codepoint value.

       This is large enough to handle Unicode Plane 0 (Basic Multilingual
       Plane: BMP) which defines the great majority of characters used in
       modern languages.

       This can be set to a higher value at some cost to efficiency.
       As of Unicode v. 11.0.0 planes up to 16 are defined.
    */

struct glyph {
    /* A glyph consists of white borders and the "central glyph" which
       can be anything, but normally does not have white borders because
       it's more efficient to represent white borders explicitly.
    */
    unsigned int width;
    unsigned int height;
        /* The dimensions of the central glyph, i.e. the 'bmap' array */
    int x;
        /* Width in pixels of the white left border of this glyph.
           This can actually be negative to indicate that the central
           glyph backs up over the previous character in the line.  In
           that case, if there is no previous character in the line, it
           is as if 'x' is 0.
        */
    int y;
        /* Height in pixels of the white bottom border of this glyph.
           Can be negative.
        */
    unsigned int xadd;
        /* Width of glyph -- white left border plus central glyph
           plus white right border
        */
    const char * bmap;
        /* The raster of the central glyph.  It is an 'width' by
           'height' array in row major order, with each byte being 1
           for black; 0 for white.  E.g. if 'width' is 20 pixels and
           'height' is 40 pixels and it's a rectangle that is black on
           the top half and white on the bottom, this is an array of
           800 bytes, with the first 400 having value 0x01 and the
           last 400 having value 0x00.
        */
};

struct font {
    /* This describes a combination of font and character set.  Given
       an code point in the range 0..255, this structure describes the
       glyph for that character.
    */
    unsigned int maxwidth, maxheight;
    int x;
        /* The minimum value of glyph.font.  The left edge of the glyph
           in the glyph set which advances furthest to the left. */
    int y;
        /* Amount of white space that should be added between lines of
           this font.  Can be negative.
        */
    struct glyph * glyph[256];
        /* glyph[i] is the glyph for code point i */
    const bit ** oldfont;
        /* for compatibility with old pbmtext routines */
        /* oldfont is NULL if the font is BDF derived */
    int fcols, frows;
};


struct font2 {
    /* Font structure for expanded character set.  Code point is in the
       range 0..maxglyph .
     */
    int maxwidth, maxheight;

    int x;
        /* The minimum value of glyph.font.  The left edge of the glyph
           in the glyph set which advances furthest to the left. */
    int y;
        /* Amount of white space that should be added between lines of
           this font.  Can be negative.
        */
    struct glyph ** glyph;
        /* glyph[i] is the glyph for code point i */

    PM_WCHAR maxglyph;
        /* max code point for glyphs, including vacant slots */

    const bit ** oldfont;
        /* for compatibility with old pbmtext routines */
        /* oldfont is NULL if the font is BDF derived */

    unsigned int fcols, frows;
};

struct font *
pbm_defaultfont(const char* const which);

struct font *
pbm_dissectfont(const bit ** const font,
                unsigned int const frows,
                unsigned int const fcols);

struct font *
pbm_loadfont(const char * const filename);

struct font *
pbm_loadpbmfont(const char * const filename);

struct font *
pbm_loadbdffont(const char * const filename);

struct font2 *
pbm_loadbdffont2(const char * const filename,
                 PM_WCHAR     const maxglyph);

struct font2 *
pbm_expandbdffont(const struct font * const font);

void
pbm_dumpfont(struct font * const fontP,
             FILE *        const ofP);

extern struct font pbm_defaultFixedfont;
extern struct font pbm_defaultBdffont;

#ifdef __cplusplus
}
#endif
