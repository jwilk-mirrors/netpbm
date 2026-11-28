/* leaftoppm.c - read an ileaf img file and write a PPM
 *
 * Copyright (C) 1994 by Bill O'Donnell.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  This software is provided "as is" without express or
 * implied warranty.
 *
 * known problems: doesn't do compressed ileaf images.
 *
 */

#include <stdio.h>
#include "ppm.h"

#define MAXCOLORS 256
#define LEAF_MAXVAL 255


static unsigned char
nextByte(FILE * const ifP) {

    char c;
    pm_readchar(ifP, &c);

    return c;
}



static void
readLeafHeader(FILE *  const ifP,
               int *   const colsP,
               int *   const rowsP,
               int *   const depthP,
               int *   const ncolorsP,
               pixel * const colors) {

    unsigned char buf[256];
    char compressed;
    short version;
    short cols;
    short rows;
    short depth;
    long magic;
    size_t bytesReadCt;

    pm_readbiglong(ifP, &magic);

    if ((uint32_t)magic != 0x894f5053)
        pm_error("Bad magic number.  First 4 bytes should be "
                 "0x894f5053 but are instead 0x%08x", (unsigned)magic);

    /* version =   2 bytes
       hres =      2
       vres =      2
       unique id = 4
       offset x =  2
       offset y =  2
       TOTAL    =  14 bytes
    */

    pm_readbigshort(ifP, &version);

    bytesReadCt = fread(buf, 1, 12, ifP);
    if (bytesReadCt != 12)
        pm_error("bad header, short file?");

    pm_readbigshort(ifP, &cols);
    *colsP = cols;
    pm_readbigshort(ifP, &rows);
    *rowsP = rows;
    pm_readbigshort(ifP, &depth);
    *depthP = depth;

    pm_readchar(ifP, &compressed);

    if (compressed)
        pm_error("Can't do compressed images.");

    if ((*depthP == 1) && (version < 4)) {
        nextByte(ifP);
        *ncolorsP = 0;
    } else if ((*depthP == 8) && (version < 4)) {
        nextByte(ifP);
        *ncolorsP = 0;
    } else {
        long format;
        pm_readbiglong(ifP, &format);
        if (format == 0x29000000) {
            /* color image */
            short ncolors;
            pm_readbigshort(ifP, &ncolors);

            if (ncolors > 0) {
                /* 8-bit image */
                unsigned int i;

                if (ncolors > 256)
                    pm_error("Can't have > 256 colors in colormap.");
                /* read colormap */
                for (i = 0; i < 256; ++i)
                    PPM_PUTR(colors[i], nextByte(ifP));
                for (i = 0; i < 256; ++i)
                    PPM_PUTG(colors[i], nextByte(ifP));
                for (i = 0; i < 256; ++i)
                    PPM_PUTB(colors[i], nextByte(ifP));
                *ncolorsP = ncolors;
            } else {
                /* 24-bit image */
                *ncolorsP = 0;
            }
        } else if (*depthP ==1) {
            /* mono image */
            short dummy;
            pm_readbigshort(ifP, &dummy);  /* must toss cmap size */
            *ncolorsP = 0;
        } else {
            /* gray image */
            short dummy;
            pm_readbigshort(ifP, &dummy);  /* must toss cmap size */
            *ncolorsP = 0;
        }
    }
}




static void
convertGrayRaster(FILE * const ifP,
                  int    const cols,
                  int    const rows,
                  pixval const maxval,
                  FILE * const ofP) {

    gray * grayrow;
    unsigned int row;

    pgm_writepgminit(stdout, cols, rows, maxval, 0);

    grayrow = pgm_allocrow(cols);

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        for (col = 0; col < cols; ++col)
            grayrow[col] = nextByte(ifP);

        if (cols % 2)
            nextByte(ifP);  /* padding */

        pgm_writepgmrow(stdout, grayrow, cols, maxval, 0);
    }
    pgm_freerow(grayrow);
}



static void
convertTrueColorRaster(FILE * const ifP,
                       int    const cols,
                       int    const rows,
                       pixval const maxval,
                       FILE * const ofP) {

    pixel * pixrow;
    unsigned int row;

    ppm_writeppminit(stdout, cols, rows, maxval, 0);

    pixrow = ppm_allocrow(cols);

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        for (col = 0; col < cols; ++col)
            PPM_PUTR(pixrow[col], nextByte(ifP));

        if (cols % 2 != 0)
            nextByte(ifP); /* padding */

        for (col = 0; col < cols; ++col)
            PPM_PUTG(pixrow[col], nextByte(ifP));

        if (cols % 2)
            nextByte(ifP); /* padding */

        for (col = 0; col < cols; ++col)
            PPM_PUTB(pixrow[col], nextByte(ifP));

        if (cols % 2 != 0)
            nextByte(ifP); /* padding */

        ppm_writeppmrow(stdout, pixrow, cols, maxval, 0);
    }
    ppm_freerow(pixrow);
}



static void
convertColorMappedRaster(FILE *        const ifP,
                         int           const cols,
                         int           const rows,
                         pixval        const maxval,
                         const pixel * const colormap,
                         FILE *        const ofP) {

    pixel * pixrow;
    unsigned int row;

    ppm_writeppminit(stdout, cols, rows, (pixval) maxval, 0);

    pixrow = ppm_allocrow(cols);

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        for (col = 0; col < cols; ++col)
            pixrow[col] = colormap[nextByte(ifP)];

        if (cols % 2 != 0)
            nextByte(ifP); /* padding */

        ppm_writeppmrow(stdout, pixrow, cols, maxval, 0);
    }
    ppm_freerow(pixrow);

}



static void
convertMonoRaster(FILE * const ifP,
                  int    const cols,
                  int    const rows,
                  FILE * const ofP) {

    bit * bitrow;
    unsigned int row;

    pbm_writepbminit(stdout, cols, rows, 0);

    bitrow = pbm_allocrow(cols);

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        unsigned char bits;

        for (col = 0, bits = 0x00; col < cols; ++col) {
            int const shift = col % 8;

            if (shift == 0)
                bits = nextByte(ifP);

            bitrow[col] = (bits & (unsigned char)(0x01 << (7 - shift))) ?
                PBM_WHITE : PBM_BLACK;
        }
        if (cols % 16 != 0 && cols % 16 <= 8)
            nextByte(ifP);  /* 16 bit pad */

        pbm_writepbmrow(stdout, bitrow, cols, 0);
    }
    pbm_freerow(bitrow);
}



int
main(int argc, const char * argv[]) {

    pixval const maxval = LEAF_MAXVAL;

    FILE * ifP;
    pixel colormap[MAXCOLORS];
    int rows, cols;
    int depth;
    int ncolors;

    pm_proginit(&argc, argv);

    if (argc-1 > 1)
        pm_error("Too many arguments.  Only argument is ileaf file name");

    if (argc-1 == 1)
        ifP = pm_openr(argv[1]);
    else
        ifP = stdin;

    readLeafHeader(ifP, &cols, &rows, &depth, &ncolors, colormap);

    if ((depth == 8) && (ncolors == 0))
        convertGrayRaster(ifP, cols, rows, maxval, stdout);
    else if (depth == 24)
        convertTrueColorRaster(ifP, cols, rows, maxval, stdout);
    else if (depth == 8)
        convertColorMappedRaster(ifP, cols, rows, maxval, colormap, stdout);
    else if (depth == 1)
        convertMonoRaster(ifP, cols, rows, stdout);

    pm_close(ifP);

    return 0;
}



