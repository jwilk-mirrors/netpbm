#include <stdbool.h>
#include <assert.h>
#include <png.h>
/* Because of a design error in png.h, you must not #include <setjmp.h> before
   <png.h>.  If you do, png.h won't compile.
*/
#include <setjmp.h>
#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "pm.h"

/* <png.h> defines (or doesn't) PNG_iTXt_SUPPORTED to tell us whether libpng
   has facilities related to PNG iTXt chunks.
*/
#ifdef PNG_iTXt_SUPPORTED
  #define HAVE_PNGLIB_WITH_ITXT 1
#else
  #define HAVE_PNGLIB_WITH_ITXT 0
#endif

#include "pngx.h"


static void
errorHandler(png_structp     const png_ptr,
             png_const_charp const msg) {

    jmp_buf * jmpbufP;

    /* This function, aside from the extra step of retrieving the "error
       pointer" (below) and the fact that it exists within the application
       rather than within libpng, is essentially identical to libpng's
       default error handler.  The second point is critical:  since both
       setjmp() and longjmp() are called from the same code, they are
       guaranteed to have compatible notions of how big a jmp_buf is,
       regardless of whether _BSD_SOURCE or anything else has (or has not)
       been defined.
    */

    pm_errormsg("fatal libpng error: %s", msg);

    jmpbufP = png_get_error_ptr(png_ptr);

    if (!jmpbufP)
        pm_longjmp();
    else
        longjmp(*jmpbufP, 1);
}



void
pngx_create(struct pngx ** const pngxPP,
            pngx_rw        const rw,
            jmp_buf *      const jmpbufP) {

    struct pngx * pngxP;

    MALLOCVAR(pngxP);

    if (!pngxP)
        pm_error("Failed to allocate memory for PNG object");
    else {
        pngxP->infoPrepared = false;
        pngxP->numPassesRequired = 1;

        switch(rw) {
        case PNGX_READ:
            pngxP->png_ptr = png_create_read_struct(
                PNG_LIBPNG_VER_STRING,
                jmpbufP, errorHandler, NULL);
            break;
        case PNGX_WRITE:
            pngxP->png_ptr = png_create_write_struct(
                PNG_LIBPNG_VER_STRING,
                jmpbufP, errorHandler, NULL);
            break;
        }
        if (!pngxP->png_ptr)
            pm_error("cannot allocate main libpng structure (png_ptr)");
        else {
            pngxP->info_ptr = png_create_info_struct(pngxP->png_ptr);

            if (!pngxP->info_ptr)
                pm_error("cannot allocate libpng info structure (info_ptr)");
            else
                *pngxPP = pngxP;
        }
        pngxP->rw = rw;
    }
}



void
pngx_destroy(struct pngx * const pngxP) {

    switch(pngxP->rw) {
    case PNGX_READ:
        png_destroy_read_struct(&pngxP->png_ptr, &pngxP->info_ptr, NULL);
        break;
    case PNGX_WRITE:
        png_destroy_write_struct(&pngxP->png_ptr, &pngxP->info_ptr);
        break;
    }

    free(pngxP);
}



bool
pngx_chunkIsPresent(struct pngx * const pngxP,
                    uint32_t      const chunkType) {

    return png_get_valid(pngxP->png_ptr, pngxP->info_ptr, chunkType);
}



unsigned int
pngx_bitDepth(struct pngx * const pngxP) {

    return png_get_bit_depth(pngxP->png_ptr, pngxP->info_ptr);
}



png_color_16
pngx_bkgd(struct pngx * const pngxP) {

    png_color_16 * colorP;

    png_get_bKGD(pngxP->png_ptr, pngxP->info_ptr, &colorP);

    return *colorP;
}



png_byte
pngx_colorType(struct pngx * const pngxP) {

    return png_get_color_type(pngxP->png_ptr, pngxP->info_ptr);
}



png_byte
pngx_filterType(struct pngx * const pngxP) {

    return png_get_filter_type(pngxP->png_ptr, pngxP->info_ptr);
}



double
pngx_gama(struct pngx * const pngxP) {

    double retval;

    png_get_gAMA(pngxP->png_ptr, pngxP->info_ptr, &retval);

    return retval;
}



uint32_t
pngx_imageHeight(struct pngx * const pngxP) {

    return png_get_image_height(pngxP->png_ptr, pngxP->info_ptr);
}



uint32_t
pngx_imageWidth(struct pngx * const pngxP) {

    return png_get_image_width(pngxP->png_ptr, pngxP->info_ptr);
}



png_byte
pngx_interlaceType(struct pngx * const pngxP) {

    return png_get_interlace_type(pngxP->png_ptr, pngxP->info_ptr);
}



struct pngx_plte
pngx_plte(struct pngx * const pngxP) {

    struct pngx_plte retval;

    int size;

    png_get_PLTE(pngxP->png_ptr, pngxP->info_ptr, &retval.palette, &size);

    assert(size >= 0);

    retval.size = size;

    return retval;
}



png_color_8
pngx_sbit(struct pngx *   const pngxP) {

    png_color_8 * sbitP;

    png_get_sBIT(pngxP->png_ptr, pngxP->info_ptr, &sbitP);

    return *sbitP;
}



struct pngx_text
pngx_text(struct pngx * const pngxP) {

    struct pngx_text retval;

    int size;

    png_get_text(pngxP->png_ptr, pngxP->info_ptr, &retval.line, &size);

    assert(size >= 0);

    retval.size = size;

    return retval;
}



png_time
pngx_time(struct pngx * const pngxP) {

    png_time * timeP;

    png_get_tIME(pngxP->png_ptr, pngxP->info_ptr, &timeP);

    return *timeP;
}



struct pngx_trns
pngx_trns(struct pngx * const pngxP) {

    struct pngx_trns retval;

    int numTrans;
    png_color_16 * transColorP;

    png_get_tRNS(pngxP->png_ptr, pngxP->info_ptr,
                 &retval.trans, &numTrans, &transColorP);

    assert(numTrans >= 0);

    retval.numTrans = numTrans;
    retval.transColor = *transColorP;

    return retval;
}



uint32_t
pngx_xPixelsPerMeter(struct pngx * const pngxP) {
/*----------------------------------------------------------------------------
  Horizontal pixel density in pixel per meter; 0 if unknown.
-----------------------------------------------------------------------------*/
    return png_get_x_pixels_per_meter(pngxP->png_ptr, pngxP->info_ptr);
}



float
pngx_pixelAspectRatio(struct pngx * const pngxP) {
/*----------------------------------------------------------------------------
  Aspect ratio - y/x.  0.0 if unknown
-----------------------------------------------------------------------------*/
    return png_get_pixel_aspect_ratio(pngxP->png_ptr, pngxP->info_ptr);
}



bool
pngx_pixelAspectRatioIsKnown(struct pngx * const pngxP) {
/*----------------------------------------------------------------------------
  There is pixel aspect ratio information in the PNG image.
-----------------------------------------------------------------------------*/
    return png_get_pixel_aspect_ratio(pngxP->png_ptr, pngxP->info_ptr) != 0.0;
}



uint32_t
pngx_yPixelsPerMeter(struct pngx * const pngxP) {
/*----------------------------------------------------------------------------
  Vertical pixel density in pixel per meter; 0 if unknown.
-----------------------------------------------------------------------------*/
    return png_get_y_pixels_per_meter(pngxP->png_ptr, pngxP->info_ptr);
}



void
pngx_removeChunk(struct pngx * const pngxP,
                 uint32_t      const chunkType) {

    png_set_invalid(pngxP->png_ptr, pngxP->info_ptr, chunkType);
}



void
pngx_setBkgdPalette(struct pngx * const pngxP,
                    unsigned int  const backgroundIndex) {

    png_color_16 background;

    background.index = backgroundIndex;

    png_set_bKGD(pngxP->png_ptr, pngxP->info_ptr, &background);
}



void
pngx_setBkgdRgb(struct pngx * const pngxP,
                png_color_16  const backgroundArg) {

    png_color_16 background;

    background = backgroundArg;

    png_set_bKGD(pngxP->png_ptr, pngxP->info_ptr, &background);
}



void
pngx_setChrm(struct pngx *      const pngxP,
             struct pngx_chroma const chroma) {

    png_set_cHRM(pngxP->png_ptr, pngxP->info_ptr,
                 chroma.wx, chroma.wy,
                 chroma.rx, chroma.ry,
                 chroma.gx, chroma.gy,
                 chroma.bx, chroma.by);
}



const char *
pngx_srgbIntentDesc(pngx_srgbIntent const srgbIntent) {

    switch (srgbIntent) {
    case PNGX_PERCEPTUAL:            return "PERCEPTUAL";
    case PNGX_RELATIVE_COLORIMETRIC: return "RELATIVE COLORIMETRIC";
    case PNGX_SATURATION:            return "SATURATION";
    case PNGX_ABSOLUTE_COLORIMETRIC: return "ABSOLUTE_COLORIMETRIC";
    }
    assert(false);
}



static int
const libpngSrgbIntentCode(pngx_srgbIntent const srgbIntent) {

    switch (srgbIntent) {
    case PNGX_PERCEPTUAL:            return 0;
    case PNGX_RELATIVE_COLORIMETRIC: return 1;
    case PNGX_SATURATION:            return 2;
    case PNGX_ABSOLUTE_COLORIMETRIC: return 3;
    }

    assert(false);  /* All cases above return */
}



void
pngx_setSrgb(struct pngx *   const pngxP,
             pngx_srgbIntent const srgbIntent) {

    png_set_sRGB(pngxP->png_ptr, pngxP->info_ptr,
                 libpngSrgbIntentCode(srgbIntent));
}



void
pngx_setCompressionSize(struct pngx * const pngxP,
                        unsigned int  const bufferSize) {

#if PNG_LIBPNG_VER >= 10009
    png_set_compression_buffer_size(pngxP->png_ptr, bufferSize);
#else
    pm_error("Your PNG library cannot set the compression buffer size.  "
             "You need at least Version 1.0.9 of Libpng; you have Version %s",
             PNG_LIBPNG_VER_STRING);
#endif
}



void
pngx_setFilter(struct pngx * const pngxP,
               int           const filterSet) {

    /* This sets the allowed filters in the compressor.  The filters, and thus
       the interpretation of 'filterSet', is specific to a filter method (aka
       filter type), which you set with pngx_setIhdr.  There is only one
       filter method defined today, though (PNG_FILTER_TYPE_BASE).

       For filter method Base, 'filterSet' is the OR of the following masks,
       each one allowing the compressor to use one filter.  Not that  the
       compressor decides on a row-by-row basis what filter to use.

         PNG_FILTER_NONE
         PNG_FILTER_SUB
         PNG_FILTER_UP
         PNG_FILTER_AVG
         PNG_FILTER_PAETH

       There are also

         PNG_NO_FILTERS
         PNG_ALL_FILTERS
    */
    png_set_filter(pngxP->png_ptr, 0, filterSet);
}



void
pngx_setGama(struct pngx * const pngxP,
             float         const fileGamma) {

    png_set_gAMA(pngxP->png_ptr, pngxP->info_ptr, fileGamma);
}



void
pngx_setGamma(struct pngx * const pngxP,
              float         const screenGamma,
              float         const imageGamma) {

    png_set_gamma(pngxP->png_ptr, screenGamma, imageGamma);
}



void
pngx_setHist(struct pngx * const pngxP,
             png_uint_16 * const histogram) {

    png_set_hIST(pngxP->png_ptr, pngxP->info_ptr, histogram);
}



void
pngx_setIhdr(struct pngx * const pngxP,
             unsigned int  const width,
             unsigned int  const height,
             unsigned int  const bitDepth,
             int           const colorType,
             int           const interlaceMethod,
             int           const compressionMethod,
             int           const filterMethod) {

    png_set_IHDR(pngxP->png_ptr, pngxP->info_ptr, width, height,
                 bitDepth, colorType, interlaceMethod, compressionMethod,
                 filterMethod);
}



void
pngx_setInterlaceHandling(struct pngx * const pngxP) {
    /* The documentation is vague and contradictory on what this does, but
       what it appears from reasoning and experimentation to do is the
       following.

       It applies to reading and writing by rows (png_write_row, png_read_row)
       as opposed to whole image (png_write_image, png_read_image).  It has
       no effect on whole image read and write.

       This is not what makes an image interlaced or tells the decompressor
       that it is interlaced.  All it does is control how you you read and
       write the raster when the image is interlaced.  It has no effect if the
       image is not interlaced.  (You make an image interlaced by setting the
       IHDR; the decompressor finds out from the IHDR that it is interlaced).

       In the write case, it controls whether you construct the subimages
       yourself and feed them to libpng in sequence or you feed libpng the
       entire image multiple times and libpng picks out the pixels appropriate
       for each subimage in each pass.

       In the read case, it controls whether you get the raw subimages and you
       assemble them into the full image or you read the whole image multiple
       times into the same buffer, with the pixels that belong to each
       subimage being filled in on each pass.

       Note that the only kind of interlacing that exists today is ADAM7 and
       consequently, the number of passes is always 1 (for no interlacing) or
       7 (for interlacing).
    */
    if (!pngxP->infoPrepared)
        pm_error("pngx_setInterlaceHandling must not be called before "
                 "pngx_writeInfo or pngx_readInfo");

    pngxP->numPassesRequired = png_set_interlace_handling(pngxP->png_ptr);
}



void
pngx_setPacking(struct pngx * const pngxP) {

    if (!pngxP->infoPrepared)
        pm_error("pngx_setPacking must not be called before "
                 "pngx_writeInfo or pngx_readInfo");

    png_set_packing(pngxP->png_ptr);
}



void
pngx_setPhys(struct pngx *    const pngxP,
             struct pngx_phys const phys) {

    png_set_pHYs(pngxP->png_ptr, pngxP->info_ptr,
                 phys.x, phys.y, phys.unit);
}



void
pngx_setPlte(struct pngx * const pngxP,
             png_color *   const palette,
             unsigned int  const paletteSize) {

    png_set_PLTE(pngxP->png_ptr, pngxP->info_ptr, palette, paletteSize);
}



void
pngx_setSbit(struct pngx * const pngxP,
             png_color_8   const sbitArg) {

    png_color_8 sbit;

    sbit = sbitArg;

    png_set_sBIT(pngxP->png_ptr, pngxP->info_ptr, &sbit);
}



void
pngx_setShift(struct pngx * const pngxP,
              png_color_8   const sigBitArg) {
/*----------------------------------------------------------------------------
   Tell the number of significant bits in the row buffers that will be given
   to the compressor.  Those bits are the least significant of the 8 bits of
   space in the row buffer for each sample.  For example, if red sample values
   are in the range 0-7, only the lower 3 bits of the 8-bit byte for each
   red sample will be used, so one would call this with sigBitArg.red == 3.

   The name alludes to the fact that to normalize the sample to 8 bits, one
   shifts it left, and this function tells how much shift has to happen.  In
   the example above, each red sample has to be shifted left 5 bits (so that
   the upper 3 bits are significant and the lower 5 bits are always zero) to
   create an 8 bit sample out of the 3 bit samples.
-----------------------------------------------------------------------------*/
    png_color_8 sigBit;

    sigBit = sigBitArg;

    png_set_shift(pngxP->png_ptr, &sigBit);
}



void
pngx_setSigBytes(struct pngx * const pngxP,
                 unsigned int  const sigByteCt) {

    assert(sigByteCt <= INT_MAX);

    png_set_sig_bytes(pngxP->png_ptr, sigByteCt);
}



void
pngx_setTextKey(png_text *   const textP,
                const char * const key) {

    /* textP->key is improperly declared in libpng as char *; should
       be const char *
    */
    textP->key = (char *)pm_strdup(key);
}



void
pngx_setTextLang(png_text *   const textP,
                 const char * const language) {

#if HAVE_PNGLIB_WITH_ITXT
    if (language)
        textP->lang = (char *)pm_strdup(language);
    else
        textP->lang = NULL;
#else
    if (language)
        pm_error("PNG library does not have ability to create an iTXT "
                 "chunk (i.e. to create a PNG with text strings in a language "
                 "other than English)");
#endif
}



void
pngx_setTextLangKey(png_text *   const textP,
                    const char * const key) {

#if HAVE_PNGLIB_WITH_ITXT
    textP->lang_key = (char *)pm_strdup(key);
#else
    pm_error("PNG library does not have ability to create an iTXT "
             "chunk (i.e. to create a PNG with text strings in a language "
             "other than English)");
#endif
}



void
pngx_termText(png_text * const textP) {

    pm_strfree(textP->key);

#if HAVE_PNGLIB_WITH_ITXT
    if (textP->lang) {
        pm_strfree(textP->lang);
        pm_strfree(textP->lang_key);
    }
#endif

    free(textP->text);
}



void
pngx_setText(struct pngx * const pngxP,
             png_text *    const textP,
             unsigned int  const count) {

    png_set_text(pngxP->png_ptr, pngxP->info_ptr, textP, count);
}



void
pngx_setTime(struct pngx * const pngxP,
             time_t        const timeArg) {

    png_time pngTime;

    png_convert_from_time_t(&pngTime, timeArg);

    png_set_tIME(pngxP->png_ptr, pngxP->info_ptr, &pngTime);
}



void
pngx_setTrnsPalette(struct pngx *    const pngxP,
                    const png_byte * const transPalette,
                    unsigned int     const paletteSize) {

    png_set_tRNS(pngxP->png_ptr, pngxP->info_ptr,
                 (png_byte *)transPalette, paletteSize, NULL);
}



void
pngx_setTrnsValue(struct pngx * const pngxP,
                  png_color_16  const transColorArg) {

    png_color_16 transColor;

    transColor = transColorArg;

    png_set_tRNS(pngxP->png_ptr, pngxP->info_ptr,
                 NULL, 0, &transColor);
}



void
pngx_readInfo(struct pngx * const pngxP) {

    png_read_info(pngxP->png_ptr, pngxP->info_ptr);

    pngxP->infoPrepared = true;
}



void
pngx_writeInfo(struct pngx * const pngxP) {

    png_write_info(pngxP->png_ptr, pngxP->info_ptr);

    pngxP->infoPrepared = true;
}



static void
verifyFileIsPng(FILE *   const ifP,
                size_t * const consumedByteCtP) {

    unsigned char buffer[4];
    size_t bytesRead;

    bytesRead = fread(buffer, 1, sizeof(buffer), ifP);
    if (bytesRead != sizeof(buffer))
        pm_error("input file is empty or too short");

    if (png_sig_cmp(buffer, (png_size_t) 0, (png_size_t) sizeof(buffer)) != 0)
        pm_error("input file is not a PNG file "
                 "(does not have the PNG signature in its first 4 bytes)");
    else
        *consumedByteCtP = bytesRead;
}



void
pngx_readStart(struct pngx * const pngxP,
               FILE *        const ifP) {

    size_t sigByteCt;

    verifyFileIsPng(ifP, &sigByteCt);

    /* Declare that we already read the signature bytes */
    pngx_setSigBytes(pngxP, (unsigned int)sigByteCt);

    png_init_io(pngxP->png_ptr, ifP);

    pngx_readInfo(pngxP);

    if (pngx_bitDepth(pngxP) < 8)
        pngx_setPacking(pngxP);
}



void
pngx_readRow(struct pngx * const pngxP,
             png_byte *    const rowBuf,
             png_byte *    const displayRow) {

    png_read_row(pngxP->png_ptr, rowBuf, displayRow);
}



void
pngx_readImage(struct pngx * const pngxP,
               png_byte **   const image) {

    png_read_image(pngxP->png_ptr, image);
}



void
pngx_writeRow(struct pngx *    const pngxP,
              const png_byte * const line) {

    png_write_row(pngxP->png_ptr, (png_byte *)line);
}



void
pngx_writeImage(struct pngx * const pngxP,
                png_byte **   const raster) {

    png_write_image(pngxP->png_ptr, (png_byte **)raster);
}



void
pngx_readEnd(struct pngx * const pngxP) {

    /* Note that some of info_ptr is not defined until png_read_end()
       completes.  That's because it comes from chunks that are at the
       end of the stream.  In particular, text and time chunks may
       be at the end.  Furthermore, they may be in both places, in
       which case info_ptr contains different information before and
       after png_read_end().
    */
    png_read_end(pngxP->png_ptr, pngxP->info_ptr);
}



void
pngx_writeEnd(struct pngx * const pngxP) {

    png_write_end(pngxP->png_ptr, pngxP->info_ptr);
}



