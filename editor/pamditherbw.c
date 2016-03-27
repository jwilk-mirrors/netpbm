/*=============================================================================
                           pamditherbw
===============================================================================
   Dither a grayscale PAM to a black and white PAM.

   By Bryan Henderson, San Jose CA.  June 2004.

   Contributed to the public domain by its author.

   Based on ideas from Pgmtopbm by Jef Poskanzer, 1989.
=============================================================================*/

#include <assert.h>
#include <string.h>

#include "pm_c_util.h"
#include "pam.h"
#include "dithers.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pm_gamma.h"

enum halftone {QT_FS,
               QT_ATKINSON,
               QT_THRESH,
               QT_DITHER8,
               QT_CLUSTER,
               QT_HILBERT};

enum ditherType {DT_REGULAR, DT_CLUSTER};

static sample blackSample = (sample) PAM_BLACK;
static sample whiteSample = (sample) PAM_BW_WHITE;
static tuple  const blackTuple = &blackSample;
static tuple  const whiteTuple = &whiteSample;

struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char *  inputFilespec;
    enum halftone halftone;
    unsigned int  clumpSize;
        /* Defined only for halftone == QT_HILBERT */
    unsigned int  clusterRadius;  
        /* Defined only for halftone == QT_CLUSTER */
    float         threshval;
    unsigned int  randomseed;
    unsigned int  randomseedSpec;
};




static void
parseCommandLine(int argc, char ** argv,
                 struct cmdlineInfo *cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry *option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;
    unsigned int floydOpt, atkinsonOpt, hilbertOpt, thresholdOpt, dither8Opt,
        cluster3Opt, cluster4Opt, cluster8Opt;
    unsigned int valueSpec, clumpSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "floyd",     OPT_FLAG,  NULL, &floydOpt,     0);
    OPTENT3(0, "fs",        OPT_FLAG,  NULL, &floydOpt,     0);
    OPTENT3(0, "atkinson",  OPT_FLAG,  NULL, &atkinsonOpt,     0);
    OPTENT3(0, "threshold", OPT_FLAG,  NULL, &thresholdOpt, 0);
    OPTENT3(0, "hilbert",   OPT_FLAG,  NULL, &hilbertOpt,   0);
    OPTENT3(0, "dither8",   OPT_FLAG,  NULL, &dither8Opt,   0);
    OPTENT3(0, "d8",        OPT_FLAG,  NULL, &dither8Opt,   0);
    OPTENT3(0, "cluster3",  OPT_FLAG,  NULL, &cluster3Opt,  0);
    OPTENT3(0, "c3",        OPT_FLAG,  NULL, &cluster3Opt,  0);
    OPTENT3(0, "cluster4",  OPT_FLAG,  NULL, &cluster4Opt,  0);
    OPTENT3(0, "c4",        OPT_FLAG,  NULL, &cluster4Opt,  0);
    OPTENT3(0, "cluster8",  OPT_FLAG,  NULL, &cluster8Opt,  0);
    OPTENT3(0, "c8",        OPT_FLAG,  NULL, &cluster8Opt,  0);
    OPTENT3(0, "value",     OPT_FLOAT, &cmdlineP->threshval, 
            &valueSpec, 0);
    OPTENT3(0, "clump",     OPT_UINT,  &cmdlineP->clumpSize, 
            &clumpSpec, 0);
    OPTENT3(0,   "randomseed",   OPT_UINT,    &cmdlineP->randomseed,
            &cmdlineP->randomseedSpec,      0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    free(option_def);

    if (floydOpt + atkinsonOpt + thresholdOpt + hilbertOpt + dither8Opt + 
        cluster3Opt + cluster4Opt + cluster8Opt == 0)
        cmdlineP->halftone = QT_FS;
    else if (floydOpt + atkinsonOpt + thresholdOpt + dither8Opt + 
        cluster3Opt + cluster4Opt + cluster8Opt > 1)
        pm_error("Cannot specify more than one halftoning type");
    else {
        if (floydOpt)
            cmdlineP->halftone = QT_FS;
        else if (atkinsonOpt)
            cmdlineP->halftone = QT_ATKINSON;
        else if (thresholdOpt)
            cmdlineP->halftone = QT_THRESH;
        else if (hilbertOpt) {
            cmdlineP->halftone = QT_HILBERT;

            if (!clumpSpec)
                cmdlineP->clumpSize = 5;
            else {
                if (cmdlineP->clumpSize < 2)
                    pm_error("-clump must be at least 2.  You specified %u",
                             cmdlineP->clumpSize);
            }
        } else if (dither8Opt)
            cmdlineP->halftone = QT_DITHER8;
        else if (cluster3Opt) {
            cmdlineP->halftone = QT_CLUSTER;
            cmdlineP->clusterRadius = 3;
        } else if (cluster4Opt) {
            cmdlineP->halftone = QT_CLUSTER;
            cmdlineP->clusterRadius = 4;
        } else if (cluster8Opt) {
            cmdlineP->halftone = QT_CLUSTER;
            cmdlineP->clusterRadius = 8;
        } else 
            pm_error("INTERNAL ERROR.  No halftone option");
    }

    if (!valueSpec)
        cmdlineP->threshval = 0.5;
    else {
        if (cmdlineP->threshval < 0.0)
            pm_error("-value cannot be negative.  You specified %f",
                     cmdlineP->threshval);
        if (cmdlineP->threshval > 1.0)
            pm_error("-value cannot be greater than one.  You specified %f",
                     cmdlineP->threshval);
    }

    if (clumpSpec && cmdlineP->halftone != QT_HILBERT)
        pm_error("-clump is not valid without -hilbert");

    if (argc-1 > 1)
        pm_error("Too many arguments (%d).  There is at most one "
                 "non-option argument:  the file name",
                 argc-1);
    else if (argc-1 == 1)
        cmdlineP->inputFilespec = argv[1];
    else
        cmdlineP->inputFilespec = "-";
}



static struct pam
makeOutputPam(unsigned int const width,
              unsigned int const height) {

    struct pam outpam;

    outpam.size = sizeof(outpam);
    outpam.len = PAM_STRUCT_SIZE(tuple_type);
    outpam.file = stdout;
    outpam.format = PAM_FORMAT;
    outpam.plainformat = 0;
    outpam.height = height;
    outpam.width = width;
    outpam.depth = 1;
    outpam.maxval = 1;
    outpam.bytes_per_sample = 1;
    strcpy(outpam.tuple_type, "BLACKANDWHITE");

    return outpam;
}



/* Hilbert curve tracer */

#define MAXORD 18

struct Hil {
    int order;
    int ord;
    int turn;
    int dx;
    int dy;
    int x;
    int y;
    int stage[MAXORD];
    int width;
    int height;
};

static void 
initHilbert(int          const w, 
            int          const h,
            struct Hil * const hilP) {
/*----------------------------------------------------------------------------
  Initialize the Hilbert curve tracer 
-----------------------------------------------------------------------------*/
    int big, ber;
    hilP->width = w;
    hilP->height = h;
    big = w > h ? w : h;
    for (ber = 2, hilP->order = 1; ber < big; ber <<= 1, hilP->order++);
    if (hilP->order > MAXORD)
        pm_error("Sorry, hilbert order is too large");
    hilP->ord = hilP->order;
    hilP->order--;
}



static bool
hilbert(int *        const px,
        int *        const py,
        struct Hil * const hilP) {
/*----------------------------------------------------------------------------
  Return non-zero if got another point
-----------------------------------------------------------------------------*/
    int temp;
    if (hilP->ord > hilP->order) {
        /* have to do first point */

        hilP->ord--;
        hilP->stage[hilP->ord] = 0;
        hilP->turn = -1;
        hilP->dy = 1;
        hilP->dx = hilP->x = hilP->y = 0;
        *px = *py = 0;
        return true;
    }

    /* Operate the state machine */
    for(;;)  {
        switch (hilP->stage[hilP->ord]) {
        case 0:
            hilP->turn = -hilP->turn;
            temp = hilP->dy;
            hilP->dy = -hilP->turn * hilP->dx;
            hilP->dx = hilP->turn * temp;
            if (hilP->ord > 0) {
                hilP->stage[hilP->ord] = 1;
                hilP->ord--;
                hilP->stage[hilP->ord]=0;
                continue;
            }
        case 1:
            hilP->x += hilP->dx;
            hilP->y += hilP->dy;
            if (hilP->x < hilP->width && hilP->y < hilP->height) {
                hilP->stage[hilP->ord] = 2;
                *px = hilP->x;
                *py = hilP->y;
                return true;
            }
        case 2:
            hilP->turn = -hilP->turn;
            temp = hilP->dy;
            hilP->dy = -hilP->turn * hilP->dx;
            hilP->dx = hilP->turn * temp;
            if (hilP->ord > 0) { 
                /* recurse */

                hilP->stage[hilP->ord] = 3;
                hilP->ord--;
                hilP->stage[hilP->ord]=0;
                continue;
            }
        case 3:
            hilP->x += hilP->dx;
            hilP->y += hilP->dy;
            if (hilP->x < hilP->width && hilP->y < hilP->height) {
                hilP->stage[hilP->ord] = 4;
                *px = hilP->x;
                *py = hilP->y;
                return true;
            }
        case 4:
            if (hilP->ord > 0) {
                /* recurse */
                hilP->stage[hilP->ord] = 5;
                hilP->ord--;
                hilP->stage[hilP->ord]=0;
                continue;
            }
        case 5:
            temp = hilP->dy;
            hilP->dy = -hilP->turn * hilP->dx;
            hilP->dx = hilP->turn * temp;
            hilP->turn = -hilP->turn;
            hilP->x += hilP->dx;
            hilP->y += hilP->dy;
            if (hilP->x < hilP->width && hilP->y < hilP->height) {
                hilP->stage[hilP->ord] = 6;
                *px = hilP->x;
                *py = hilP->y;
                return true;
            }
        case 6:
            if (hilP->ord > 0) {
                /* recurse */
                hilP->stage[hilP->ord] = 7;
                hilP->ord--;
                hilP->stage[hilP->ord]=0;
                continue;
            }
        case 7:
            temp = hilP->dy;
            hilP->dy = -hilP->turn * hilP->dx;
            hilP->dx = hilP->turn * temp;
            hilP->turn = -hilP->turn;
            /* Return from a recursion */
            if (hilP->ord < hilP->order)
                hilP->ord++;
            else
                return false;
        }
    }
}



static void 
doHilbert(FILE *       const ifP,
          unsigned int const clumpSize) {
/*----------------------------------------------------------------------------
  Use hilbert space filling curve dithering
-----------------------------------------------------------------------------*/
    /*
     * This is taken from the article "Digital Halftoning with
     * Space Filling Curves" by Luiz Velho, proceedings of
     * SIGRAPH '91, page 81.
     *
     * This is not a terribly efficient or quick version of
     * this algorithm, but it seems to work. - Graeme Gill.
     * graeme@labtam.labtam.OZ.AU
     *
     */
    struct pam graypam;
    struct pam bitpam;
    tuple ** grays;
    tuple ** bits;

    struct Hil hil;

    int end;
    int *x,*y;
    int sum;

    grays = pnm_readpam(ifP, &graypam, PAM_STRUCT_SIZE(tuple_type));

    bitpam = makeOutputPam(graypam.width, graypam.height);

    bits = pnm_allocpamarray(&bitpam);

    MALLOCARRAY(x, clumpSize);
    MALLOCARRAY(y, clumpSize);
    if (x == NULL  || y == NULL)
        pm_error("out of memory");
    initHilbert(graypam.width, graypam.height, &hil);

    sum = 0;
    end = clumpSize;

    while (end == clumpSize) {
        unsigned int i;
        /* compute the next cluster co-ordinates along hilbert path */
        for (i = 0; i < end; i++) {
            bool gotPoint;
            gotPoint = hilbert(&x[i], &y[i], &hil);
            if (!gotPoint)
                end = i;    /* we reached the end */
        }
        /* sum levels */
        for (i = 0; i < end; i++)
            sum += grays[y[i]][x[i]][0];
        /* dither half and half along path */
        for (i = 0; i < end; i++) {
            unsigned int const row = y[i];
            unsigned int const col = x[i];
            if (sum >= graypam.maxval) {
                bits[row][col][0] = 1;
                sum -= graypam.maxval;
            } else
                bits[row][col][0] = 0;
        }
    }
    free(x);    free(y); 
    pnm_writepam(&bitpam, bits);

    pnm_freepamarray(bits, &bitpam);
    pnm_freepamarray(grays, &graypam);
}



struct converter {
    void (*convertRow)(struct converter * const converterP,
                       unsigned int       const row,
                       tuplen                   grayrow[], 
                       tuple                    bitrow[]);
    void (*destroy)(struct converter * const converterP);
    unsigned int cols;
    void * stateP;
};



struct fsState {
    float * thiserr;
        /* thiserr[N] is the power from previous pixels to include in 
           future column N of the current row.
        */
    float * nexterr;
        /* nexterr[N] is the power from previous pixels to include in 
           future column N of the next row.
        */
    bool fs_forward;
        /* We're going forward (left to right) through the current row */
    samplen white;
        /* The power value we consider to be white (normally 1.0).
           Constant. */
    samplen halfWhite;
        /* The power value we consider to be half white (always half of
           'white'; carried separately to save computation)
        */
};


static void
fsConvertRow(struct converter * const converterP,
             unsigned int       const row,
             tuplen                   grayrow[],
             tuple                    bitrow[]) {

    struct fsState * const stateP = converterP->stateP;

    samplen * const thiserr = stateP->thiserr;
    samplen * const nexterr = stateP->nexterr;

    unsigned int limitcol;
    unsigned int col;
    
    for (col = 0; col < converterP->cols + 2; ++col)
        nexterr[col] = 0.0;

    if (stateP->fs_forward) {
        col = 0;
        limitcol = converterP->cols;
    } else {
        col = converterP->cols - 1;
        limitcol = -1;
    }

    do {
        samplen const thisPixelPower =
            MIN(pm_ungamma709(grayrow[col][0]), stateP->white);
        samplen accum;

        accum = thisPixelPower + thiserr[col + 1];

        if (accum >= stateP->halfWhite) {
            /* We've accumulated enough light (power) to justify a
               white output pixel.
            */
            bitrow[col] = whiteTuple;
            /* Remove from sum the power of this white output pixel */
            accum -= stateP->white;
        } else
            bitrow[col] = blackTuple;

        /* Forward to future output pixels the power from current
           input pixel and the power forwarded from previous input
           pixels to the current pixel, less any power we put into the
           current output pixel.
        */
        if (stateP->fs_forward) {
            thiserr[col + 2] += (accum * 7) / 16;
            nexterr[col    ] += (accum * 3) / 16;
            nexterr[col + 1] += (accum * 5) / 16;
            nexterr[col + 2] += (accum * 1) / 16;
            
            ++col;
        } else {
            thiserr[col    ] += (accum * 7) / 16;
            nexterr[col + 2] += (accum * 3) / 16;
            nexterr[col + 1] += (accum * 5) / 16;
            nexterr[col    ] += (accum * 1) / 16;
            
            --col;
        }
    } while (col != limitcol);
    
    stateP->thiserr = nexterr;
    stateP->nexterr = thiserr;
    stateP->fs_forward = ! stateP->fs_forward;
}



static void
fsDestroy(struct converter * const converterP) {

    struct fsState * const stateP = converterP->stateP;

    free(stateP->thiserr);
    free(stateP->nexterr);

    free(stateP);
}



static struct converter
createFsConverter(struct pam * const graypamP,
                  float        const threshFraction) {

    struct fsState * stateP;
    struct converter converter;

    converter.cols       = graypamP->width;
    converter.convertRow = &fsConvertRow;
    converter.destroy    = &fsDestroy;

    MALLOCVAR_NOFAIL(stateP);

    /* Initialize Floyd-Steinberg error vectors. */
    MALLOCARRAY_NOFAIL(stateP->thiserr, graypamP->width + 2);
    MALLOCARRAY_NOFAIL(stateP->nexterr, graypamP->width + 2);

    {
        /* (random errors in [-1/8 .. 1/8]) */
        unsigned int col;
        for (col = 0; col < graypamP->width + 2; ++col)
            stateP->thiserr[col] = ((float)rand()/RAND_MAX - 0.5) / 4;
    }

    stateP->halfWhite = threshFraction;
    stateP->white = 2 * threshFraction;

    stateP->fs_forward = TRUE;

    converter.stateP = stateP;

    return converter;
}



struct atkinsonState {
    float * error[3];
        /* error[R][C] is the power from previous pixels to include
           in column C of the Rth row down from the current row
           (0th row is the current row).

           No error propagates down more than two rows.

           For R == 0, C is a column we haven't done yet.
        */
    samplen white;
        /* The power value we consider to be white (normally 1.0).
           Constant. */
    samplen halfWhite;
        /* The power value we consider to be half white (always half of
           'white'; carried separately to save computation)
        */
};


static void
moveAtkinsonErrorWindowDown(struct converter * const converterP) {
                            
    struct atkinsonState * const stateP = converterP->stateP;

    float * const oldError0 = stateP->error[0];

    unsigned int relRow;
    unsigned int col;

    for (relRow = 0; relRow < 2; ++relRow)
        stateP->error[relRow] = stateP->error[relRow+1];

    for (col = 0; col < converterP->cols + 2; ++col)
        oldError0[col] = 0.0;

    stateP->error[2] = oldError0;
}



static void
atkinsonConvertRow(struct converter * const converterP,
                   unsigned int       const row,
                   tuplen                   grayrow[],
                   tuple                    bitrow[]) {

    /* See http://www.tinrocket.com/projects/programming/graphics/00158/
       for a description of the Atkinson algorithm
    */

    struct atkinsonState * const stateP = converterP->stateP;

    samplen ** const error = stateP->error;

    unsigned int col;

    for (col = 0; col < converterP->cols; ++col) {
        samplen accum;

        accum = pm_ungamma709(grayrow[col][0]) + error[0][col + 1];
        if (accum >= stateP->halfWhite) {
            /* We've accumulated enough light (power) to justify a
               white output pixel.
            */
            bitrow[col] = whiteTuple;
            /* Remove from accum the power of this white output pixel */
            accum -= stateP->white;
        } else
            bitrow[col] = blackTuple;
        
        /* Forward to future output pixels 3/4 of the power from current
           input pixel and the power forwarded from previous input
           pixels to the current pixel, less any power we put into the
           current output pixel.
        */
        error[0][col+1] += accum/8;
        error[0][col+2] += accum/8;
        if (col > 0)
            error[1][col-1] += accum/8;
        error[1][col  ] += accum/8;
        error[1][col+1] += accum/8;
        error[2][col  ] += accum/8;
    }
    
    moveAtkinsonErrorWindowDown(converterP);
}



static void
atkinsonDestroy(struct converter * const converterP) {

    struct atkinsonState * const stateP = converterP->stateP;

    unsigned int relRow;

    for (relRow = 0; relRow < 3; ++relRow)
        free(stateP->error[relRow]);

    free(stateP);
}



static struct converter
createAtkinsonConverter(struct pam * const graypamP,
                        float        const threshFraction) {

    struct atkinsonState * stateP;
    struct converter converter;
    unsigned int relRow;
    
    converter.cols       = graypamP->width;
    converter.convertRow = &atkinsonConvertRow;
    converter.destroy    = &atkinsonDestroy;

    MALLOCVAR_NOFAIL(stateP);

    for (relRow = 0; relRow < 3; ++relRow)
        MALLOCARRAY_NOFAIL(stateP->error[relRow], graypamP->width + 2);

    {
        /* (random errors in [-1/8 .. 1/8]) */
        unsigned int col;
        for (col = 0; col < graypamP->width + 2; ++col) {
            stateP->error[0][col] = ((float)rand()/RAND_MAX - 0.5) / 4;
            stateP->error[1][col] = 0.0;
            stateP->error[2][col] = 0.0;
        }
    }

    stateP->halfWhite = threshFraction;
    stateP->white = 2 * threshFraction;

    converter.stateP = stateP;

    return converter;
}



struct threshState {
    samplen threshval;
};


static void
threshConvertRow(struct converter * const converterP,
                 unsigned int       const row,
                 tuplen                   grayrow[],
                 tuple                    bitrow[]) {
    
    struct threshState * const stateP = converterP->stateP;

    unsigned int col;
    for (col = 0; col < converterP->cols; ++col)
        bitrow[col] =
            grayrow[col][0] >= stateP->threshval ? whiteTuple : blackTuple;
}



static void
threshDestroy(struct converter * const converterP) {
    free(converterP->stateP);
}



static struct converter
createThreshConverter(struct pam * const graypamP,
                      float        const threshFraction) {

    struct threshState * stateP;
    struct converter converter;

    MALLOCVAR_NOFAIL(stateP);

    converter.cols       = graypamP->width;
    converter.convertRow = &threshConvertRow;
    converter.destroy    = &threshDestroy;
    
    stateP->threshval    = threshFraction;
    converter.stateP     = stateP;

    return converter;
}



struct clusterState {
    unsigned int radius;
    float ** clusterMatrix;
};



static void
clusterConvertRow(struct converter * const converterP,
                  unsigned int       const row,
                  tuplen                   grayrow[],
                  tuple                    bitrow[]) {

    struct clusterState * const stateP = converterP->stateP;
    unsigned int const diameter = 2 * stateP->radius;

    unsigned int col;

    for (col = 0; col < converterP->cols; ++col) {
        float const threshold = 
            stateP->clusterMatrix[row % diameter][col % diameter];
        bitrow[col] =
            grayrow[col][0] > threshold ? whiteTuple : blackTuple;
    }
}



static void
clusterDestroy(struct converter * const converterP) {

    struct clusterState * const stateP = converterP->stateP;
    unsigned int const diameter = 2 * stateP->radius;

    unsigned int row;

    for (row = 0; row < diameter; ++row)
        free(stateP->clusterMatrix[row]);

    free(stateP->clusterMatrix);
    
    free(stateP);
}



static struct converter
createClusterConverter(struct pam *    const graypamP,
                       enum ditherType const ditherType,
                       unsigned int    const radius) {
    
    /* TODO: We create a floating point normalized, gamma-adjusted
       dither matrix from the old integer dither matrices that were 
       developed for use with integer arithmetic.  We really should
       just change the literal values in dither.h instead of computing
       the matrix from the integer literal values here.
    */
    
    int const clusterNormalizer = radius * radius * 2;
    unsigned int const diameter = 2 * radius;

    struct converter converter;
    struct clusterState * stateP;
    unsigned int row;

    converter.cols       = graypamP->width;
    converter.convertRow = &clusterConvertRow;
    converter.destroy    = &clusterDestroy;

    MALLOCVAR_NOFAIL(stateP);

    stateP->radius = radius;

    MALLOCARRAY_NOFAIL(stateP->clusterMatrix, diameter);
    for (row = 0; row < diameter; ++row) {
        unsigned int col;

        MALLOCARRAY_NOFAIL(stateP->clusterMatrix[row], diameter);
        
        for (col = 0; col < diameter; ++col) {
            switch (ditherType) {
            case DT_REGULAR: 
                switch (radius) {
                case 8: 
                    stateP->clusterMatrix[row][col] = 
                        pm_gamma709((float)dither8[row][col] / 256);
                    break;
                default: 
                    pm_error("INTERNAL ERROR: invalid radius");
                }
                break;
            case DT_CLUSTER: {
                int val;
                switch (radius) {
                case 3: val = cluster3[row][col]; break;
                case 4: val = cluster4[row][col]; break;
                case 8: val = cluster8[row][col]; break;
                default:
                    pm_error("INTERNAL ERROR: invalid radius");
                }
                stateP->clusterMatrix[row][col] = 
                    pm_gamma709((float)val / clusterNormalizer);
            }
            break;
            }
        }
    }            

    converter.stateP = stateP;

    return converter;
}



int
main(int argc, char *argv[]) {

    struct cmdlineInfo cmdline;
    FILE* ifP;

    pgm_init(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    srand(cmdline.randomseedSpec ? cmdline.randomseed : pm_randseed());

    ifP = pm_openr(cmdline.inputFilespec);

    if (cmdline.halftone == QT_HILBERT)
        doHilbert(ifP, cmdline.clumpSize);
    else {
        struct converter converter;
        struct pam graypam;
        struct pam bitpam;
        tuplen * grayrow;
        tuple * bitrow;
        int row;

        pnm_readpaminit(ifP, &graypam, PAM_STRUCT_SIZE(tuple_type));

        bitpam = makeOutputPam(graypam.width, graypam.height);
        
        pnm_writepaminit(&bitpam);

        switch (cmdline.halftone) {
        case QT_FS:
            converter = createFsConverter(&graypam, cmdline.threshval);
            break;
        case QT_ATKINSON:
            converter = createAtkinsonConverter(&graypam, cmdline.threshval);
            break;
        case QT_THRESH:
            converter = createThreshConverter(&graypam, cmdline.threshval);
            break;
        case QT_DITHER8: 
            converter = createClusterConverter(&graypam, DT_REGULAR, 8); 
            break;
        case QT_CLUSTER: 
            converter = createClusterConverter(&graypam, 
                                               DT_CLUSTER, 
                                               cmdline.clusterRadius);
            break;
        case QT_HILBERT: 
                pm_error("INTERNAL ERROR: halftone is QT_HILBERT where it "
                         "shouldn't be.");
                break;
        }

        grayrow = pnm_allocpamrown(&graypam);
        MALLOCARRAY_NOFAIL(bitrow, bitpam.width);

        for (row = 0; row < graypam.height; ++row) {
            pnm_readpamrown(&graypam, grayrow);

            converter.convertRow(&converter, row, grayrow, bitrow);
            
            pnm_writepamrow(&bitpam, bitrow);
        }
        free(bitrow);
        pnm_freepamrow(grayrow);

        if (converter.destroy)
            converter.destroy(&converter);
    }

    pm_close(ifP);

    return 0;
}
