/*===========================================================================*
 * frames.h
 *
 *  stuff dealing with frames
 *
 *===========================================================================*/

#ifndef FRAMES_INCLUDED
#define FRAMES_INCLUDED

/*==============*
 * HEADER FILES *
 *==============*/

#include "pm_config.h"  /* For __inline__ */
#include "mtypes.h"
#include "mheaders.h"
#include "iframe.h"
#include "frame.h"


/*===========*
 * CONSTANTS *
 *===========*/

#define I_FRAME 1
#define P_FRAME 2
#define B_FRAME 3

#define LUM_BLOCK   0
#define CHROM_BLOCK 1
#define CR_BLOCK    2
#define CB_BLOCK    3

#define MOTION_FORWARD      0
#define MOTION_BACKWARD     1
#define MOTION_INTERPOLATE  2


#define USE_HALF    0
#define USE_FULL    1

    /* motion vector stuff */
#define FORW_F_CODE fCode       /* from picture header */
#define BACK_F_CODE fCode
#define FORW_F  (1 << (FORW_F_CODE - 1))
#define BACK_F  (1 << (BACK_F_CODE - 1))
#define RANGE_NEG   (-(1 << (3 + FORW_F_CODE)))
#define RANGE_POS   ((1 << (3 + FORW_F_CODE))-1)
#define MODULUS     (1 << (4 + FORW_F_CODE))

#define ORIGINAL_FRAME  0
#define DECODED_FRAME   1


/*=======================*
 * STRUCTURE DEFINITIONS *
 *=======================*/

typedef struct FrameTableStruct {
    /* the following are all initted once and never changed */
    /* (they depend only on the pattern */
    char typ;
    struct FrameTableStruct *next;
    struct FrameTableStruct *prev;

    /* nextOutput is a pointer to next frame table entry to output */
    struct FrameTableStruct *nextOutput;

    boolean freeNow;    /* TRUE iff no frames point back to this */

    int number;

    int bFrameNumber;       /* actual frame number, if a b-frame */

} FrameTable;


/*==================*
 * TYPE DEFINITIONS *
 *==================*/

typedef struct dct_data_tye_struct {
  char useMotion;
  char pattern, mode;
  int fmotionX, fmotionY, bmotionX, bmotionY;
} dct_data_type;


/*==================*
 * GLOBAL VARIABLES *
 *==================*/

extern Block    **dct, **dctr, **dctb;
extern dct_data_type **dct_data;


/*========*
 * MACROS *
 *========*/

/* return ceiling(a/b) where a, b are ints, using temp value c */
#define int_ceil_div(a,b,c)     ((b*(c = a/b) < a) ? (c+1) : c)
#define int_floor_div(a,b,c)    ((b*(c = a/b) > a) ? (c-1) : c)

#define BLOCK_TO_FRAME_COORD(bx1, bx2, x1, x2) {    \
    x1 = (bx1)*DCTSIZE;             \
    x2 = (bx2)*DCTSIZE;             \
    }

static __inline__ void
MotionToFrameCoord(int   const by,
                   int   const bx,
                   int   const my,
                   int   const mx,
                   int * const yP,
                   int * const xP) {
/*----------------------------------------------------------------------------
   Given a DCT block location and a motion vector, return the pixel
   coordinates to which the upper left corner of the block moves.

   Return negative coordinates if it moves out of frame.
-----------------------------------------------------------------------------*/
    *yP = by * DCTSIZE + my;
    *xP = bx * DCTSIZE + mx;
}



#define COORD_IN_FRAME(fy,fx, type)                 \
    ((type == LUM_BLOCK) ?                      \
     ((fy >= 0) && (fx >= 0) && (fy < Fsize_y) && (fx < Fsize_x)) : \
     ((fy >= 0) && (fx >= 0) && (fy < (Fsize_y>>1)) && (fx < (Fsize_x>>1))))


static __inline__ void
encodeMotionVector(int      const x,
                   int      const y,
                   vector * const qP,
                   vector * const rP,
                   int      const f,
                   int      const fCode) {

    int tempX;
    int tempY;
    int tempC;

    if (x < RANGE_NEG)
        tempX = x + MODULUS;
    else if (x > RANGE_POS)
        tempX = x - MODULUS;
    else
        tempX = x;

    if (y < RANGE_NEG)
        tempY = y + MODULUS;
    else if (y > RANGE_POS)
        tempY = y - MODULUS;
    else
        tempY = y;

    if (tempX >= 0) {
        qP->x = int_ceil_div(tempX, f, tempC);
        rP->x = f - 1 + tempX - qP->x*f;
    } else {
        qP->x = int_floor_div(tempX, f, tempC);
        rP->x = f - 1 - tempX + qP->x*f;
    }

    if (tempY >= 0) {
        qP->y = int_ceil_div(tempY, f, tempC);
        rP->y = f - 1 + tempY - qP->y*f;
    } else {
        qP->y = int_floor_div(tempY, f, tempC);
        rP->y = f - 1 - tempY + qP->y*f;
    }
}



#define DoQuant(bit, src, dest)                                         \
  if (pattern & bit) {                                                  \
    switch (Mpost_QuantZigBlock(src, dest, QScale, FALSE)) {            \
    case MPOST_NON_ZERO:                                                \
      break;                                                            \
    case MPOST_ZERO:                                                    \
      pattern ^= bit;                                                   \
      break;                                                            \
    case MPOST_OVERFLOW:                                                \
      if (QScale != 31) {                                               \
    QScale++;                                                       \
    overflowChange = TRUE;                                          \
    overflowValue++;                                                \
    goto calc_blocks;                                               \
      }                                                                 \
      break;                                                            \
    }                                                                   \
  }

/*===============================*
 * EXTERNAL PROCEDURE prototypes *
 *===============================*/

void
AllocDctBlocks(void);

void
ComputeBMotionLumBlock(MpegFrame * const prev,
                       MpegFrame * const next,
                       int         const by,
                       int         const bx,
                       int         const mode,
                       motion      const motion,
                       LumBlock *  const motionBlockP);

int
BMotionSearch(const LumBlock * const currentBlockP,
              MpegFrame *      const prev,
              MpegFrame *      const next,
              int              const by,
              int              const bx,
              motion *         const motionP,
              int              const oldMode);

void GenPFrame (BitBucket * const bb,
                MpegFrame * const current,
                MpegFrame * const prev);
void GenBFrame (BitBucket * const bb,
                MpegFrame * const curr,
                MpegFrame * const prev,
                MpegFrame * const next);

float
PFrameTotalTime(void);

float
BFrameTotalTime(void);

void
ShowPFrameSummary(unsigned int const inputFrameBits,
                  unsigned int const totalBits,
                  FILE *       const fpointer);

void
ShowBFrameSummary(unsigned int const inputFrameBits,
                  unsigned int const totalBits,
                  FILE *       const fpointer);

/*==================*
 * GLOBAL VARIABLES *
 *==================*/

extern int pixelFullSearch;
extern int searchRangeP,searchRangeB;  /* defined in psearch.c */
extern int qscaleI;
extern int gopSize;
extern int slicesPerFrame;
extern int blocksPerSlice;
extern int referenceFrame;
extern int quietTime;       /* shut up for at least quietTime seconds;
                 * negative means shut up forever
                 */
extern boolean realQuiet;   /* TRUE = no messages to stdout */

extern boolean frameSummary;    /* TRUE = frame summaries should be printed */
extern boolean  printSNR;
extern boolean  printMSE;
extern boolean  decodeRefFrames;    /* TRUE = should decode I and P frames */
extern int  fCodeI,fCodeP,fCodeB;
extern boolean    forceEncodeLast;
extern int TIME_RATE;

#endif


/*
 * Copyright (c) 1995 The Regents of the University of California.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
 * CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

/*
 *  $Header: /n/picasso/project/mpeg/mpeg_dist/mpeg_encode/headers/RCS/frames.h,v 1.13 1995/08/15 23:43:04 smoot Exp $
 *  $Log: frames.h,v $
 *  Revision 1.13  1995/08/15 23:43:04  smoot
 *  *** empty log message ***
 *
 * Revision 1.12  1995/04/14  23:13:18  smoot
 * Reorganized for better rate control.  Added overflow in DCT values
 * handling.
 *
 * Revision 1.11  1995/01/19  23:54:46  smoot
 * allow computediffdcts to un-assert parts of the pattern
 *
 * Revision 1.10  1995/01/16  07:43:10  eyhung
 * Added realQuiet
 *
 * Revision 1.9  1995/01/10  23:15:28  smoot
 * Fixed searchRange lack of def
 *
 * Revision 1.8  1994/11/15  00:55:36  smoot
 * added printMSE
 *
 * Revision 1.7  1994/11/14  22:51:02  smoot
 * added specifics flag.  Added BlockComputeSNR parameters
 *
 * Revision 1.6  1994/11/01  05:07:23  darryl
 *  with rate control changes added
 *
 * Revision 1.1  1994/09/27  01:02:55  darryl
 * Initial revision
 *
 * Revision 1.5  1993/07/22  22:24:23  keving
 * nothing
 *
 * Revision 1.4  1993/07/09  00:17:23  keving
 * nothing
 *
 * Revision 1.3  1993/06/03  21:08:53  keving
 * nothing
 *
 * Revision 1.2  1993/03/02  19:00:27  keving
 * nothing
 *
 * Revision 1.1  1993/02/19  20:15:51  keving
 * nothing
 *
 */


