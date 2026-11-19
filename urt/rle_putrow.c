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
 * rle_putrow.c - Save a row of the fb to a file.
 *
 * Author:      Spencer W. Thomas
 *              Computer Science Dept.
 *              University of Utah
 * Date:        1 April 1981
 * Copyright (c) 1981,1986 Spencer W. Thomas
 *
 * $Id: rle_putrow.c,v 3.0.1.2 1992/01/28 18:29:22 spencer Exp $
 */

#include <stdio.h>

#include <pm.h>

#include "rle_put.h"
#include "rle.h"


#define FALSE   0
#define TRUE    1


static int
findruns(rle_pixel * const row,
         int         const rowlen,
         int         const color,
         int         const nrunAlready,
         short    (* const brun)[2]) {
/*----------------------------------------------------------------------------
  Find runs not a given color in the row.

  Inputs:
       row:            Row of pixel values
       rowlen:         Number of pixels in the row.
       color:          Color to compare against.
       nrun:           Number of runs already found (in different colors).
       brun:           Runs found in other color channels already.
  Outputs:
       brun:           Modified to reflect merging of runs in this color.
       Returns number of runs in brun.
-----------------------------------------------------------------------------*/

    /* Algorithm:

       Search for occurrences of pixels not of the given color outside the
       runs already found.  When some are found, add a new run or extend an
       existing one.  Adjacent runs with fewer than two pixels intervening are
       merged.
    */
    int i;
    int nrun;

    for (i = 0, nrun = nrunAlready; i <= nrun; ) {
        int lower, upper;
        int s;
        /* Assert: 0 <= i <= rowlen
           brun[i] is the run following the "blank" space being
           searched.  If i == rowlen, search after brun[i-1].
        */

        /* get lower and upper bounds of search */

        if (i == 0)
            lower = 0;
        else
            lower = brun[i-1][1] + 1;

        if (i == nrun)
            upper = rowlen - 1;
        else
            upper = brun[i][0] - 1;

        /* Search for beginning of run != color */

        for (s = lower; s <= upper && row[s] == color; ++s);

        if (s <= upper) {
            /* found a new run */
            if (s > lower + 1 || i == 0) {
                int j;
                /* disjoint from preceding run */
                /* Shift following runs up */
                for (j = nrun; j > i; --j) {
                    brun[j][0] = brun[j-1][0];
                    brun[j][1] = brun[j-1][1];
                }
                brun[i][0] = s;
                ++nrun;
            } else
                --i;            /* just add to preceding run */

            for ( ; s <= upper && row[s] != color; ++s);

            brun[i][1] = s - 1;

            if (s >= upper && i < nrun - 1) {
                int j;
                 /* merge with following run */
                brun[i][1] = brun[i+1][1];
                /* move following runs back down */
                for (j = i + 2; j < nrun; ++j) {
                    brun[j-1][0] = brun[j][0];
                    brun[j-1][1] = brun[j][1];
                }
                --nrun;
            }
        }

        /* Search in next space */
        ++i;
    }

    return nrun;
}



static void
putRowOneColor(rle_hdr *   const the_hdr,
               short (*    const pbrun)[2],
               rle_pixel * const row,
               int         const rowlen,
               int         const nrun,
               short *     const rstartP,
               short *     const runvalP) {

    short rstart;
    short runval;
    int j;

    rstart = *rstartP;
    runval = *runvalP;

    if (the_hdr->priv.put.brun[0][0] > 0)
        SkipPixels(pbrun[0][0], FALSE, FALSE);

    for (j = 0; j < nrun; ++j) {
        short const dstart0 = pbrun[j][0];
        /* Starting point for current data segment. */
        short const dend = pbrun[j][1];
        /* Ending point of current data segment. */

        short state;     /* State of run-finding state machine. */
        short dstart;
        int i;

        for (dstart = dstart0, i = dstart, state = DATA;
             i <= dend;
             ++i) {

            switch(state) {
            case DATA:
                if (i > dstart && runval == row[i]) {
                    /* 2 in a row may be a run. */
                    /* If odd data length, start with RUN1 */
                    if (((i - dstart) % 2) == 0)
                        state = RUN1;
                    else
                        state = RUN2;
                } else {
                    runval = row[i];    /* maybe a run starts here? */
                    rstart = i;
                }
                break;

            case RUN4:
                if (runval == row[i]) {
                    /* If the following data might be longer
                       than 255 pixels then look for 8 in a
                       row, otherwise, 6 in a row is
                       sufficient.  Fake this by skipping to
                       state RUN5.
                    */
                    if (dend - i > 255)
                        state  = RUN5;  /* Need some more. */
                    else
                        state = RUN7;   /* Next one makes a run. */
                } else {
                    state = DATA;       /* Nope, back to data */
                    runval = row[i];    /* but maybe a new run here? */
                    rstart = i;
                }
                break;

            case RUN1:
            case RUN2:
            case RUN3:
            case RUN5:
            case RUN6:
                if (runval == row[i]) {
                    ++state;
                } else {
                    state = DATA;       /* Nope, back to data */
                    runval = row[i];    /* but maybe a new run here? */
                    rstart = i;
                }
                break;

            case RUN7:
                if (runval == row[i]) {
                    /* enough in a row for a run */
                    state = INRUN;
                    putdata(row + dstart, rstart - dstart);
                    while (row[++i] == runval && i <= dend);
                    --i;
                } else {
                    state = DATA;               /* not a run, */
                    runval = row[i];    /* but may this starts one */
                    rstart = i;
                }
                break;

            case INRUN:
                if (runval != row[i]) {
                    /* run out */
                    state = DATA;
                    putrun(runval, i - rstart, FALSE);
                    runval = row[i];    /* who knows, might be more */
                    rstart = i;
                    dstart = i; /* starting a new 'data' run */
                }
                break;
            }
        }
        if (state == INRUN)
            putrun(runval, i - rstart, TRUE);   /* last bit */
        else
            putdata(row + dstart, i - dstart);

        if (j < nrun - 1)
            SkipPixels(pbrun[j+1][0] - dend - 1, FALSE, state == INRUN);
        else {
            if (rowlen - dend > 0)
                SkipPixels(rowlen - dend - 1, TRUE, state == INRUN);
        }
    }
    *rstartP = rstart;
    *runvalP = runval;
}



void
rle_putrow(rle_pixel ** const rows,
           int          const rowlen,
           rle_hdr *    const the_hdr) {
/*----------------------------------------------------------------------------
  Write a scanline to the output file.

  Inputs:
       rows:           Array of pointers to rle_pixel arrays containing the
                       pixel information.  If NULL, we skip rowlen scanlines.

                       N.B. this is actually a pointer to the element in the
                       array that contains the first color channel, and
                       if there is an alpha channel, that is the element
                       BEFORE that.  It is designed to be accessed with a
                       negative array index to access the alpha channel!
                       We need to fix this some day.

       rowlen:         The number of pixels in the scanline, or the
                       number of scanlines to skip (see above).
  Outputs:
       Run length encoded information is written to the_hdr.rle_file.
-----------------------------------------------------------------------------*/

    /* Algorithm:

       There are two parts:

               1. Find all "sufficiently long" runs of background
                  color.  These will not be saved at all.

               2. For each run of non-background, for each color
                  channel, find runs of identical pixel values
                  between "data" segments (of differing pixel
                  values).

       For part 1, "sufficiently long" is 2 pixels, if the following
       data is less than 256 pixels long, otherwise it is 4 pixels.
       This is enforced by a post-process merge.

       Part 1 finds runs of the background pixel value in each channel
       independently, and then merges the results.

       Part 2 uses a state machine.  For each run of non-background
       data, it searches for sufficiently long sequences of a single
       value (in each channel independently).  Sufficiently long is 4
       pixels if the following data is < 256 pixels, 6 pixels
       otherwise.  This is because the startup cost for the run is 2
       bytes, and the startup cost for a data segment is 2 bytes if
       it is < 256 pixels long, 4 bytes otherwise.  Thus a run
       shorter than 4 or 6 pixels (respectively) would actually make
       the output longer.  An additional pixel is required if the
       preceding data is an odd number of pixels long (because a
       filler byte will be output at the end of it.)
    */

    char bits[256];
    int nrun;

    if (!rows) {
        the_hdr->priv.put.nblank += rowlen;
        return;
    }
    /* If not done already, allocate space to remember runs of
       non-background color.  A run of bg color must be at least 2
       bytes long to count, so there can be at most rowlen/3 of them.
    */
    if (!the_hdr->priv.put.brun) {
        the_hdr->priv.put.brun = (short (*)[2])malloc(
            (unsigned)((rowlen/3 + 1) * 2 * sizeof(short)) );
        if (!the_hdr->priv.put.brun) {
            pm_error("%s: Malloc failed in rle_putrow, writing '%s'",
                     the_hdr->cmd, the_hdr->file_name);
        }
    }
    {
        int i;
        /* Unpack bitmask in the_hdr struct */
        for (i = 0; i < the_hdr->ncolors; ++i)
            bits[i] = RLE_BIT(*the_hdr, i);
    }
    bits[255] = RLE_BIT(*the_hdr, 255);

    /* If saving only non-background pixels, find runs of them.  Note
       that the alpha channel is considered to be background iff it is
       zero.
    */
    if (the_hdr->background) {
        int i;
        /* Find runs in each color individually, merging them as we go. */
        nrun = 0;               /* start out with no runs */
        /* Alpha channel first */
        if (the_hdr->alpha)
            nrun = findruns(rows[-1], rowlen, 0, nrun, the_hdr->priv.put.brun);
        /* Now the color channels */
        for (i = 0; i < the_hdr->ncolors; ++i) {
            if (bits[i])
                nrun = findruns(rows[i], rowlen, the_hdr->bg_color[i],
                                nrun, the_hdr->priv.put.brun);
        }
    } else {
        the_hdr->priv.put.brun[0][0] = 0;
        the_hdr->priv.put.brun[0][1] = rowlen - 1;
        nrun = 1;
    }
    /* One final pass merges runs with fewer than 4 intervening pixels
       if the second run is longer than 255 pixels.  This is because
       the startup cost for such a run is 4 bytes.
    */
    if (nrun > 1) {
        unsigned int i;
        for (i = nrun - 1; i > 0; --i) {
            short (* pbrun)[2] = the_hdr->priv.put.brun;

            if (pbrun[i][1] - pbrun[i][0] > 255 &&
                pbrun[i-1][1] + 4 > pbrun[i][0]) {

                int j;

                pbrun[i-1][1] = pbrun[i][1];

                for (j = i; j < nrun - 1; ++j) {
                    pbrun[j][0] = pbrun[j+1][0];
                    pbrun[j][1] = pbrun[j+1][1];
                }
                --nrun;
            }
        }
    }

    if (nrun > 0) {
        short rstart;         /* Starting point of current run. */
        short runval;         /* Data value for current run. */
        int mask;

        if (the_hdr->priv.put.nblank > 0) {
            SkipBlankLines(the_hdr->priv.put.nblank);
            the_hdr->priv.put.nblank = 0;
        }
        /* Do all channels */
        for (mask = (the_hdr->alpha ? -1 : 0), rstart = 0, runval = 0;
             mask < (int)the_hdr->ncolors;
             ++mask) {

            if (bits[mask & 0xff]) {
                SetColor(mask);

                /* N.B. 'mask' is -1 for alpha channel, and rows[mask] still
                   works (see definition of 'rows') and, so does
                   bits[mask & 0xff], as bits[255] is for the alpha channel.
                */

                putRowOneColor(the_hdr, the_hdr->priv.put.brun,
                               rows[mask], rowlen, nrun,
                               &rstart, &runval);

                if (mask != the_hdr->ncolors - 1)
                    NewScanLine(FALSE);
            }
        }
    }
    /* Increment to next scanline */
    ++the_hdr->priv.put.nblank;

    /* flush every scanline */
    fflush(the_hdr->rle_file);
}



void
rle_put_init(rle_hdr * const the_hdr) {
/*----------------------------------------------------------------------------
  Initialize the header structure for writing scanlines.

  Inputs:
       [None]
  Outputs:
       the_hdr:        Private portions initialized for output.
-----------------------------------------------------------------------------*/
    the_hdr->dispatch = RUN_DISPATCH;

    if (the_hdr->is_init != RLE_INIT_MAGIC) {
        the_hdr->cmd = "Urt";
        the_hdr->file_name = "some file";
    }
    the_hdr->priv.put.nblank = 0;       /* Reinit static vars */
    /* Would like to be able to free previously allocated storage,
     * but can't count on a non-NULL value being a valid pointer.
     */
    the_hdr->priv.put.brun = NULL;
    the_hdr->priv.put.fileptr = 0;

    /* Only save alpha if alpha AND alpha channel bit are set. */
    if (the_hdr->alpha)
        the_hdr->alpha = (RLE_BIT( *the_hdr, -1 ) != 0);
    else
        RLE_CLR_BIT(*the_hdr, -1);
}



void
rle_put_setup(rle_hdr * const the_hdr) {
/*----------------------------------------------------------------------------
  Initialize for writing RLE, and write header to output file.

  Inputs:
       the_hdr:        Describes output image.
  Outputs:
       the_hdr:        Initialized.
-----------------------------------------------------------------------------*/

    rle_put_init( the_hdr );
    the_hdr->img_num++;         /* Count output images. */
    Setup();
}



void
rle_defaultBlockHook(rle_hdr * const the_hdr) {
}



void
rle_puteof(rle_hdr * const the_hdr) {
/*----------------------------------------------------------------------------
  Write an EOF code into the output file.
-----------------------------------------------------------------------------*/
    if (the_hdr->dispatch == NO_DISPATCH) {
        /* puteof already done */
    } else {
        PutEof();
        fflush( the_hdr->rle_file);
        /* Free storage allocated by rle_put_init. */
        if (the_hdr->priv.put.brun) {
            free(the_hdr->priv.put.brun);
            the_hdr->priv.put.brun = NULL;
        }
        /* Signal that puteof has been called. */
        the_hdr->dispatch = NO_DISPATCH;
    }
}



