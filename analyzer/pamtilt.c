/*=============================================================================
                             pamtilt
===============================================================================
  Print the tilt angle of an image (typically black and white text).

  Based on pgmskew by Gregg Townsend, August 2005.

  Adapted to Netpbm by Bryan Henderson, August 2005.

  All work has been contributed to the public domain by its authors.
=============================================================================*/

#define _XOPEN_SOURCE 500  /* get M_PI in math.h */

#include <assert.h>
#include <math.h>

#include "pm_c_util.h"
#include "pam.h"
#include "mallocvar.h"
#include "shhopt.h"

/* program constants */
#define DIFFMAX 255                     /* maximum scaling for differences */
#define BARLENGTH 60                    /* length of bars printed with -v */

struct cmdlineInfo {
    /* command parameters */
    const char * inputFilename;
    float maxangle;     /* maximum angle attempted */
    float astep;        /* initial angle increment */
    float qmin;         /* minimum quality of solution */
    unsigned int hstep; /* horizontal step size */
    unsigned int vstep; /* vertical step size */
    unsigned int dstep; /* difference distance */
    unsigned int fast;    /* skip third iteration */
    unsigned int verbose; /* generate commentary */
};

static void
abandon(void) {

    printf("00.00\n");
    exit(0);
}



static void
parseCommandLine(int argc, const char ** const argv,
                 struct cmdlineInfo * const cmdlineP) {

    static optEntry option_def[50];
    static optStruct3 opt;
    unsigned int option_def_index;

    /* set defaults */
    cmdlineP->hstep = 11;        /* read only every 11th column */
    cmdlineP->vstep = 5;         /* calc differences every 5th row */
    cmdlineP->dstep = 2;         /* check for differences two rows down */
    cmdlineP->maxangle = 10.0;   /* assume skew is less than +/- ten degrees */
    cmdlineP->astep = 1.0;       /* initially check by one-degree increments */
    cmdlineP->qmin = 1.0;        /* don't require S/N better than 1.0 */

    /* initialize option table */
    option_def_index = 0;       /* incremented by OPTENT3 */
    OPTENT3(0, "fast",     OPT_FLAG,  NULL, &cmdlineP->fast,     0);
    OPTENT3(0, "verbose",  OPT_FLAG,  NULL, &cmdlineP->verbose,  0);
    OPTENT3(0, "angle",    OPT_FLOAT, &cmdlineP->maxangle, NULL, 0);
    OPTENT3(0, "quality",  OPT_FLOAT, &cmdlineP->qmin,     NULL, 0);
    OPTENT3(0, "astep",    OPT_FLOAT, &cmdlineP->astep,    NULL, 0);
    OPTENT3(0, "hstep",    OPT_UINT,  &cmdlineP->hstep,    NULL, 0);
    OPTENT3(0, "vstep",    OPT_UINT,  &cmdlineP->vstep,    NULL, 0);
    OPTENT3(0, "dstep",    OPT_UINT,  &cmdlineP->dstep,    NULL, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;          /* no short options used */
    opt.allowNegNum = FALSE;            /* don't allow negative values */
    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);

    if (cmdlineP->hstep < 1)
        pm_error("-hstep must be at least 1 column.");
    if (cmdlineP->vstep < 1)
        pm_error("-vstep must be at least 1 row.");
    if (cmdlineP->dstep < 1)
        pm_error("-dstep must be at least 1 row.");
    if (cmdlineP->maxangle < 1 || cmdlineP->maxangle > 45)
        pm_error("-maxangle must be between 1 and 45 degrees.");

    if (argc-1 < 1)                      /* if input file name given */
        cmdlineP->inputFilename = "-";
    else {
        cmdlineP->inputFilename = argv[1];

        if (argc-1 > 1)
            pm_error("There is at most one argument.  You specified %d",
                     argc-1);
    }
}



static void
computeSteps(const struct pam * const pamP,
             unsigned int       const hstepReq,
             unsigned int       const vstepReq,
             unsigned int *     const hstepP,
             unsigned int *     const vstepP) {
/*----------------------------------------------------------------------------
  Adjust parameters if necessary now that we have the image size
-----------------------------------------------------------------------------*/
    if (pamP->width < 10 || pamP->height < 10)
        abandon();

    if (pamP->width < 10 * hstepReq)
        *hstepP = pamP->width / 10;
    else
        *hstepP = hstepReq;

    if (pamP->height < 10 * vstepReq)
        *vstepP = pamP->height / 10;
    else
        *vstepP = vstepReq;
}



static void
load(const struct pam * const pamP,
     unsigned int       const hstep,
     sample ***         const pixelsP,
     unsigned int *     const hsampleCtP) {
/*----------------------------------------------------------------------------
  Read part of the raster - to wit, every hstep'th column of every row.
  Return that sampling of the raster as *pixelsP, and the resulting
  array width (pixels in each row) as *hsampleCtP.
-----------------------------------------------------------------------------*/
    unsigned int const hsampleCt = 1 + (pamP->width - 1) / hstep;
        /* use only this many cols */

    unsigned int row;
    tuple *  tuplerow;
    sample ** pixels;

    tuplerow = pnm_allocpamrow(pamP);

    MALLOCARRAY(pixels, pamP->height);

    if (pixels == NULL)
        pm_error("Unable to allocate array of %u rows", pamP->height);

    for (row = 0; row < pamP->height; ++row) {
        unsigned int i;
        unsigned int col;

        pnm_readpamrow(pamP, tuplerow);

        MALLOCARRAY(pixels[row], hsampleCt);
        if (pixels[row] == NULL)
            pm_error("Unable to allocate %u-sample array for Row %u",
                     hsampleCt, row);

        /* save every hstep'th column */
        for (i = col = 0; i < hsampleCt; ++i, col += hstep)
            pixels[row][i] = tuplerow[col][0];
    }

    pnm_freepamrow(tuplerow);

    *hsampleCtP = hsampleCt;
    *pixelsP   = pixels;
}



static void
freePixels(sample **    const samples,
           unsigned int const rows) {

    unsigned int row;

    for (row = 0; row < rows; ++row)
        free(samples[row]);

    free(samples);
}



static void
replacePixelValuesWithScaledDiffs(
    const struct pam * const pamP,
    sample **          const pixels,
    unsigned int       const hsampleCt,
    unsigned int       const dstep) {
/*----------------------------------------------------------------------------
  Replace pixel values with scaled differences used for all
  calculations
-----------------------------------------------------------------------------*/
    float const m = DIFFMAX / (float) pamP->maxval;  /* scale multiplier */

    unsigned int row;

    for (row = dstep; row < pamP->height; ++row) {
        unsigned int col;
        for (col = 0; col < hsampleCt; ++col) {
            int const d = pixels[row - dstep][col] - pixels[row][col];
            unsigned int const absd = d < 0 ? -d : d;
            pixels[row - dstep][col] = m * absd;  /* scale the difference */
        }
    }
}



static unsigned long
totalBrightness(sample **    const pixels,
                unsigned int const hsampleCt,
                unsigned int const startRow,
                float        const dy) {
/*----------------------------------------------------------------------------
   Total brightness of samples in the line that goes from the left edge
   of Row 'startRow' of 'pixels' down to the right at 'dy' rows per column.

   Note that 'dy' can be negative.

   Assume that whatever 'dy' is, the sloping line thus described remains
   within 'pixels'.
-----------------------------------------------------------------------------*/
    unsigned long total;
    unsigned int x;
    float y;
        /* The vertical location in the 'pixels' region of the currently
           analyzed pixel.  0 is the top of the image.  Note that while
           'pixels' is discrete pixels, this is a location in continuous
           space.  We consider the brightness of the pixel at 1.5 to be
           the mean of the brightness of the pixel in row 1 and the pixel
           in row 2.
        */
    for (x = 0, y = startRow + 0.5, total = 0;
         x < hsampleCt;
         ++x, y += dy) {

        assert(y >= 0.0);  /* Because of entry conditions */

        total += pixels[(unsigned)y][x];
    }
    return total;
}



static void
scoreAngleRegion(sample **    const pixels,
                 unsigned int const hsampleCt,
                 unsigned int const startRow,
                 unsigned int const endRow,
                 unsigned int const vstep,
                 float        const dy,
                 float *      const scoreP) {
/*----------------------------------------------------------------------------
   Same as scoreAngle(), but we look only at the region from 'startRow'
   to 'endRow', which we assume is enough inside the image that tilted
   lines starting at the left within that region don't run off the top
   or bottom of the image.

   We assume 'startRow' and 'endRow' are within the image and denote at
   least one row.

   Instead of a tilt angle, we have 'dy', the slope (downward) of the lines
   in our assumed tilt.
-----------------------------------------------------------------------------*/
    double const tscale  = 1.0 / hsampleCt;

    unsigned int row;
    double sum;
        /* Sum of brightness measure of all sampled lines which are
           (assuming tilt) contained within image.
        */
    unsigned int n;
        /* Number of lines that went into 'total' */

    for (row = startRow, sum = 0.0, n = 0; row < endRow; row += vstep) {
        double const dt =
            tscale * totalBrightness(pixels, hsampleCt, row, dy);
            /* mean brightness of the samples in the line */

        sum += dt * dt;
        n += 1;
    }
    assert(n > 0);  /* Because we assume there's at least one row */
    *scoreP = sum / n;
}



static void
scoreAngle(const struct pam * const pamP,
           sample **          const pixels,
           unsigned int       const hstep,
           unsigned int       const vstep,
           unsigned int       const hsampleCt,
           float              const angle,
           float *            const scoreP) {
/*----------------------------------------------------------------------------
  Calculate the score for assuming the image described by *pamP and 'pixels'
  is tilted down by angle 'angle' (in degrees) from what it should be.
  I.e. the angle from the top edge of the paper to the lines of text in the
  image is 'angle'.  Positive means the text slopes down; negative means it
  slopes up.

  The score is a measure of how bright the lines of the image are if
  we assume this angle.  If the angle is right, there should be lots
  of lines that are all white, because they are between lines of text.
  If the angle is wrong, many lines will cross over lines of text and
  thus be less than all white.  A higher score indicates 'angle' is more
  likely to be the angle at which the image is tilted.

  If 'angle' is so great that not a single line goes all the way across the
  page without running off the top or bottom, we call the score -1.  In
  every other case, it is nonnegative.

  'pixels' is NOT all the pixels in the image; it is just a sampling.
  In each row, it contains only 'hsampleCt' pixels, sampled from the
  image at intervals of 'hstep' pixels.  E.g if the image is 1000
  pixels wide, pixels might be only 10 pixels wide, containing columns
  0, 100, 200, etc. of the image.

  Return the score as *scoreP.
-----------------------------------------------------------------------------*/
    float  const radians = (float)angle/360 * 2 * M_PI;
    float  const dy      = hstep * tan(radians);
        /* How much a line sinks because of the tilt when we move one sample
           ('hstep' columns of the image) to the right.
        */
    if (fabs(dy * hsampleCt) > pamP->height) {
        /* This is so tilted that not a single line of the image fits
           entirely on the page, so we can't do the measurement.
        */
        *scoreP = -1.0;
    } else {
        unsigned int startRow, endRow;

        if (dy > 0) {
            /* Lines of image drop as you go right, so the bottommost lines go
               off the page and we can't follow them.
            */
            startRow = 0;
            endRow = (unsigned int)(pamP->height - dy * hsampleCt + 0.5);
        } else {
            /* Lines of image rise as you go right, so the topmost lines go
               off the page and we can't follow them.
            */
            startRow = (unsigned int)(0 - dy * hsampleCt + 0.5);
            endRow = pamP->height;
        }
        assert(endRow > startRow);  /* because of 'if (fabs(dy ...' */

        scoreAngleRegion(pixels, hsampleCt, startRow, endRow, vstep, dy,
                         scoreP);
    }
}



static void
getBestAngleLocal(
    const struct pam * const pamP,
    sample **          const pixels,
    unsigned int       const hstep,
    unsigned int       const vstep,
    unsigned int       const hsampleCt,
    float              const minangle,
    float              const maxangle,
    float              const incr,
    bool               const verbose,
    float *            const bestAngleP,
    float *            const qualityP) {
/*----------------------------------------------------------------------------
  Find the angle that gives the highest score within the range
  [minangle, maxangle].  Those are in degrees, with positive meaning
  the subject is rotated clockwise on the page (so a horizontal line in
  the subject would slope down across the page).
-----------------------------------------------------------------------------*/
    int const nsamples = ((maxangle - minangle) / incr + 1.5);

    float score;
    float quality;  /* signal/noise ratio of the best angle */
    float angle;
    float bestangle;
    float bestscore;
    float total;
    float others;
    float * results;
    int i;

    MALLOCARRAY_NOFAIL(results, nsamples);      /* allocate array of results */

    /* try all angles within the given range, stepping by incr */
    bestangle = minangle;
    bestscore = 0;
    total = 0;
    for (i = 0; i < nsamples; i++) {
        angle = minangle + i * incr;
        scoreAngle(pamP, pixels, hstep, vstep, hsampleCt, angle, &score);
        results[i] = score;
        if (score > bestscore ||
            (score == bestscore && fabs(angle) < fabs(bestangle))) {
            bestscore = score;
            bestangle = angle;
        }
        total += score;
    }

    others = (total-bestscore) / (nsamples-1);  /* get mean of other scores */
    quality = bestscore / others;

    if (verbose) {
        fprintf(stderr,
                "\n%2d angles from %6.2f to %5.2f by %4.2f:  "
                "best = %6.2f, S:N = %4.2f\n",
                nsamples, minangle, maxangle, incr, bestangle, quality);
        for (i = 0; i < nsamples; ++i) {
            float const angle = minangle + i * incr;
            int const n = (int) (BARLENGTH * results[i] / bestscore + 0.5);
            fprintf(stderr, "%6.2f: %8.2f %0*d\n", angle, results[i], n, 0);
        }
    }
    free(results);

    *qualityP   = quality;
    *bestAngleP = bestangle;
}



static void
readSampledPixels(const char *   const inputFilename,
                  unsigned int   const hstepReq,
                  unsigned int   const vstepReq,
                  unsigned int * const hstepP,
                  unsigned int * const vstepP,
                  sample ***     const pixelsP,
                  struct pam *   const pamP,
                  unsigned int * const hsampleCtP) {
/*----------------------------------------------------------------------------
  Read the image.

  Sample the image and return the selected pixels as *pixelsP, which is
  an array the same height as the image, but with only certain columns
  sampled.  Return as *hsampleCtP the number of columns sampled (i.e. the
  width of the *pixelsP array).

  Return the horizontal step size used in the sampling as *hstepP.
  Return the appropriate vertical step size as *vstepP.
-----------------------------------------------------------------------------*/
    FILE * ifP;
    unsigned int hstep;
    unsigned int vstep;

    ifP = pm_openr(inputFilename);

    pnm_readpaminit(ifP, pamP, PAM_STRUCT_SIZE(tuple_type));

    computeSteps(pamP, hstepReq, vstepReq, &hstep, &vstep);

    load(pamP, hstep, pixelsP, hsampleCtP);

    *hstepP = hstep;
    *vstepP = vstep;

    pm_close(ifP);
}



static void
getAngle(const struct pam * const pamP,
         sample **          const pixels,
         unsigned int       const hstep,
         unsigned int       const vstep,
         unsigned int       const hsampleCt,
         float              const maxangle,
         float              const astep,
         float              const qmin,
         bool               const fast,
         bool               const verbose,
         float *            const angleP) {

    float a;
    float da;
    float lastq;        /* quality (s/n ratio) of last measurement */

    getBestAngleLocal(pamP, pixels, hstep, vstep, hsampleCt,
                      -maxangle, maxangle, astep, verbose,
                      &a, &lastq);

    if ((a < -maxangle + astep / 2) || (a > maxangle - astep / 2))
        /* extreme val almost certainly wrong */
        abandon();
    if (lastq < qmin)
        /* insufficient s/n ratio */
        abandon();

    /* make a finer search in the neighborhood */
    da = astep / 10;
    getBestAngleLocal(pamP, pixels, hstep, vstep, hsampleCt,
                      a - 9 * da, a + 9 * da, da, verbose,
                      &a, &lastq);

    /* iterate once more unless we don't need that much accuracy */
    if (!fast) {
        da /= 10;
        getBestAngleLocal(pamP, pixels, hstep, vstep, hsampleCt,
                          a - 9 * da, a + 9 * da, da, verbose,
                          &a, &lastq);
    }
    *angleP = a;
}



int
main(int argc, const char ** argv) {

    struct cmdlineInfo cmdline;
    struct pam pam;
    sample ** pixels;       /* pixel data */
    unsigned int hsampleCt; /* number of horizontal samples used */
    unsigned int hstep;    /* horizontal step size */
    unsigned int vstep;    /* vertical step size */
    float angle;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    readSampledPixels(cmdline.inputFilename, cmdline.hstep, cmdline.vstep,
                      &hstep, &vstep, &pixels, &pam, &hsampleCt);

    replacePixelValuesWithScaledDiffs(&pam, pixels, hsampleCt, cmdline.dstep);

    getAngle(&pam, pixels, hstep, vstep, hsampleCt,
             cmdline.maxangle, cmdline.astep, cmdline.qmin,
             cmdline.fast, cmdline.verbose, &angle);

    /* report the result on stdout */
    printf("%.2f\n", angle);

    freePixels(pixels, pam.height);

    return 0;
}



