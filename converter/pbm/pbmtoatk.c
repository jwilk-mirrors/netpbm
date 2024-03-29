/* pbmtoatk.c - convert portable bitmap to Andrew Toolkit raster object
**
** Copyright (C) 1991 by Bill Janssen.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdio.h>
#include <string.h>

#include "pm_c_util.h"
#include "nstring.h"
#include "pbm.h"

#define DEFAULTSCALE (1<<16)
#define RASTERVERSION 2


static void
write_atk_bytes(FILE *        const file,
                unsigned char const curbyte,
                unsigned int  const startcount) {

    /* codes for data stream */
    static unsigned char const whitezero   = 'f';
    static unsigned char const whitetwenty = 'z';
    static unsigned char const blackzero   = 'F';
    static unsigned char const blacktwenty = 'Z';
    static unsigned char const otherzero   = 0x1F;

    #define WHITEBYTE 0x00
    #define BLACKBYTE 0xFF

    /* WriteRow table for conversion of a byte value to two character
       hex representation
    */

    static unsigned char hex[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };

    unsigned int curcount;

    curcount = startcount;  /* initial value */

    switch (curbyte) {
    case WHITEBYTE:
        while (curcount > 20) {
            fputc(whitetwenty, file);
            curcount -= 20;
        }
        fputc(whitezero + curcount, file);
        break;
    case BLACKBYTE:
        while (curcount > 20) {
            fputc(blacktwenty, file);
            curcount -= 20;
        }
        fputc(blackzero + curcount, file);
        break;
    default:
        while (curcount > 16) {
            fputc(otherzero + 16, file);
            fputc(hex[curbyte / 16], file);
            fputc(hex[curbyte & 15], file);
            curcount -= 16;
        }
        if (curcount > 1)
            fputc(otherzero + curcount, file);
        else ;  /* the byte written will represent a single instance */
        fputc(hex[curbyte / 16], file);
        fputc(hex[curbyte & 15], file);
    }
}



static void
process_atk_byte(int *           const pcurcount,
                 unsigned char * const pcurbyte,
                 FILE *          const file,
                 unsigned char   const newbyte,
                 int             const eolflag) {

    int curcount;
    unsigned char curbyte;

    curcount = *pcurcount;  /* initial value */
    curbyte  = *pcurbyte;  /* initial value */

    if (curcount < 1) {
        *pcurbyte = curbyte = newbyte;
        *pcurcount = curcount = 1;
    } else if (newbyte == curbyte) {
        *pcurcount = (curcount += 1);
    }

    if (curcount > 0 && newbyte != curbyte) {
        write_atk_bytes (file, curbyte, curcount);
        *pcurcount = 1;
        *pcurbyte = newbyte;
    }

    if (eolflag) {
        write_atk_bytes (file, *pcurbyte, *pcurcount);
        fprintf(file, " |\n");
        *pcurcount = 0;
        *pcurbyte = 0;
    }
}



int
main(int argc, const char ** argv) {

    FILE * ifP;
    bit * bitrow;
    int rows, cols, format;
    unsigned int row;
    unsigned char curbyte;
    int curcount;

    pm_proginit(&argc, argv);

    if (argc-1 > 1)
        pm_error("Too many arguments.  Only argument is file name");

    else if (argc-1 == 1) {
        ifP = pm_openr(argv[1]);
    } else {
        ifP = stdin;
    }

    pbm_readpbminit(ifP, &cols, &rows, &format);
    /* Note: atktopbm imposes limits: cols <= 1000000 rows <= 1000000
       Whether these values are defined in the official file format
       specification is unknown.  We issue warning messages when large
       values are encountered.
     */

    if (cols > 1000000)
        pm_message("Proceeding with extremely large width value: %u", cols);
    if (rows > 1000000)
        pm_message("Proceeding with extremely large height value: %u", rows);

    bitrow = pbm_allocrow_packed(cols);

    printf("\\begindata{raster,%d}\n", 1);
    printf("%d %d %d %d ", RASTERVERSION, 0, DEFAULTSCALE, DEFAULTSCALE);
    printf("%d %d %d %d\n", 0, 0, cols, rows); /* subraster */
    printf("bits %d %d %d\n", 1, cols, rows);

    for (row = 0; row < rows; ++row) {
        unsigned int const byteCt = pbm_packed_bytes(cols);
        unsigned int i;

        pbm_readpbmrow_packed(ifP, bitrow, cols, format);
        pbm_cleanrowend_packed(bitrow, cols);

        for (i = 0, curbyte = 0, curcount = 0; i < byteCt; ++i) {
            process_atk_byte(&curcount, &curbyte, stdout,
                             bitrow[i],
                             i + 1 < byteCt ? FALSE : TRUE );
        }
    }

    pbm_freerow_packed(bitrow);
    pm_close(ifP);

    printf("\\enddata{raster, %d}\n", 1);

    return 0;
}



