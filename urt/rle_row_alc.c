/*
 * This software is copyrighted as noted below.  It may be freely copied,
 * modified, and redistributed, provided that the copyright notice is
 * preserved on all copies.
 *
 * There is no warranty or other guarantee of fitness for this software,
 * it is provided solely "as is".  Bug reports or fixes may be sent
 * to the author, who may or may not act on them as he desires.
 *
 * You may not include this software in a program or other software product
 * without supplying the source, or without informing the end-user that the
 * source is available for no extra charge.
 *
 * If you modify this software, you should include a notice giving the
 * name of the person performing the modification, the date of modification,
 * and the reason for such modification.
 *
 *  Modified at BRL 16-May-88 by Mike Muuss to avoid Alliant STDC desire
 *  to have all "void" functions so declared.
 */
/*
 * rle_row_alc.c - Allocate buffers for rle_getrow/rle_putrow.
 *
 * Author:      Spencer W. Thomas
 *              Computer Science Dept.
 *              University of Utah
 * Date:        Fri Nov 14 1986
 * Copyright (c) 1986, Spencer W. Thomas
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "mallocvar.h"

#include "rle.h"




static unsigned int
numChannels(const rle_hdr * const the_hdr) {

    unsigned int nchan;
    unsigned int i;

    nchan = 0; /* initial value */

    if (the_hdr->alpha && RLE_BIT(*the_hdr, RLE_ALPHA))
        ++nchan;

    for (i = 0; i < the_hdr->ncolors; ++i) {
        if (RLE_BIT(*the_hdr, i))
            ++nchan;
    }

    return nchan;
}



int
rle_row_alloc(rle_hdr *     const the_hdr,
              rle_pixel *** const scanP) {
/*----------------------------------------------------------------------------
  Allocate buffer space for use by rle_getrow and rle_putrow.

  Inputs:
       the_hdr:        Header structure for RLE file to be read or
                       written.
  Outputs:
       *scanP:         Pointer to first color channel in the created scanline
                       buffer.

                       Note that if the image has an alpha channel, this is
                       the _second_ element of the array.
                       if present.

       Returns 0 for success, -1 if malloc failed.

  Assumptions:
       No input scanline will extend beyond the declared xmax endpoint.

-----------------------------------------------------------------------------*/
    /* Algorithm:

       Count number of channels actually used (check bitmap).
       Allocate nchan*rowlength pixels, allocate a buffer
       to hold ncolors+alpha pointers, and give each channel
       rowlength pixels.  Rowlength is xmax + 1, to allow for rle_getrow
       usage.
    */

    unsigned int const nchan  = numChannels(the_hdr);
    unsigned int const ncol   = the_hdr->ncolors + the_hdr->alpha;
    unsigned int const rowlen = the_hdr->xmax + 1;

    int          retval;
    rle_pixel ** scanbuf;
    rle_pixel *  pixbuf;

    MALLOCARRAY(scanbuf, ncol);

    if (!scanbuf)
        retval = -1;
    else {
        MALLOCARRAY(pixbuf, nchan * rowlen);

        if (!pixbuf)
            retval = -1;
        else {
            rle_pixel ** colorScanbuf;
            int i;

            retval = 0;

            assert(the_hdr->alpha == 0 || the_hdr->alpha == 1);

            colorScanbuf =  the_hdr->alpha ? &scanbuf[1] : &scanbuf[0];

            for (i = -the_hdr->alpha; i < the_hdr->ncolors; ++i) {
                if (RLE_BIT(*the_hdr, i)) {
                    colorScanbuf[i] = pixbuf;
                    pixbuf += rowlen;
                } else
                    colorScanbuf[i] = 0;
            }
            *scanP = colorScanbuf;

            if (retval != 0)
                free(scanbuf);
        }
    }
    return retval;
}



void
rle_row_free(rle_hdr *    const the_hdr,
             rle_pixel ** const scanP) {
/*----------------------------------------------------------------------------
  Free storage allocated by rle_row_alloc().

  Inputs:
       the_hdr:        Header structure as above.
       scanP:          Pointer to first color channel in scanbuf.
----------------------------------------------------------------------------*/
    unsigned int i;

    assert(the_hdr->alpha == 0 || the_hdr->alpha == 1);

    rle_pixel ** const scanbuf = scanP - the_hdr->alpha;

    for (i = 0; i < the_hdr->ncolors + the_hdr->alpha; i++) {
        if (scanbuf[i]) {
            free(scanbuf[i]);
            break;
        }
    }
    free(scanbuf);
}



