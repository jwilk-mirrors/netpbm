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

static void
readLeafHeader(FILE *  const fp,
               int *   const colsP,
               int *   const rowsP,
               int *   const depthP,
               int *   const ncolorsP,
               pixel * const colors) {

    unsigned char buf[256];
    unsigned char compressed;
    short version;
    short cols;
    short rows;
    short depth;
    long magic;

    pm_readbiglong(fp, &magic);

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

    pm_readbigshort(fp, &version);

    if (fread(buf, 1, 12, fp) != 12)
        pm_error("bad header, short file?");

    pm_readbigshort(fp, &cols);
    *colsP = cols;
    pm_readbigshort(fp, &rows);
    *rowsP = rows;
    pm_readbigshort(fp, &depth);
    *depthP = depth;

    if ((compressed = fgetc(fp)) != 0)
        pm_error("Can't do compressed images.");

    if ((*depthP == 1) && (version < 4)) {
        fgetc(fp);
        *ncolorsP = 0;
    } else if ((*depthP == 8) && (version < 4)) {
        fgetc(fp);
        *ncolorsP = 0;
    } else {
        long format;
        pm_readbiglong(fp, &format);
        if (format == 0x29000000) {
            /* color image */
            short ncolors;
            pm_readbigshort(fp, &ncolors);

            if (ncolors > 0) {
                /* 8-bit image */
                unsigned int i;

                if (ncolors > 256)
                    pm_error("Can't have > 256 colors in colormap.");
                /* read colormap */
                for (i=0; i < 256; ++i)
                    PPM_PUTR(colors[i], fgetc(fp));
                for (i=0; i < 256; ++i)
                    PPM_PUTG(colors[i], fgetc(fp));
                for (i=0; i < 256; ++i)
                    PPM_PUTB(colors[i], fgetc(fp));
                *ncolorsP = ncolors;
            } else {
                /* 24-bit image */
                *ncolorsP = 0;
            }
        } else if (*depthP ==1) {
            /* mono image */
            short dummy;
            pm_readbigshort(fp, &dummy);  /* must toss cmap size */
            *ncolorsP = 0;
        } else {
            /* gray image */
            short dummy;
            pm_readbigshort(fp, &dummy);  /* must toss cmap size */
            *ncolorsP = 0;
        }
    }
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

    if ((depth == 8) && (ncolors == 0)) {
        /* gray image */
        gray * grayrow;
        unsigned int row;
        pgm_writepgminit(stdout, cols, rows, maxval, 0);
        grayrow = pgm_allocrow(cols);
        for (row = 0; row < rows; ++row) {
            unsigned int col;
            for (col = 0; col < cols; ++col)
                grayrow[col] = (gray)fgetc(ifP);
            if (cols % 2)
                fgetc (ifP); /* padding */
            pgm_writepgmrow(stdout, grayrow, cols, maxval, 0);
        }
        pgm_freerow(grayrow);
    } else if (depth == 24) {
        /* true color */
        pixel * pixrow;
        unsigned int row;

        ppm_writeppminit(stdout, cols, rows, maxval, 0);

        pixrow = ppm_allocrow(cols);

        for (row = 0; row < rows; ++row) {
            unsigned int col;

            for (col = 0; col < cols; ++col)
                PPM_PUTR(pixrow[col], fgetc(ifP));

            if (cols % 2 != 0)
                fgetc (ifP); /* padding */

            for (col = 0; col < cols; ++col)
                PPM_PUTG(pixrow[col], fgetc(ifP));

            if (cols % 2)
                fgetc (ifP); /* padding */

            for (col = 0; col < cols; ++col)
                PPM_PUTB(pixrow[col], fgetc(ifP));

            if (cols % 2 != 0)
                fgetc (ifP); /* padding */

            ppm_writeppmrow(stdout, pixrow, cols, maxval, 0);
        }
        ppm_freerow(pixrow);
    } else if (depth == 8) {
        /* 8-bit (color mapped) image */
        pixel * pixrow;
        unsigned int row;

        ppm_writeppminit(stdout, cols, rows, (pixval) maxval, 0);

        pixrow = ppm_allocrow(cols);

        for (row = 0; row < rows; ++row) {
            unsigned int col;

            for (col = 0; col < cols; ++col)
                pixrow[col] = colormap[fgetc(ifP)];

            if (cols % 2 != 0)
                fgetc(ifP); /* padding */

            ppm_writeppmrow(stdout, pixrow, cols, maxval, 0);
        }
        ppm_freerow(pixrow);
    } else if (depth == 1) {
        /* mono image */
        bit *bitrow;
        unsigned int row;

        pbm_writepbminit(stdout, cols, rows, 0);

        bitrow = pbm_allocrow(cols);

        for (row = 0; row < rows; ++row) {
            unsigned int col;
            unsigned char bits;

            for (col = 0, bits = 0x00; col < cols; ++col) {
                int const shift = col % 8;
                if (shift == 0)
                    bits = (unsigned char) fgetc(ifP);
                bitrow[col] = (bits & (unsigned char)(0x01 << (7 - shift))) ?
                    PBM_WHITE : PBM_BLACK;
            }
            if (cols % 16 != 0 && cols % 16 <= 8)
                fgetc(ifP);  /* 16 bit pad */

            pbm_writepbmrow(stdout, bitrow, cols, 0);
        }
        pbm_freerow(bitrow);
    }
    pm_close(ifP);

    return 0;
}



