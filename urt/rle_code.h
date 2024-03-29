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
 */
/*
 * rle_code.h - Definitions for Run Length Encoding.
 *
 * Author:      Spencer W. Thomas
 *              Computer Science Dept.
 *              University of Utah
 * Date:        Mon Aug  9 1982
 * Copyright (c) 1982 Spencer W. Thomas
 *
 * $Header: /usr/users/spencer/src/urt/include/RCS/rle_code.h,v 3.0 90/08/03 15:19:48 spencer Exp $
 */

#ifndef RLE_MAGIC

/*
 * Opcode definitions
 */

#define     LONG                0x40
#define     RSkipLinesOp        1
#define     RSetColorOp         2
#define     RSkipPixelsOp       3
#define     RByteDataOp         5
#define     RRunDataOp          6
#define     REOFOp              7

#define     H_CLEARFIRST        0x1   /* clear framebuffer flag */
#define     H_NO_BACKGROUND     0x2   /* if set, no bg color supplied */
#define     H_ALPHA             0x4   /* if set, alpha channel (-1) present */
#define     H_COMMENT           0x8   /* if set, comments present */

struct XtndRsetup {
    /* This maps the layout of the header text in the file */

    unsigned char hc_xpos[2];
    unsigned char hc_ypos[2];
    unsigned char hc_xlen[2];
    unsigned char hc_ylen[2];
    unsigned char h_flags;
    unsigned char h_ncolors;
    unsigned char h_pixelbits;
    unsigned char h_ncmap;
    unsigned char h_cmaplen;   /* log2 of color map size */
};
#define     SETUPSIZE   ((4*2)+5)

/* "Old" RLE format magic numbers */
#define     RMAGIC      ('R' << 8)      /* top half of magic number */
#define     WMAGIC      ('W' << 8)      /* black&white rle image */

#define     RLE_MAGIC   ((short)0xcc52) /* RLE file magic number */

#endif /* RLE_MAGIC */
