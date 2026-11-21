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
 * rle_getrow.c - Read an RLE file in.
 *
 * Author:  Spencer W. Thomas
 *      Computer Science Dept.
 *      University of Utah
 * Date:    Wed Apr 10 1985
 * Copyright (c) 1985 Spencer W. Thomas
 *
 * $Id: rle_getrow.c,v 3.0.1.5 1992/03/04 19:33:08 spencer Exp spencer $
 */

#include <string.h>
#include <stdio.h>

#include "netpbm/pm.h"
#include "netpbm/mallocvar.h"

#include "rle.h"
#include "rle_code.h"
#include "vaxshort.h"

/* Read a two-byte "short" that started in VAX (LITTLE_ENDIAN) order */
#define VAXSHORT( var, fp )\
    { var = fgetc(fp)&0xFF; var |= (fgetc(fp)) << 8; }

/* Instruction format -- first byte is opcode, second is datum. */

#define OPCODE(inst) (inst[0] & ~LONG)
#define LONGP(inst) (inst[0] & LONG)
#define DATUM(inst) (inst[1] & 0xff)    /* Make sure it's unsigned. */

static int     debug_f;     /* If non-zero, print debug info. */



static void
readComments(rle_hdr * const hdrP) {

    FILE * ifP = hdrP->rle_file;

    /* There are comments */
    short comlen;
    char * cp;

    VAXSHORT(comlen, ifP); /* get comment length */

    if (comlen < 0)
        pm_error("Negative comment length in RLE header");
    else if (comlen > 0) {
        unsigned int const evenlen = (comlen + 1) & ~1;    /* make it even */

        char * commentHeap;
        unsigned int i;

        MALLOCARRAY(commentHeap, evenlen);

        if (commentHeap == NULL) {
            pm_error("Malloc failed for comment buffer of size %u "
                     "in rle_get_setup, reading '%s'",
                     evenlen, hdrP->file_name );
        }
        fread(commentHeap, 1, evenlen, ifP);
        /* Count the comments */
        for (i = 0, cp = commentHeap; cp < commentHeap + comlen; ++cp) {
            if (*cp == '\0')
                ++i;
        }
        ++i;            /* extra for NULL pointer at end */
        /* Get space to put pointers to comments */
        MALLOCARRAY(hdrP->comments, i);
        if (hdrP->comments == NULL) {
            pm_error("Malloc failed for %d comment pointers "
                     "in rle_get_setup, reading '%s'",
                     i, hdrP->file_name );
        }
        /* Set comment heap */
        hdrP->comments[0] = commentHeap;

        /* Set pointers to individual comments in the heap as
          hdrP->comments[1], hdrP->comments[2], etc.
        */
        for (i = 1, cp = commentHeap + 1;
             cp < commentHeap + comlen;
             ++cp)
            if (*(cp - 1) == '\0')
                hdrP->comments[i++] = cp;
        hdrP->comments[i] = NULL;
    } else
        hdrP->comments = NULL;
}



int
rle_get_setup(rle_hdr * const hdrP) {
/*-----------------------------------------------------------------------------
  Read the initialization information from an RLE file.
  Inputs:
    hdrP:    Contains pointer to the input file.
  Outputs:
    hdrP:    Initialized with information from the input file.
  Returns
     0  on success,
     -1 if the file is not an RLE file,
     -2 if malloc of the color map failed,
     -3 if an immediate EOF is hit (empty input file)
     -4 if an EOF is encountered reading the setup information.
  Assumptions:
    input file is positioned to the "magic" number in an RLE file (usually
    first byte of the file).
  Algorithm:
    Read in the setup info and fill in *hdrP.
---------------------------------------------------------------------------- */
    struct XtndRsetup setup;
    short magic;
    FILE * ifP = hdrP->rle_file;
    int i;

    /* Clear old stuff out of the header. */
    rle_hdr_clear(hdrP);
    if (hdrP->is_init != RLE_INIT_MAGIC)
        rle_names(hdrP, "Urt", "some file", 0);
    ++hdrP->img_num;     /* Count images. */

    VAXSHORT(magic, ifP);
    if (feof(ifP))
        return RLE_EMPTY;
    if (magic != RLE_MAGIC)
        return RLE_NOT_RLE;
    fread(&setup, 1, SETUPSIZE, ifP);  /* assume VAX packing */
    if (feof( ifP))
        return RLE_EOF;

    /* Extract information from setup */
    hdrP->ncolors = setup.h_ncolors;
    for (i = 0; i < hdrP->ncolors; ++i)
        RLE_SET_BIT(*hdrP, i);

    if (!(setup.h_flags & H_NO_BACKGROUND) && setup.h_ncolors > 0) {
        rle_pixel * bg_color;

        MALLOCARRAY(hdrP->bg_color, setup.h_ncolors);
        if (!hdrP->bg_color)
            pm_error("Failed to allocation array for %u background colors",
                     setup.h_ncolors);
        MALLOCARRAY(bg_color, 1 + (setup.h_ncolors / 2) * 2);
        if (!bg_color)
            pm_error("Failed to allocation array for %u background colors",
                     1+(setup.h_ncolors / 2) * 2);
        fread((char *)bg_color, 1, 1 + (setup.h_ncolors / 2) * 2, ifP);
        for (i = 0; i < setup.h_ncolors; ++i)
            hdrP->bg_color[i] = bg_color[i];
        free(bg_color);
    } else {
        getc(ifP);   /* skip filler byte */
        hdrP->bg_color = NULL;
    }

    if (setup.h_flags & H_NO_BACKGROUND)
        hdrP->background = 0;
    else if (setup.h_flags & H_CLEARFIRST)
        hdrP->background = 2;
    else
        hdrP->background = 1;
    if (setup.h_flags & H_ALPHA) {
        hdrP->alpha = 1;
        RLE_SET_BIT( *hdrP, RLE_ALPHA );
    } else
        hdrP->alpha = 0;

    hdrP->xmin = vax_gshort(setup.hc_xpos);
    hdrP->ymin = vax_gshort(setup.hc_ypos);
    hdrP->xmax = hdrP->xmin + vax_gshort(setup.hc_xlen) - 1;
    hdrP->ymax = hdrP->ymin + vax_gshort(setup.hc_ylen) - 1;

    hdrP->ncmap = setup.h_ncmap;
    hdrP->cmaplen = setup.h_cmaplen;

    if (hdrP->ncmap > 0) {
        int const maplen = hdrP->ncmap * (1 << hdrP->cmaplen);

        unsigned int i;
        unsigned char *maptemp;

        MALLOCARRAY(hdrP->cmap, maplen);
        MALLOCARRAY(maptemp, 2 * maplen);
        if (hdrP->cmap == NULL || maptemp == NULL) {
            pm_error("Malloc failed for color map of size %d*%d "
                     "in rle_get_setup, reading '%s'",
                     hdrP->ncmap, (1 << hdrP->cmaplen),
                     hdrP->file_name );
            return RLE_NO_SPACE;
        }
        fread(maptemp, 2, maplen, ifP);
        for (i = 0; i < maplen; ++i)
            hdrP->cmap[i] = vax_gshort(&maptemp[i * 2]);
        free(maptemp);
    }

    if (setup.h_flags & H_COMMENT)
        readComments(hdrP);
    else
        hdrP->comments = NULL;

    /* Initialize state for rle_getrow */
    hdrP->priv.get.scan_y = hdrP->ymin;
    hdrP->priv.get.vert_skip = 0;
    hdrP->priv.get.is_eof = 0;
    hdrP->priv.get.is_seek = ftell(ifP) > 0;
    debug_f = 0;

    if (!feof(ifP))
        return RLE_SUCCESS; /* success! */
    else {
        hdrP->priv.get.is_eof = 1;
        return RLE_EOF;
    }
}



static void
doSkipLines(rle_hdr *    const hdrP,
            const char * const inst,
            FILE *       const ifP) {

    if (LONGP(inst)) {
        VAXSHORT(hdrP->priv.get.vert_skip, ifP);
    } else
        hdrP->priv.get.vert_skip = DATUM(inst);

    if (debug_f)
        pm_message("Skip %d Lines (to %d)",
                   hdrP->priv.get.vert_skip,
                   hdrP->priv.get.scan_y +
                   hdrP->priv.get.vert_skip);
}



static void
doSetColor(rle_hdr *    const hdrP,
           const char * const inst,
           rle_pixel ** const scanline,
           int *        const channelP,
           int *        const scanXP,
           rle_pixel ** const scancP) {

    int channel;
    int scanX;

    channel = DATUM(inst);  /* select color channel */
    if (channel == 255)
        channel = -1;

    scanX = hdrP->xmin;

    if (RLE_BIT(*hdrP, channel))
        *scancP = scanline[channel] + scanX;

    if (debug_f)
        pm_message("Set color to %d (reset x to %d)", channel, scanX);

    *channelP = channel;
    *scanXP   = scanX;
}



static void
doSkipPixels(rle_hdr *    const hdrP,
             const char * const inst,
             FILE *       const ifP,
             int *        const scanXP,
             rle_pixel ** const scancP) {

    short longData;

    if (LONGP(inst)) {
        VAXSHORT(longData, ifP);
        *scanXP += longData;
        *scancP += longData;
        if (debug_f)
            pm_message("Skip %d pixels (to %d)", longData, *scanXP);
    } else {
        *scanXP += DATUM(inst);
        *scancP += DATUM(inst);
        if (debug_f)
            pm_message("Skip %d pixels (to %d)", DATUM(inst), *scanXP);
    }
}



static void
doByteData(rle_hdr *    const hdrP,
           const char * const inst,
           FILE *       const ifP,
           int          const channel,
           int *        const scanXP,
           rle_pixel ** const scancP) {
/*----------------------------------------------------------------------------
   Execute a Byte Data instruction from the RLE image.  Put the data at
   *scancP and update *scancP to point past it.

   *scanXP at entry is the column number corresponding to the first of the
   pixels of this instruction, and we update it to the column just after the
   pixels.

   File *ifP is positioned after the two-byte instruction header.

   We don't put pixels that are off the right edge of the image at *scancP.
   We just discard them.
-----------------------------------------------------------------------------*/
    int          scanX;
    rle_pixel *  scanc;
    int          arg;
    int          nc;
        /* Instruction argument: Number of bytes of data */

    scanX = *scanXP;  /* initial value */
    scanc = *scancP;  /* initial value */

    if (LONGP(inst)) {
        VAXSHORT(arg, ifP);
    } else
        arg = DATUM(inst);

    nc = arg + 1;

    if (RLE_BIT(*hdrP, channel)) {
        unsigned int bytesToFillCt;
            /* Number of bytes to fill (taking into account limits of the
               scanline)
            */
        unsigned int  bytesToSkipCt;
            /* Number of bytes of data to skip (because they are beyond the
               limits of the scanline)
            */
        if (debug_f)
            pm_message("Read pixel data %d bytes (to %d):", nc, scanX + nc);
        /* Don't fill past end of scanline! */
        if (scanX + nc > hdrP->xmax) {
            bytesToSkipCt = scanX + nc - hdrP->xmax - 1;
            bytesToFillCt = nc - bytesToSkipCt;
        } else {
            bytesToSkipCt = 0;
            bytesToFillCt = nc;
        }
        fread((char *)scanc, 1, bytesToFillCt, ifP);
        {
            unsigned int i;
            for (i = 0; i < bytesToSkipCt; ++i)
                getc(ifP);
        }
        if (bytesToFillCt & 0x1)
            getc(ifP);   /* throw away odd byte */

        if (debug_f) {
            unsigned int i;
            for (i = 0; i < bytesToFillCt; ++i)
                fprintf(stderr, "%02x", scanc[i]);
            putc('\n', stderr);
        }
        scanc += bytesToFillCt;
        scanX += bytesToFillCt;
   } else {
        if (debug_f)
            pm_message("Discard pixel data %d bytes (to %d):", nc, scanX + nc);
        if (hdrP->priv.get.is_seek)
            fseek(ifP, ((nc + 1) / 2) * 2, 1);
        else {
            unsigned int i;
            for (i = ((nc + 1) / 2) * 2; i > 0; --i)
                getc(ifP);  /* discard it */
        }
        scanc += nc;
        scanX += nc;
    }

    *scanXP = scanX;
    *scancP = scanc;
}



static void
doRunDataOp(rle_hdr *    const hdrP,
            const char * const inst,
            FILE *       const ifP,
            int          const channel,
            int *        const scanXP,
            rle_pixel ** const scancP) {
/*----------------------------------------------------------------------------
   Execute a Run Data instruction from the RLE image.  Put the data at
   *scancP and update *scancP to point past it.

   *scanXP at entry is the column number corresponding to the first of the
   pixels, and we update it to the column just after the last of the pixels.

   File *ifP is positioned after the two-byte instruction header.

   We don't put pixels that are off the right edge of the image at *scancP.
   We just discard them.
-----------------------------------------------------------------------------*/
    int          scanX;
    rle_pixel *  scanc;
    short        word;
        /* Instructin argument: The byte to replicate */
    int          arg;
    int          nc;
        /* Instruction argument: Number of bytes of data */

    scanX = *scanXP;  /* initial value */
    scanc = *scancP;  /* initial value */

    if (LONGP(inst)) {
        VAXSHORT(arg, ifP);
    } else
        arg = DATUM(inst);
    nc = arg + 1;

    scanX += nc;

    VAXSHORT(word, ifP);

    if (debug_f)
        pm_message("Run length %d (to %d), data %02x", nc, scanX, word);

    if (RLE_BIT(*hdrP, channel)) {
        unsigned int bytesToFillCt;
            /* Number of bytes to fill (taking into account limits of the
               scanline)
            */
        unsigned int  bytesToSkipCt;
            /* Number of bytes of data to skip (because they are beyond the
               limits of the scanline)
            */
        if (scanX > hdrP->xmax) {
            bytesToSkipCt = scanX - hdrP->xmax - 1;
            bytesToFillCt = nc - bytesToSkipCt;
        } else {
            bytesToSkipCt = 0;
            bytesToFillCt = nc;
        }
        /* We use a loop instead of memset if doing fewer than 10 repetitions
           because it is faster, at least on a Vax 11/785.
        */
        if (bytesToFillCt >= 10) {
            memset((char *)scanc, word, bytesToFillCt);
        } else {
            unsigned int i;
            for (i = 0; i < bytesToFillCt; ++i)
                scanc[i] = word;
        }
        scanc += bytesToFillCt;
    }
    *scanXP = scanX;
    *scancP = scanc;
}



static void
doEOF(rle_hdr * const hdrP) {

    hdrP->priv.get.is_eof = 1;

    if (debug_f)
        pm_message("End of Image");
}



int
rle_getrow(rle_hdr *    const hdrP,
           rle_pixel ** const scanline) {
/*-----------------------------------------------------------------------------
  Get a scanline from the input file.
  Inputs:
   hdrP:    Header structure containing information about
           the input file.
  Outputs:
   scanline:   an array of pointers to the individual color
           scanlines.  Scanline is assumed to have
           hdrP->ncolors pointers to arrays of rle_pixel,
           each of which is at least hdrP->xmax+1 long.
   Returns the current scanline number.
  Assumptions:
   rle_get_setup has already been called.
  Algorithm:
   If a vertical skip is being executed, and clear-to-background is
   specified (hdrP->background is true), just set the
   scanlines to the background color.  If clear-to-background is
   not set, just increment the scanline number and return.

   Otherwise, read input until a vertical skip is encountered,
   decoding the instructions into scanline data.

   If ymax is reached (or, somehow, passed), continue reading and
   discarding input until end of image.
---------------------------------------------------------------------------- */
    FILE * const ifP = hdrP->rle_file;

    rle_pixel * scanc;

    int scanX; /* current X position */
    int channel;         /* current color channel */
    int nc;
    char inst[2];

    scanX = hdrP->xmin; /* initial value */
    channel = 0; /* initial value */
    /* Clear to background if specified */
    if (hdrP->background != 1) {
        if (hdrP->alpha && RLE_BIT( *hdrP, -1))
            memset((char *)scanline[-1] + hdrP->xmin, 0,
                   hdrP->xmax - hdrP->xmin + 1);
        for (nc = 0; nc < hdrP->ncolors; ++nc) {
            if (RLE_BIT( *hdrP, nc)) {
                /* Unless bg color given explicitly, use 0. */
                if (hdrP->background != 2 || hdrP->bg_color[nc] == 0)
                    memset((char *)scanline[nc] + hdrP->xmin, 0,
                           hdrP->xmax - hdrP->xmin + 1);
                else
                    memset((char *)scanline[nc] + hdrP->xmin,
                           hdrP->bg_color[nc],
                           hdrP->xmax - hdrP->xmin + 1);
            }
        }
    }

    /* If skipping, then just return */
    if (hdrP->priv.get.vert_skip > 0) {
        --hdrP->priv.get.vert_skip;
        ++hdrP->priv.get.scan_y;
        if (hdrP->priv.get.vert_skip > 0) {
            if (hdrP->priv.get.scan_y >= hdrP->ymax) {
                int const y = hdrP->priv.get.scan_y;
                while (rle_getskip(hdrP) != 32768)
                    ;
                return y;
            } else
                return hdrP->priv.get.scan_y;
        }
    }

    /* If EOF has been encountered, return also */
    if (hdrP->priv.get.is_eof)
        return ++hdrP->priv.get.scan_y;

    /* Otherwise, read and interpret instructions until a skipLines
       instruction is encountered.
    */
    if (RLE_BIT(*hdrP, channel))
        scanc = scanline[channel] + scanX;
    else
        scanc = NULL;
    for (;;) {
        inst[0] = getc(ifP);
        inst[1] = getc(ifP);
        if (feof(ifP)) {
            hdrP->priv.get.is_eof = 1;
            break;      /* <--- one of the exits */
        }

        switch(OPCODE(inst)) {
        case RSkipLinesOp:
            doSkipLines(hdrP, inst, ifP);
            break;

        case RSetColorOp:
            doSetColor(hdrP, inst, scanline, &channel, &scanX, &scanc);
            break;

        case RSkipPixelsOp:
            doSkipPixels(hdrP, inst, ifP, &scanX, &scanc);
            break;

        case RByteDataOp:
            doByteData(hdrP, inst, ifP, channel, &scanX, &scanc);
            break;

        case RRunDataOp:
            doRunDataOp(hdrP, inst, ifP, channel, &scanX, &scanc);
            break;

        case REOFOp:
            doEOF(hdrP);
            break;

        default:
            pm_error("rle_getrow: Unrecognized opcode: %d, reading %s",
                     inst[0], hdrP->file_name);
        }
        if (OPCODE(inst) == RSkipLinesOp || OPCODE(inst) == REOFOp)
            break;          /* <--- the other loop exit */
    }

    /* If at end, skip the rest of a malformed image. */
    if (hdrP->priv.get.scan_y >= hdrP->ymax) {
        int const y = hdrP->priv.get.scan_y;
        while (rle_getskip(hdrP) != 32768 )
            ;
        return y;
    }

    return hdrP->priv.get.scan_y;
}


