/* fstopgm.c - read a Usenix FaceSaver(tm) file and produce a portable graymap
**
** Copyright (C) 1989 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <string.h>

#include "pm.h"
#include "pgm.h"



static int
gethexit(FILE * const ifP) {

    for ( ; ; ) {
        unsigned int const i = getc(ifP);

        if (i == EOF)
            pm_error("EOF / read error");
        else {
            char const c = (char) i;
            if (c >= '0' && c <= '9')
                return c - '0';
            else if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            else if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            else {
                /* Ignore - whitespace. */
            }
        }
    }
}



static void
warnNonsquarePixels(unsigned int const cols,
                    unsigned int const xcols,
                    unsigned int const rows,
                    unsigned int const xrows) {

    const char * const baseMsg = "warning, non-square pixels";

    if (pm_have_float_format()) {
        float const rowratio = (float) xrows / (float) rows;
        float const colratio = (float) xcols / (float) cols;

        pm_message("%s; to fix do a 'pamscale -%cscale %g'",
                   baseMsg,
                   rowratio > colratio ? 'y' : 'x',
                   rowratio > colratio ?
                   rowratio / colratio : colratio / rowratio);
    } else
        pm_message("%s", baseMsg);
}



int
main(int argc, const char ** argv) {

    FILE * ifP;
    gray ** grays;
    int argn;
    int row;
    gray maxval;
    int rows, cols, depth;
    int xrows, xcols, xdepth;
#define STRSIZE 1000

    pm_proginit(&argc, argv);

    rows = 0;
    cols = 0;
    depth = 0;

    xrows = 0;
    xcols = 0;
    xdepth = 0;

    argn = 1;

    if (argn < argc) {
        ifP = pm_openr(argv[argn]);
        argn++;
    } else
        ifP = stdin;

    if (argn != argc)
        pm_error("Too many arguments.  The only argument is the file name");

    /* Read the FaceSaver(tm) header. */
    for ( ; ; ) {
        char buf[STRSIZE];
        char firstname[STRSIZE];
        char lastname[STRSIZE];
        char email[STRSIZE];

        char * const rc = fgets(buf, STRSIZE, ifP);

        if (rc  == NULL)
            pm_error("error reading header");

        /* Blank line ends header. */
        if (strlen(buf) == 1)
            break;

        if (sscanf(buf, "FirstName: %[^\n]", firstname) == 1);
        else if (sscanf(buf, "LastName: %[^\n]", lastname) == 1);
        else if (sscanf(buf, "E-mail: %[^\n]", email ) == 1);
        else if (sscanf(buf, "PicData: %d %d %d\n",
                        &cols, &rows, &depth ) == 3) {
            if (depth != 8)
                pm_error("can't handle 'PicData' depth other than 8");
        } else if (sscanf(buf, "Image: %d %d %d\n",
                          &xcols, &xrows, &xdepth ) == 3) {
            if (xdepth != 8)
                pm_error("can't handle 'Image' depth other than 8");
        }
    }
    if (cols <= 0 || rows <= 0)
        pm_error("invalid header");
    maxval = pm_bitstomaxval(depth);
    if (maxval > PGM_OVERALLMAXVAL)
        pm_error("depth %d is too large.  Our maximum is %d",
                 maxval, PGM_OVERALLMAXVAL);
    if (xcols != 0 && xrows != 0 && (xcols != cols || xrows != rows))
        warnNonsquarePixels(cols, xcols, rows, xrows);

    /* Read the hex bits. */
    grays = pgm_allocarray(cols, rows);
    for (row = rows - 1; row >= 0; --row) {
        unsigned int col;
        for (col = 0; col < cols; ++col) {
            grays[row][col] =  gethexit(ifP) << 4;
            grays[row][col] += gethexit(ifP);
        }
    }
    pm_close(ifP);

    pgm_writepgm(stdout, grays, cols, rows, maxval, 0);
    pm_close(stdout);

    return 0;
}



