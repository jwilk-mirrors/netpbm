/*

        Fractal forgery generator for the PPM toolkit

    Originally  designed  and  implemented  in December of 1989 by
    John Walker as a stand-alone program for the  Sun  and  MS-DOS
    under  Turbo C.  Adapted in September of 1991 for use with Jef
        Poskanzer's raster toolkit.

    References cited in the comments are:

        Foley, J. D., and Van Dam, A., Fundamentals of Interactive
        Computer  Graphics,  Reading,  Massachusetts:  Addison
        Wesley, 1984.

        Peitgen, H.-O., and Saupe, D. eds., The Science Of Fractal
        Images, New York: Springer Verlag, 1988.

        Press, W. H., Flannery, B. P., Teukolsky, S. A., Vetterling,
        W. T., Numerical Recipes In C, New Rochelle: Cambridge
        University Press, 1988.

    Author:
        John Walker
        http://www.fourmilab.ch/

    Permission  to  use, copy, modify, and distribute this software and
    its documentation  for  any  purpose  and  without  fee  is  hereby
    granted,  without any conditions or restrictions.  This software is
    provided "as is" without express or implied warranty.

*/

#define _XOPEN_SOURCE 500  /* get M_PI in math.h */

#include <math.h>
#include <float.h>
#include <assert.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "rand.h"
#include "shhopt.h"
#include "ppm.h"

static double const hugeVal = 1e50;

/* Definitions used to address real and imaginary parts in a two-dimensional
   array of complex numbers as stored by fourn(). */

#define Real(v, x, y)  v[1 + (((x) * meshsize) + (y)) * 2]
#define Imag(v, x, y)  v[2 + (((x) * meshsize) + (y)) * 2]

/* Coordinate indices within arrays. */

typedef struct {
    double x;
    double y;
    double z;
} Vector;

/* Definition for obtaining random numbers. */

/*  Local variables  */

static double fracdim;            /* Fractal dimension */
static double powscale;           /* Power law scaling exponent */
static int meshsize = 256;        /* FFT mesh size */
static double inclangle, hourangle;   /* Star position relative to planet */
static bool inclspec = FALSE;      /* No inclination specified yet */
static bool hourspec = FALSE;      /* No hour specified yet */
static double icelevel;           /* Ice cap threshold */
static double glaciers;           /* Glacier level */
static int starfraction;          /* Star fraction */
static int starcolor;            /* Star color saturation */


struct CmdlineInfo {
    unsigned int clouds;
    unsigned int night;
    float        dimension;
    float        hourAngle;
    unsigned int hourSpec;
    float        inclAngle;
    unsigned int inclinationSpec;
    unsigned int meshSize;
    unsigned int meshSpec;
    float        power;
    float        glaciers;
    float        ice;
    int          saturation;
    unsigned int seed;
    unsigned int seedSpec;
    int          stars;
    unsigned int starsSpec;
    unsigned int width;
    unsigned int height;
};



static void
parseCommandLine(int argc, const char **argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
  Convert program invocation arguments (argc,argv) into a format the
  program can use easily, struct cmdlineInfo.  Validate arguments along
  the way and exit program with message if invalid.

  Note that some string information we return as *cmdlineP is in the storage
  argv[] points to.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int dimensionSpec, seedSpec,
        meshSpec, powerSpec, glaciersSpec, iceSpec, saturationSpec,
        starsSpec, widthSpec, heightSpec;
    float hour;
    float inclination;
    unsigned int mesh;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;
    OPTENT3(0, "clouds",      OPT_FLAG, NULL, &cmdlineP->clouds, 0);
    OPTENT3(0, "night",       OPT_FLAG, NULL, &cmdlineP->night, 0);
    OPTENT3(0, "dimension",   OPT_FLOAT, &cmdlineP->dimension,
            &dimensionSpec, 0);
    OPTENT3(0, "hour",        OPT_FLOAT, &hour,
            &cmdlineP->hourSpec, 0);
    OPTENT3(0, "inclination", OPT_FLOAT, &inclination,
            &cmdlineP->inclinationSpec, 0);
    OPTENT3(0, "tilt",        OPT_FLOAT, &inclination,
            &cmdlineP->inclinationSpec, 0);
    OPTENT3(0, "mesh",        OPT_UINT, &mesh,
            &meshSpec, 0);
    OPTENT3(0, "power",       OPT_FLOAT, &cmdlineP->power,
            &powerSpec, 0);
    OPTENT3(0, "glaciers",    OPT_FLOAT, &cmdlineP->glaciers,
            &glaciersSpec, 0);
    OPTENT3(0, "ice",         OPT_FLOAT, &cmdlineP->ice,
            &iceSpec, 0);
    OPTENT3(0, "saturation",  OPT_INT,   &cmdlineP->saturation,
            &saturationSpec, 0);
    OPTENT3(0, "seed",        OPT_UINT,  &cmdlineP->seed,
            &seedSpec, 0);
    OPTENT3(0, "stars",       OPT_INT,   &cmdlineP->stars,
            &starsSpec, 0);
    OPTENT3(0, "width",       OPT_UINT,  &cmdlineP->width,
            &widthSpec, 0);
    OPTENT3(0, "xsize",       OPT_UINT,  &cmdlineP->width,
            &widthSpec, 0);
    OPTENT3(0, "height",      OPT_UINT,  &cmdlineP->height,
            &heightSpec, 0);
    OPTENT3(0, "ysize",       OPT_UINT,  &cmdlineP->height,
            &heightSpec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (dimensionSpec) {
        if (cmdlineP->dimension <= 0.0)
            pm_error("-dimension must be greater than zero.  "
                     "You specified %f", cmdlineP->dimension);
        else if (cmdlineP->dimension > 5.0 + FLT_EPSILON)
            pm_error("-dimension must not be greater than 5.  "
                     "Results are not interesting with higher numbers, so "
                     "we assume it is a mistake.  "
                     "You specified %f", cmdlineP->dimension);
    } else
        cmdlineP->dimension = cmdlineP->clouds ? 2.15 : 2.4;

    if (cmdlineP->hourSpec)
        cmdlineP->hourAngle = (M_PI / 12.0) * (hour + 12.0);

    if (cmdlineP->inclinationSpec)
        cmdlineP->inclAngle = (M_PI / 180.0) * inclination;

    if (meshSpec) {
        unsigned int i;
        if (mesh < 2)
            pm_error("-mesh value must be at least 2.  "
                     "You specified %u", mesh);
        /* Force FFT mesh to the next larger power of 2. */
        for (i = 2; i < mesh; i <<= 1);
        cmdlineP->meshSize = i;
    } else
        cmdlineP->meshSize = 256;

    if (powerSpec) {
        if (cmdlineP->power <= 0.0)
            pm_error("-power must be greater than zero.  "
                     "You specified %f", cmdlineP->power);
    } else
        cmdlineP->power = cmdlineP->clouds ? 0.75 : 1.2;

    if (iceSpec) {
        if (cmdlineP->ice <= 0.0)
            pm_error("-ice must be greater than zero.  "
                     "You specified %f", cmdlineP->ice);
    } else
        cmdlineP->ice = 0.4;

    if (glaciersSpec) {
        if (cmdlineP->glaciers <= 0.0)
            pm_error("-glaciers must be greater than 0.  "
                     "You specified %f", cmdlineP->glaciers);
    } else
        cmdlineP->glaciers = 0.75;

    if (!starsSpec)
        cmdlineP->stars = 100;

    if (!saturationSpec)
        cmdlineP->saturation = 125;

    if (!seedSpec)
        cmdlineP->seed = pm_randseed();

    if (!widthSpec)
        cmdlineP->width = 256;

    if (!heightSpec)
        cmdlineP->height = 256;

    if (argc-1 > 0)
        pm_error("There are no non-option arguments.  "
                 "You specified %u", argc-1);

    free(option_def);
}



static void
fourn(double *    const data,
      const int * const nn,
      int         const ndim,
      int         const isign) {
/*----------------------------------------------------------------------------
    Multi-dimensional fast Fourier transform

    Arguments:

       data       A  one-dimensional  array  of  floats  (NOTE!!!   NOT
              DOUBLES!!), indexed from one (NOTE!!!   NOT  ZERO!!),
              containing  pairs of numbers representing the complex
              valued samples.  The Fourier transformed results  are
              returned in the same array.

       nn         An  array specifying the edge size in each dimension.
              THIS ARRAY IS INDEXED FROM  ONE,  AND  ALL  THE  EDGE
              SIZES MUST BE POWERS OF TWO!!!

       ndim       Number of dimensions of FFT to perform.  Set to 2 for
              two dimensional FFT.

       isign      If 1, a Fourier transform is done; if -1 the  inverse
              transformation is performed.

        This  function  is essentially as given in Press et al., "Numerical
        Recipes In C", Section 12.11, pp.  467-470.
-----------------------------------------------------------------------------*/
    int i1, i2, i3;
    int i2rev, i3rev, ip1, ip2, ip3, ifp1, ifp2;
    int ibit, idim, k1, k2, n, nprev, nrem, ntot;
    double tempi, tempr;
    double theta, wi, wpi, wpr, wr, wtemp;

#define SWAP(a,b) tempr=(a); (a) = (b); (b) = tempr

    ntot = 1;
    for (idim = 1; idim <= ndim; idim++)
        ntot *= nn[idim];
    nprev = 1;
    for (idim = ndim; idim >= 1; idim--) {
        n = nn[idim];
        nrem = ntot / (n * nprev);
        ip1 = nprev << 1;
        ip2 = ip1 * n;
        ip3 = ip2 * nrem;
        i2rev = 1;
        for (i2 = 1; i2 <= ip2; i2 += ip1) {
            if (i2 < i2rev) {
                for (i1 = i2; i1 <= i2 + ip1 - 2; i1 += 2) {
                    for (i3 = i1; i3 <= ip3; i3 += ip2) {
                        i3rev = i2rev + i3 - i2;
                        SWAP(data[i3], data[i3rev]);
                        SWAP(data[i3 + 1], data[i3rev + 1]);
                    }
                }
            }
            ibit = ip2 >> 1;
            while (ibit >= ip1 && i2rev > ibit) {
                i2rev -= ibit;
                ibit >>= 1;
            }
            i2rev += ibit;
        }
        ifp1 = ip1;
        while (ifp1 < ip2) {
            ifp2 = ifp1 << 1;
            theta = isign * (M_PI * 2) / (ifp2 / ip1);
            wtemp = sin(0.5 * theta);
            wpr = -2.0 * wtemp * wtemp;
            wpi = sin(theta);
            wr = 1.0;
            wi = 0.0;
            for (i3 = 1; i3 <= ifp1; i3 += ip1) {
                for (i1 = i3; i1 <= i3 + ip1 - 2; i1 += 2) {
                    for (i2 = i1; i2 <= ip3; i2 += ifp2) {
                        k1 = i2;
                        k2 = k1 + ifp1;
                        tempr = wr * data[k2] - wi * data[k2 + 1];
                        tempi = wr * data[k2 + 1] + wi * data[k2];
                        data[k2] = data[k1] - tempr;
                        data[k2 + 1] = data[k1 + 1] - tempi;
                        data[k1] += tempr;
                        data[k1 + 1] += tempi;
                    }
                }
                wr = (wtemp = wr) * wpr - wi * wpi + wr;
                wi = wi * wpr + wtemp * wpi + wi;
            }
            ifp1 = ifp2;
        }
        nprev *= n;
    }
}



static double
cast(double             const low,
     double             const high,
     struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
   A random number in the range ['low', 'high'].
-----------------------------------------------------------------------------*/
    return (low + (high - low) * pm_drand(randStP));

}



static double
randPhase(struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
   A random number in the range [0, 2 * PI).
-----------------------------------------------------------------------------*/
     return (2.0 * M_PI * pm_drand1(randStP));

}



static void
spectralsynth(double **          const aP,
              unsigned int       const n,
              double             const h,
              struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
  Spectrally synthesized fractal motion in two dimensions.

  This algorithm is given under the name SpectralSynthesisFM2D on page 108 of
  Peitgen & Saupe.
-----------------------------------------------------------------------------*/
    unsigned int const bl = ((((unsigned long) n) * n) + 1) * 2;

    int i, j, i0, j0, nsize[3];
    double rad, phase, rcos, rsin;
    double * a;

    MALLOCARRAY(a, bl);

    if (!a) {
        pm_error("Cannot allocate %u x %u result array (%u doubles).",
                 n, n, bl);
    }
    for (i = 0; i < bl; ++i)
        a[i] = 0.0;  /* initial value */

    for (i = 0; i <= n / 2; i++) {
        for (j = 0; j <= n / 2; j++) {
            phase = randPhase(randStP);
            if (i != 0 || j != 0) {
                rad = pow( (double) (i*i + j*j), - (h + 1) / 2) * pm_gaussrand(randStP);
            } else {
                rad = 0;
            }
            rcos = rad * cos(phase);
            rsin = rad * sin(phase);
            Real(a, i, j) = rcos;
            Imag(a, i, j) = rsin;
            i0 = (i == 0) ? 0 : n - i;
            j0 = (j == 0) ? 0 : n - j;
            Real(a, i0, j0) = rcos;
            Imag(a, i0, j0) = - rsin;
        }
    }
    Imag(a, n / 2, 0) = 0;
    Imag(a, 0, n / 2) = 0;
    Imag(a, n / 2, n / 2) = 0;
    for (i = 1; i <= n / 2 - 1; i++) {
        for (j = 1; j <= n / 2 - 1; j++) {
            phase = randPhase(randStP);
            rad = pow((double) (i * i + j * j), -(h + 1) / 2) * pm_gaussrand(randStP);
            rcos = rad * cos(phase);
            rsin = rad * sin(phase);
            Real(a, i, n - j) = rcos;
            Imag(a, i, n - j) = rsin;
            Real(a, n - i, j) = rcos;
            Imag(a, n - i, j) = - rsin;
        }
    }

    nsize[0] = 0;
    nsize[1] = nsize[2] = n;          /* Dimension of frequency domain array */
    fourn(a, nsize, 2, -1);       /* Take inverse 2D Fourier transform */

    *aP = a;
}



static void
temprgb(double   const temp,
        double * const r,
        double * const g,
        double * const b) {
/*----------------------------------------------------------------------------
  Calculate the relative R, G, and B components for a black body emitting
  light at a given temperature.  We solve the Planck radiation equation
  directly for the R, G, and B wavelengths defined for the CIE 1931 Standard
  Colorimetric Observer.  The color temperature is specified in kelvins.
-----------------------------------------------------------------------------*/
    double const c1 = 3.7403e10;
    double const c2 = 14384.0;
    double er, eg, eb, es;

/* Lambda is the wavelength in microns: 5500 angstroms is 0.55 microns. */

#define Planck(lambda)  ((c1 * pow((double) lambda, -5.0)) /  \
                         (pow(M_E, c2 / (lambda * temp)) - 1))

        er = Planck(0.7000);
        eg = Planck(0.5461);
        eb = Planck(0.4358);
#undef Planck

        es = 1.0 / MAX(er, MAX(eg, eb));

        *r = er * es;
        *g = eg * es;
        *b = eb * es;
}



static void
etoile(pixel *        const pix,
       struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
    Set a pixel in the starry sky.
-----------------------------------------------------------------------------*/
    if ((pm_rand(randStP) % 1000) < starfraction) {
        double const starQuality   = 0.5;
            /* Brightness distribution exponent */
        double const starIntensity = 8;
            /* Brightness scale factor */
        double const starTintExp = 0.5;
            /* Tint distribution exponent */
        double const v =
            MIN(255.0,
                starIntensity * pow(1 / (1 - cast(0, 0.9999, randStP)),
                                    (double) starQuality));

        /* We make a special case for star color  of zero in order to
           prevent  floating  point  roundoff  which  would  otherwise
           result  in  more  than  256 star colors.  We can guarantee
           that if you specify no star color, you never get more than
           256 shades in the image. */

        if (starcolor == 0) {
            pixval const vi = v;

            PPM_ASSIGN(*pix, vi, vi, vi);
        } else {
            double temp;
            double r, g, b;

            temp = 5500 + starcolor *
                pow(1 / (1 - cast(0, 0.9999, randStP)), starTintExp) *
                ((pm_rand(randStP) & 7) ? -1 : 1);

            /* Constrain temperature to a reasonable value: >= 2600K
               (S Cephei/R Andromedae), <= 28,000 (Spica). */
            temp = MAX(2600, MIN(28000, temp));
            temprgb(temp, &r, &g, &b);
            PPM_ASSIGN(*pix, (int) (r * v + 0.499),
                       (int) (g * v + 0.499),
                       (int) (b * v + 0.499));
        }
    } else {
        PPM_ASSIGN(*pix, 0, 0, 0);
    }
}



static double
uprj(unsigned int const a,
     unsigned int const size) {

    return (double)a/(size-1);
}



static double
atSat(double const x,
      double const y,
      double const dsat) {

    return x*(1.0-dsat) + y*dsat;
}



static unsigned char *
makeCp(const double * const a,
       unsigned int   const n,
       pixval         const maxval) {

    /* Prescale the grid points into intensities. */

    unsigned char * cp;
    unsigned char * ap;

    if (UINT_MAX / n < n)
        pm_error("arithmetic overflow squaring %u", n);
    cp = malloc(n * n);
    if (cp == NULL)
        pm_error("Unable to allocate %u bytes for cp array", n);

    ap = cp;  /* initial value */
    {
        unsigned int i;
        for (i = 0; i < n; i++) {
            unsigned int j;
            for (j = 0; j < n; j++)
                *ap++ = ((double)maxval * (Real(a, i, j) + 1.0)) / 2.0;
        }
    }
    return cp;
}



static void
createPlanetStuff(bool               const clouds,
                  const double *     const a,
                  unsigned int       const n,
                  double **          const uP,
                  double **          const u1P,
                  unsigned int **    const bxfP,
                  unsigned int **    const bxcP,
                  unsigned char **   const cpP,
                  Vector *           const sunvecP,
                  unsigned int       const cols,
                  pixval             const maxval,
                  struct pm_randSt * const randStP) {

    double *u, *u1;
    unsigned int *bxf, *bxc;
    unsigned char * cp;
    double shang, siang;
    bool flipped;

    /* Compute incident light direction vector. */

    shang = hourspec ? hourangle : randPhase(randStP);
    siang = inclspec ? inclangle : cast(-M_PI * 0.12, M_PI * 0.12, randStP);

    sunvecP->x = sin(shang) * cos(siang);
    sunvecP->y = sin(siang);
    sunvecP->z = cos(shang) * cos(siang);  /* initial value */

    /* Allow only 25% of random pictures to be crescents */

    if (!hourspec && ((pm_rand(randStP) % 100) < 75)) {
        flipped = (sunvecP->z < 0);
        sunvecP->z = fabs(sunvecP->z);
    } else
        flipped = FALSE;

    if (!clouds) {
        pm_message(
            "        -inclination %.0f -hour %d -ice %.2f -glaciers %.2f",
            (siang * (180.0 / M_PI)),
            (int) (((shang * (12.0 / M_PI)) + 12 +
                    (flipped ? 12 : 0)) + 0.5) % 24,
            icelevel,
            glaciers);
        pm_message("        -stars %d -saturation %d.",
                   starfraction, starcolor);
    }

    cp = makeCp(a, n, maxval);

    /* Fill the screen from the computed  intensity  grid  by  mapping
       screen  points onto the grid, then calculating each pixel value
       by bilinear interpolation from  the  surrounding  grid  points.
       (N.b. the pictures would undoubtedly look better when generated
       with small grids if  a  more  well-behaved  interpolation  were
       used.)

       Also compute the line-level interpolation parameters that
       caller will need every time around his inner loop.
    */

    MALLOCARRAY(u,   cols);
    MALLOCARRAY(u1,  cols);
    MALLOCARRAY(bxf, cols);
    MALLOCARRAY(bxc, cols);

    if (u == NULL || u1 == NULL || bxf == NULL || bxc == NULL)
        pm_error("Cannot allocate %u element interpolation tables.", cols);
    {
        unsigned int j;
        for (j = 0; j < cols; ++j) {
            double const bx = (n - 1) * uprj(j, cols);

            bxf[j] = floor(bx);
            bxc[j] = MIN(bxf[j] + 1, n - 1);
            u[j] = bx - bxf[j];
            u1[j] = 1 - u[j];
        }
    }
    *uP   = u;  *u1P  = u1;
    *bxfP = bxf; *bxcP = bxc;
    *cpP  = cp;
}



static void
generateStarrySkyRow(pixel *            const pixels,
                     unsigned int       const cols,
                     struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
  Generate a starry sky.  Note  that no FFT is performed;
  the output is  generated  directly  from  a  power  law
  mapping  of  a  pseudorandom sequence into intensities.
-----------------------------------------------------------------------------*/
    unsigned int j;

    for (j = 0; j < cols; ++j)
        etoile(pixels + j, randStP);
}



static void
generateCloudRow(pixel *         const pixels,
                 unsigned int    const cols,
                 double          const t,
                 double          const t1,
                 double *        const u,
                 double *        const u1,
                 unsigned char * const cp,
                 unsigned int *  const bxc,
                 unsigned int *  const bxf,
                 int             const byc,
                 int             const byf,
                 pixval          const maxval) {

    /* Render the FFT output as clouds. */

    unsigned int col;

    for (col = 0; col < cols; ++col) {
        double r;
        pixval w;

        r = 0.0;  /* initial value */
        /* Note that where t1 and t are zero, the cp[] element
           referenced below does not exist.
        */
        if (t1 > 0.0)
            r += t1 * u1[col] * cp[byf + bxf[col]] +
                t1 * u[col]  * cp[byf + bxc[col]];
        if (t > 0.0)
            r += t * u1[col] * cp[byc + bxf[col]] +
                t * u[col]  * cp[byc + bxc[col]];

        w = (r > 127.0) ? (maxval * ((r - 127.0) / 128.0)) : 0;

        PPM_ASSIGN(pixels[col], w, w, maxval);
    }
}



static void
makeLand(int *  const irP,
         int *  const igP,
         int *  const ibP,
         double const r) {
/*----------------------------------------------------------------------------
  Land area.  Look up color based on elevation from precomputed
  color map table.
-----------------------------------------------------------------------------*/
    static unsigned char pgnd[][3] = {
        {206, 205, 0}, {208, 207, 0}, {211, 208, 0},
        {214, 208, 0}, {217, 208, 0}, {220, 208, 0},
        {222, 207, 0}, {225, 205, 0}, {227, 204, 0},
        {229, 202, 0}, {231, 199, 0}, {232, 197, 0},
        {233, 194, 0}, {234, 191, 0}, {234, 188, 0},
        {233, 185, 0}, {232, 183, 0}, {231, 180, 0},
        {229, 178, 0}, {227, 176, 0}, {225, 174, 0},
        {223, 172, 0}, {221, 170, 0}, {219, 168, 0},
        {216, 166, 0}, {214, 164, 0}, {212, 162, 0},
        {210, 161, 0}, {207, 159, 0}, {205, 157, 0},
        {203, 156, 0}, {200, 154, 0}, {198, 152, 0},
        {195, 151, 0}, {193, 149, 0}, {190, 148, 0},
        {188, 147, 0}, {185, 145, 0}, {183, 144, 0},
        {180, 143, 0}, {177, 141, 0}, {175, 140, 0},
        {172, 139, 0}, {169, 138, 0}, {167, 137, 0},
        {164, 136, 0}, {161, 135, 0}, {158, 134, 0},
        {156, 133, 0}, {153, 132, 0}, {150, 132, 0},
        {147, 131, 0}, {145, 130, 0}, {142, 130, 0},
        {139, 129, 0}, {136, 128, 0}, {133, 128, 0},
        {130, 127, 0}, {127, 127, 0}, {125, 127, 0},
        {122, 127, 0}, {119, 127, 0}, {116, 127, 0},
        {113, 127, 0}, {110, 128, 0}, {107, 128, 0},
        {104, 128, 0}, {102, 127, 0}, { 99, 126, 0},
        { 97, 124, 0}, { 95, 122, 0}, { 93, 120, 0},
        { 92, 117, 0}, { 92, 114, 0}, { 92, 111, 0},
        { 93, 108, 0}, { 94, 106, 0}, { 96, 104, 0},
        { 98, 102, 0}, {100, 100, 0}, {103,  99, 0},
        {106,  99, 0}, {109,  99, 0}, {111, 100, 0},
        {114, 101, 0}, {117, 102, 0}, {120, 103, 0},
        {123, 102, 0}, {125, 102, 0}, {128, 100, 0},
        {130,  98, 0}, {132,  96, 0}, {133,  94, 0},
        {134,  91, 0}, {134,  88, 0}, {134,  85, 0},
        {133,  82, 0}, {131,  80, 0}, {129,  78, 0}
    };

    unsigned int const ix = ((r - 128) * (ARRAY_SIZE(pgnd) - 1)) / 127;

    *irP = pgnd[ix][0];
    *igP = pgnd[ix][1];
    *ibP = pgnd[ix][2];
}



static void
makeWater(int *  const irP,
          int *  const igP,
          int *  const ibP,
          double const r,
          pixval const maxval) {

    /* Water.  Generate clouds above water based on elevation.  */

    *irP = *igP = r > 64 ? (r - 64) * 4 : 0;
    *ibP = maxval;
}



static void
addIce(int *  const irP,
       int *  const igP,
       int *  const ibP,
       double const r,
       double const azimuth,
       double const icelevel,
       double const glaciers,
       pixval const maxval) {

    /* Generate polar ice caps. */

    double const icet = pow(fabs(sin(azimuth)), 1.0 / icelevel) - 0.5;
    double const ice = MAX(0.0,
                           (icet + glaciers * MAX(-0.5, (r - 128) / 128.0)));
    if  (ice > 0.125) {
        *irP = maxval;
        *igP = maxval;
        *ibP = maxval;
    }
}



static void
limbDarken(int *          const irP,
           int *          const igP,
           int *          const ibP,
           unsigned int   const col,
           unsigned int   const row,
           unsigned int   const cols,
           unsigned int   const rows,
           Vector         const sunvec,
           pixval         const maxval) {

    /* With Gcc 2.95.3 compiler optimization level > 1, I have seen this
       function confuse all the variables and ultimately generate a
       completely black image.  Adding an extra reference to 'rows' seems
       to put things back in order, and the assert() below does that.
       Take it out, and the problem comes back!  04.02.21.
    */

    /* Apply limb darkening by cosine rule. */

    double const atthick  = 1.03;
    double const atSatFac = 1.0;
    double const athfac   = sqrt(atthick * atthick - 1.0);
        /* Atmosphere thickness as a percentage of planet's diameter */

    double const dy = 2 * ((double)rows/2 - row) / rows;
    double const dysq = dy * dy;
    /* Note: we are in fact normalizing this horizontal position by the
       vertical size of the picture.  And we know rows >= cols.
    */
    double const dx   = 2 * ((double)cols/2 - col) / rows;
    double const dxsq = dx * dx;

    double const ds = MIN(1.0, sqrt(dxsq + dysq));

    /* Calculate atmospheric absorption based on the thickness of
       atmosphere traversed by light on its way to the surface.
    */
    double const dsq = ds * ds;
    double const dsat = atSatFac * ((sqrt(atthick * atthick - dsq) -
                                     sqrt(1.0 * 1.0 - dsq)) / athfac);

    assert(rows >= cols);  /* An input requirement */

    *irP = atSat(*irP, maxval/2, dsat);
    *igP = atSat(*igP, maxval/2, dsat);
    *ibP = atSat(*ibP, maxval,   dsat);
    {
        double const PlanetAmbient = 0.05;

        double const sqomdysq = sqrt(1.0 - dysq);
        double const svx = sunvec.x;
        double const svy = sunvec.y * dy;
        double const svz = sunvec.z * sqomdysq;
        double const di =
            MAX(0, MIN(1.0, svx * dx + svy + svz * sqrt(1.0 - dxsq)));
        double const inx = PlanetAmbient * 1.0 + (1.0 - PlanetAmbient) * di;

        *irP *= inx;
        *igP *= inx;
        *ibP *= inx;
    }
}



static void
generatePlanetRow(pixel *            const pixelrow,
                  unsigned int       const row,
                  unsigned int       const rows,
                  unsigned int       const cols,
                  double             const t,
                  double             const t1,
                  double *           const u,
                  double *           const u1,
                  unsigned char *    const cp,
                  unsigned int *     const bxc,
                  unsigned int *     const bxf,
                  int                const byc,
                  int                const byf,
                  Vector             const sunvec,
                  pixval             const maxval,
                  struct pm_randSt * const randStP) {

    unsigned int const StarClose = 2;

    double const azimuth    = asin(((((double) row) / (rows - 1)) * 2) - 1);
    unsigned int const lcos = (rows / 2) * fabs(cos(azimuth));

    unsigned int col;

    for (col = 0; col < cols; ++col)
        PPM_ASSIGN(pixelrow[col], 0, 0, 0);

    for (col = cols/2 - lcos; col <= cols/2 + lcos; ++col) {
        double r;
        int ir, ig, ib;

        r = 0.0;   /* initial value */

        /* Note that where t1 and t are zero, the cp[] element
           referenced below does not exist.
        */
        if (t1 > 0.0)
            r += t1 * u1[col] * cp[byf + bxf[col]] +
                t1 * u[col]  * cp[byf + bxc[col]];
        if (t > 0.0)
            r += t * u1[col] * cp[byc + bxf[col]] +
                t * u[col]  * cp[byc + bxc[col]];

        if (r >= 128)
            makeLand(&ir, &ig, &ib, r);
        else
            makeWater(&ir, &ig, &ib, r, maxval);

        addIce(&ir, &ig, &ib, r, azimuth, icelevel, glaciers, maxval);

        limbDarken(&ir, &ig, &ib, col, row, cols, rows, sunvec, maxval);

        PPM_ASSIGN(pixelrow[col], ir, ig, ib);
    }

    /* Left stars */

    for (col = 0; (int)col < (int)(cols/2 - (lcos + StarClose)); ++col)
        etoile(&pixelrow[col], randStP);

    /* Right stars */

    for (col = cols/2 + (lcos + StarClose); col < cols; ++col)
        etoile(&pixelrow[col], randStP);
}



static void
genplanet(bool                const stars,
          bool                const clouds,
          const double *      const a,
          unsigned int        const cols,
          unsigned int        const rows,
          unsigned int        const n,
          unsigned int        const rseed,
          struct pm_randSt *  const randStP) {
/*----------------------------------------------------------------------------
  Generate planet from elevation array.

  If 'stars' is true, a is undefined.  Otherwise, it is defined.
-----------------------------------------------------------------------------*/
    pixval const maxval = PPM_MAXMAXVAL;

    unsigned char *cp;
    double *u, *u1;
    unsigned int *bxf, *bxc;

    pixel *pixelrow;
    unsigned int row;

    Vector sunvec;

    ppm_writeppminit(stdout, cols, rows, maxval, FALSE);

    if (stars) {
        pm_message("night: -seed %d -stars %d -saturation %d.",
                   rseed, starfraction, starcolor);
        cp = NULL;
        u = NULL; u1 = NULL;
        bxf = NULL; bxc = NULL;
    } else {
        pm_message("%s: -seed %d -dimension %.2f -power %.2f -mesh %d",
                   clouds ? "clouds" : "planet",
                   rseed, fracdim, powscale, meshsize);
        createPlanetStuff(clouds, a, n, &u, &u1, &bxf, &bxc, &cp, &sunvec,
                          cols, maxval, randStP);
    }

    pixelrow = ppm_allocrow(cols);
    for (row = 0; row < rows; ++row) {
        if (stars)
            generateStarrySkyRow(pixelrow, cols, randStP);
        else {
            double const by = (n - 1) * uprj(row, rows);
            int    const byf = floor(by) * n;
            int    const byc = byf + n;
            double const t = by - floor(by);
            double const t1 = 1 - t;

            if (clouds)
                generateCloudRow(pixelrow, cols,
                                 t, t1, u, u1, cp, bxc, bxf, byc, byf,
                                 maxval);
            else
                generatePlanetRow(pixelrow, row, rows, cols,
                                  t, t1, u, u1, cp, bxc, bxf, byc, byf,
                                  sunvec,
                                  maxval,
                                  randStP);
        }
        ppm_writeppmrow(stdout, pixelrow, cols, maxval, FALSE);
    }
    pm_close(stdout);

    ppm_freerow(pixelrow);
    if (cp)  free(cp);
    if (u)   free(u);
    if (u1)  free(u1);
    if (bxf) free(bxf);
    if (bxc) free(bxc);
}



static void
applyPowerLawScaling(double * const a,
                     int      const meshsize,
                     double   const powscale) {

    /* Apply power law scaling if non-unity scale is requested. */

    if (powscale != 1.0) {
        unsigned int i;
        for (i = 0; i < meshsize; i++) {
            unsigned int j;
            for (j = 0; j < meshsize; j++) {
                double const r = Real(a, i, j);
                if (r > 0)
                    Real(a, i, j) = MIN(hugeVal, pow(r, powscale));
            }
        }
    }
}



static void
computeExtremeReal(const double * const a,
                   int            const meshsize,
                   double *       const rminP,
                   double *       const rmaxP) {

    /* Compute extrema for autoscaling. */

    double rmin, rmax;
    unsigned int i;

    rmin = hugeVal;
    rmax = -hugeVal;

    for (i = 0; i < meshsize; i++) {
        unsigned int j;
        for (j = 0; j < meshsize; j++) {
            double r = Real(a, i, j);

            rmin = MIN(rmin, r);
            rmax = MAX(rmax, r);
        }
    }
    *rminP = rmin;
    *rmaxP = rmax;
}



static void
replaceWithSpread(double * const a,
                  int      const meshsize) {
/*----------------------------------------------------------------------------
  Replace the real part of each element of the 'a' array with a
  measure of how far the real is from the middle; sort of a standard
  deviation.
-----------------------------------------------------------------------------*/
    double rmin, rmax;
    double rmean, rrange;
    unsigned int i;

    computeExtremeReal(a, meshsize, &rmin, &rmax);

    rmean = (rmin + rmax) / 2;
    rrange = (rmax - rmin) / 2;

    for (i = 0; i < meshsize; i++) {
        unsigned int j;
        for (j = 0; j < meshsize; j++) {
            Real(a, i, j) = (Real(a, i, j) - rmean) / rrange;
        }
    }
}



static bool
planet(unsigned int const cols,
       unsigned int const rows,
       bool         const stars,
       bool         const clouds,
       unsigned int const rseed) {
/*----------------------------------------------------------------------------
   Make a planet.
-----------------------------------------------------------------------------*/
    double * a;
    bool error;
    struct pm_randSt randSt;

    pm_randinit(&randSt);
    pm_srand(&randSt, rseed);

    if (stars) {
        a = NULL;
        error = FALSE;
    } else {
        spectralsynth(&a, meshsize, 3.0 - fracdim, &randSt);
        if (!a) {
            error = TRUE;
        } else {
            applyPowerLawScaling(a, meshsize, powscale);

            replaceWithSpread(a, meshsize);

            error = FALSE;
        }
    }
    if (!error)
        genplanet(stars, clouds, a, cols, rows, meshsize, rseed, &randSt);

    if (a != NULL)
        free(a);

    return !error;
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    bool success;

    unsigned int cols, rows;     /* Dimensions of our output image */

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    fracdim      = cmdline.dimension;
    hourspec     = cmdline.hourSpec;
    hourangle    = cmdline.hourAngle;
    inclspec     = cmdline.inclinationSpec;
    inclangle    = cmdline.inclAngle;
    meshsize     = cmdline.meshSize;
    powscale     = cmdline.power;
    icelevel     = cmdline.ice;
    glaciers     = cmdline.glaciers;
    starfraction = cmdline.stars;
    starcolor    = cmdline.saturation;

    /* Force  screen to be at least  as wide as it is high.  Long,
       skinny screens  cause  crashes  because  picture  width  is
       calculated based on height.
    */

    cols = (MAX(cmdline.height, cmdline.width) + 1) & (~1);
    rows = cmdline.height;

    success = planet(cols, rows, cmdline.night,
                     cmdline.clouds, cmdline.seed);

    exit(success ? 0 : 1);
}



