#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <float.h>

#include "netpbm/pm.h"
#include "netpbm/nstring.h"

#include "jasper/jas_string.h"
#include "jasper/jas_malloc.h"
#include "jasper/jas_image.h"
#include "jasper/jas_tvp.h"
#include "jasper/jas_version.h"
#include "jasper/jas_math.h"
#include "jasper/jas_debug.h"

#include "jpc_flt.h"
#include "jpc_fix.h"
#include "jpc_tagtree.h"
#include "jpc_enc.h"
#include "jpc_cs.h"
#include "jpc_mct.h"
#include "jpc_tsfb.h"
#include "jpc_qmfb.h"
#include "jpc_t1enc.h"
#include "jpc_t2enc.h"
#include "jpc_cod.h"
#include "jpc_math.h"
#include "jpc_util.h"

/*****************************************************************************\
*
\*****************************************************************************/

#define JPC_POW2(n) \
    (1 << (n))

#define JPC_FLOORTOMULTPOW2(x, n) \
  (((n) > 0) ? ((x) & (~((1 << n) - 1))) : (x))
/* Round to the nearest multiple of the specified power of two in the
  direction of negative infinity. */

#define JPC_CEILTOMULTPOW2(x, n) \
  (((n) > 0) ? JPC_FLOORTOMULTPOW2(((x) + (1 << (n)) - 1), n) : (x))
/* Round to the nearest multiple of the specified power of two in the
  direction of positive infinity. */

#define JPC_POW2(n) \
  (1 << (n))

jpc_enc_tile_t *jpc_enc_tile_create(jpc_enc_cp_t *cp, jas_image_t *image, int tileno);
void jpc_enc_tile_destroy(jpc_enc_tile_t *tile);

static jpc_enc_tcmpt_t *tcmpt_create(jpc_enc_tcmpt_t *tcmpt, jpc_enc_cp_t *cp,
  jas_image_t *image, jpc_enc_tile_t *tile);
static void tcmpt_destroy(jpc_enc_tcmpt_t *tcmpt);
static jpc_enc_rlvl_t *rlvl_create(jpc_enc_rlvl_t *rlvl, jpc_enc_cp_t *cp,
  jpc_enc_tcmpt_t *tcmpt, jpc_tsfb_band_t *bandinfos);
static void rlvl_destroy(jpc_enc_rlvl_t *rlvl);
static jpc_enc_band_t *band_create(jpc_enc_band_t *band, jpc_enc_cp_t *cp,
  jpc_enc_rlvl_t *rlvl, jpc_tsfb_band_t *bandinfos);
static void band_destroy(jpc_enc_band_t *bands);
static jpc_enc_prc_t *prc_create(jpc_enc_prc_t *prc, jpc_enc_cp_t *cp,
  jpc_enc_band_t *band);
static void prc_destroy(jpc_enc_prc_t *prcs);
static jpc_enc_cblk_t *cblk_create(jpc_enc_cblk_t *cblk, jpc_enc_cp_t *cp,
  jpc_enc_prc_t *prc);
static void cblk_destroy(jpc_enc_cblk_t *cblks);
static void pass_destroy(jpc_enc_pass_t *pass);
void jpc_enc_dump(jpc_enc_t *enc);

/*****************************************************************************\
* Local prototypes.
\*****************************************************************************/

void quantize(jas_matrix_t *data, jpc_fix_t stepsize);
jpc_enc_t *jpc_enc_create(jpc_enc_cp_t *cp, jas_stream_t *out, jas_image_t *image);
void jpc_enc_destroy(jpc_enc_t *enc);



static uint_fast32_t
jpc_abstorelstepsize(jpc_fix_t absdelta, int scaleexpn) {

    int p;
    uint_fast32_t mant;
    uint_fast32_t expn;
    int n;

    if (absdelta < 0) {
        abort();
    }

    p = jpc_firstone(absdelta) - JPC_FIX_FRACBITS;
    n = 11 - jpc_firstone(absdelta);
    mant = ((n < 0) ? (absdelta >> (-n)) : (absdelta << n)) & 0x7ff;
    expn = scaleexpn - p;
    if (scaleexpn < p) {
        abort();
    }
    return JPC_QCX_EXPN(expn) | JPC_QCX_MANT(mant);
}



typedef enum {
    OPT_DEBUG,
    OPT_IMGAREAOFFX,
    OPT_IMGAREAOFFY,
    OPT_TILEGRDOFFX,
    OPT_TILEGRDOFFY,
    OPT_TILEWIDTH,
    OPT_TILEHEIGHT,
    OPT_PRCWIDTH,
    OPT_PRCHEIGHT,
    OPT_CBLKWIDTH,
    OPT_CBLKHEIGHT,
    OPT_MODE,
    OPT_PRG,
    OPT_NOMCT,
    OPT_MAXRLVLS,
    OPT_SOP,
    OPT_EPH,
    OPT_LAZY,
    OPT_TERMALL,
    OPT_SEGSYM,
    OPT_VCAUSAL,
    OPT_RESET,
    OPT_PTERM,
    OPT_NUMGBITS,
    OPT_RATE,
    OPT_ILYRRATES,
} optid_t;

jas_taginfo_t encopts[] = {
    {OPT_DEBUG, "debug"},
    {OPT_IMGAREAOFFX, "imgareatlx"},
    {OPT_IMGAREAOFFY, "imgareatly"},
    {OPT_TILEGRDOFFX, "tilegrdtlx"},
    {OPT_TILEGRDOFFY, "tilegrdtly"},
    {OPT_TILEWIDTH, "tilewidth"},
    {OPT_TILEHEIGHT, "tileheight"},
    {OPT_PRCWIDTH, "prcwidth"},
    {OPT_PRCHEIGHT, "prcheight"},
    {OPT_CBLKWIDTH, "cblkwidth"},
    {OPT_CBLKHEIGHT, "cblkheight"},
    {OPT_MODE, "mode"},
    {OPT_PRG, "prg"},
    {OPT_NOMCT, "nomct"},
    {OPT_MAXRLVLS, "numrlvls"},
    {OPT_SOP, "sop"},
    {OPT_EPH, "eph"},
    {OPT_LAZY, "lazy"},
    {OPT_TERMALL, "termall"},
    {OPT_SEGSYM, "segsym"},
    {OPT_VCAUSAL, "vcausal"},
    {OPT_PTERM, "pterm"},
    {OPT_RESET, "resetprob"},
    {OPT_NUMGBITS, "numgbits"},
    {OPT_RATE, "rate"},
    {OPT_ILYRRATES, "ilyrrates"},
    {-1, 0}
};

typedef enum {
    PO_L = 0,
    PO_R
} poid_t;


jas_taginfo_t prgordtab[] = {
    {JPC_COD_LRCPPRG, "lrcp"},
    {JPC_COD_RLCPPRG, "rlcp"},
    {JPC_COD_RPCLPRG, "rpcl"},
    {JPC_COD_PCRLPRG, "pcrl"},
    {JPC_COD_CPRLPRG, "cprl"},
    {-1, 0}
};

typedef enum {
    MODE_INT,
    MODE_REAL
} modeid_t;

jas_taginfo_t modetab[] = {
    {MODE_INT, "int"},
    {MODE_REAL, "real"},
    {-1, 0}
};



static void
tracev(const char * const fmt,
       va_list            args) {

    vfprintf(stderr, fmt, args);

    fprintf(stderr, "\n");
}



static void
trace(const char * const fmt, ...) {

    if (jas_getdbglevel() > 0) {
        va_list args;

        va_start(args, fmt);
        tracev(fmt, args);
        va_end(args);
    }
}



/*****************************************************************************\
* Option parsing code.
\*****************************************************************************/

static void
ratestrtosize(const char *    const s,
              uint_fast32_t   const rawsize,
              uint_fast32_t * const sizeP) {

    if (strchr(s, 'B')) {
        *sizeP = atoi(s);
    } else {
        jpc_flt_t const f = atof(s);

        if (f < 0) {
            *sizeP = 0;
        } else if (f > 1.0) {
            *sizeP = rawsize + 1;
        } else {
            *sizeP = f * rawsize;
        }
    }
}



static void
cp_destroy(jpc_enc_cp_t *cp) {

    if (cp->ccps) {
        if (cp->tcp.ilyrrates) {
            jas_free(cp->tcp.ilyrrates);
        }
        jas_free(cp->ccps);
    }
    jas_free(cp);
}



static jpc_enc_cp_t *
cp_create(char *optstr, jas_image_t *image) {

    jpc_enc_cp_t *cp;
    jas_tvparser_t *tvp;
    int ret;
    int numilyrrates;
    double *ilyrrates;
    int i;
    int tagid;
    jpc_enc_tcp_t *tcp;
    jpc_enc_tccp_t *tccp;
    jpc_enc_ccp_t *ccp;
    uint_fast16_t cmptno;
    uint_fast16_t rlvlno;
    uint_fast16_t prcwidthexpn;
    uint_fast16_t prcheightexpn;
    bool enablemct;
    uint_fast16_t lyrno;
    uint_fast32_t hsteplcm;
    uint_fast32_t vsteplcm;
    bool mctvalid;

    tvp = 0;
    cp = 0;
    ilyrrates = 0;
    numilyrrates = 0;

    if (!(cp = jas_malloc(sizeof(jpc_enc_cp_t)))) {
        goto error;
    }

    prcwidthexpn = 15;
    prcheightexpn = 15;
    enablemct = true;

    cp->ccps = 0;
    cp->debug = 0;
    cp->imgareatlx = UINT_FAST32_MAX;
    cp->imgareatly = UINT_FAST32_MAX;
    cp->refgrdwidth = 0;
    cp->refgrdheight = 0;
    cp->tilegrdoffx = UINT_FAST32_MAX;
    cp->tilegrdoffy = UINT_FAST32_MAX;
    cp->tilewidth = 0;
    cp->tileheight = 0;
    cp->numcmpts = jas_image_numcmpts(image);

    hsteplcm = 1;
    vsteplcm = 1;
    for (cmptno = 0; cmptno < jas_image_numcmpts(image); ++cmptno) {
        if (jas_image_cmptbrx(image, cmptno) +
            jas_image_cmpthstep(image, cmptno) <=
          jas_image_brx(image) || jas_image_cmptbry(image, cmptno) +
          jas_image_cmptvstep(image, cmptno) <= jas_image_bry(image)) {
            fprintf(stderr,
                    "We don't know how to interpret this image type\n");
            goto error;
        }
        /* Note: We ought to be calculating the LCMs here.  Fix some day. */
        hsteplcm *= jas_image_cmpthstep(image, cmptno);
        vsteplcm *= jas_image_cmptvstep(image, cmptno);
    }

    if (!(cp->ccps = jas_malloc(cp->numcmpts * sizeof(jpc_enc_ccp_t)))) {
        goto error;
    }
    for (cmptno = 0, ccp = cp->ccps; cmptno < cp->numcmpts; ++cmptno,
      ++ccp) {
        ccp->sampgrdstepx = jas_image_cmpthstep(image, cmptno);
        ccp->sampgrdstepy = jas_image_cmptvstep(image, cmptno);
        /* XXX - this isn't quite correct for more general image */
        ccp->sampgrdsubstepx = 0;
        ccp->sampgrdsubstepx = 0;
        ccp->prec = jas_image_cmptprec(image, cmptno);
        ccp->sgnd = jas_image_cmptsgnd(image, cmptno);
        ccp->numstepsizes = 0;
        memset(ccp->stepsizes, 0, sizeof(ccp->stepsizes));
    }

    cp->rawsize = jas_image_rawsize(image);
    cp->totalsize = UINT_FAST32_MAX;
        /* Set default value, the special value that means size is unlimited
           (so lossless coding is called for).  To be overridden if user
           specified
        */

    tcp = &cp->tcp;
    tcp->csty = 0;
    tcp->intmode = true;
    tcp->prg = JPC_COD_LRCPPRG;
    tcp->numlyrs = 1;
    tcp->ilyrrates = 0;

    tccp = &cp->tccp;
    tccp->csty = 0;
    tccp->maxrlvls = 6;
    tccp->cblkwidthexpn = 6;
    tccp->cblkheightexpn = 6;
    tccp->cblksty = 0;
    tccp->numgbits = 2;

    if (!(tvp = jas_tvparser_create(optstr ? optstr : ""))) {
        goto error;
    }

    while (!(ret = jas_tvparser_next(tvp))) {
        switch (jas_taginfo_nonull(jas_taginfos_lookup(encopts,
          jas_tvparser_gettag(tvp)))->id) {
        case OPT_DEBUG:
            cp->debug = atoi(jas_tvparser_getval(tvp));
            break;
        case OPT_IMGAREAOFFX:
            cp->imgareatlx = atoi(jas_tvparser_getval(tvp));
            break;
        case OPT_IMGAREAOFFY:
            cp->imgareatly = atoi(jas_tvparser_getval(tvp));
            break;
        case OPT_TILEGRDOFFX:
            cp->tilegrdoffx = atoi(jas_tvparser_getval(tvp));
            break;
        case OPT_TILEGRDOFFY:
            cp->tilegrdoffy = atoi(jas_tvparser_getval(tvp));
            break;
        case OPT_TILEWIDTH:
            cp->tilewidth = atoi(jas_tvparser_getval(tvp));
            break;
        case OPT_TILEHEIGHT:
            cp->tileheight = atoi(jas_tvparser_getval(tvp));
            break;
        case OPT_PRCWIDTH:
            prcwidthexpn = jpc_floorlog2(atoi(jas_tvparser_getval(tvp)));
            break;
        case OPT_PRCHEIGHT:
            prcheightexpn = jpc_floorlog2(atoi(jas_tvparser_getval(tvp)));
            break;
        case OPT_CBLKWIDTH:
            tccp->cblkwidthexpn =
              jpc_floorlog2(atoi(jas_tvparser_getval(tvp)));
            break;
        case OPT_CBLKHEIGHT:
            tccp->cblkheightexpn =
              jpc_floorlog2(atoi(jas_tvparser_getval(tvp)));
            break;
        case OPT_MODE:
            if ((tagid = jas_taginfo_nonull(jas_taginfos_lookup(modetab,
              jas_tvparser_getval(tvp)))->id) < 0) {
                fprintf(stderr,
                  "ignoring invalid mode %s\n",
                  jas_tvparser_getval(tvp));
            } else {
                tcp->intmode = (tagid == MODE_INT);
            }
            break;
        case OPT_PRG:
            if ((tagid = jas_taginfo_nonull(jas_taginfos_lookup(prgordtab,
              jas_tvparser_getval(tvp)))->id) < 0) {
                fprintf(stderr,
                  "ignoring invalid progression order %s\n",
                  jas_tvparser_getval(tvp));
            } else {
                tcp->prg = tagid;
            }
            break;
        case OPT_NOMCT:
            enablemct = false;
            break;
        case OPT_MAXRLVLS:
            tccp->maxrlvls = atoi(jas_tvparser_getval(tvp));
            break;
        case OPT_SOP:
            cp->tcp.csty |= JPC_COD_SOP;
            break;
        case OPT_EPH:
            cp->tcp.csty |= JPC_COD_EPH;
            break;
        case OPT_LAZY:
            tccp->cblksty |= JPC_COX_LAZY;
            break;
        case OPT_TERMALL:
            tccp->cblksty |= JPC_COX_TERMALL;
            break;
        case OPT_SEGSYM:
            tccp->cblksty |= JPC_COX_SEGSYM;
            break;
        case OPT_VCAUSAL:
            tccp->cblksty |= JPC_COX_VSC;
            break;
        case OPT_RESET:
            tccp->cblksty |= JPC_COX_RESET;
            break;
        case OPT_PTERM:
            tccp->cblksty |= JPC_COX_PTERM;
            break;
        case OPT_NUMGBITS:
            cp->tccp.numgbits = atoi(jas_tvparser_getval(tvp));
            break;
        case OPT_RATE:
            ratestrtosize(jas_tvparser_getval(tvp), cp->rawsize,
                          &cp->totalsize);
            break;
        case OPT_ILYRRATES:
            if (jpc_atoaf(jas_tvparser_getval(tvp), &numilyrrates,
              &ilyrrates)) {
                fprintf(stderr,
                        "warning: invalid intermediate layer rates specifier "
                        "ignored (%s)\n",
                        jas_tvparser_getval(tvp));
            }
            break;

        default:
            fprintf(stderr, "warning: ignoring invalid option %s\n",
             jas_tvparser_gettag(tvp));
            break;
        }
    }

    jas_tvparser_destroy(tvp);
    tvp = 0;

    if (cp->imgareatlx == UINT_FAST32_MAX) {
        cp->imgareatlx = 0;
    } else {
        if (hsteplcm != 1) {
            fprintf(stderr, "warning: overriding imgareatlx value\n");
        }
        cp->imgareatlx *= hsteplcm;
    }
    if (cp->imgareatly == UINT_FAST32_MAX) {
        cp->imgareatly = 0;
    } else {
        if (vsteplcm != 1) {
            fprintf(stderr, "warning: overriding imgareatly value\n");
        }
        cp->imgareatly *= vsteplcm;
    }
    cp->refgrdwidth = cp->imgareatlx + jas_image_width(image);
    cp->refgrdheight = cp->imgareatly + jas_image_height(image);
    if (cp->tilegrdoffx == UINT_FAST32_MAX) {
        cp->tilegrdoffx = cp->imgareatlx;
    }
    if (cp->tilegrdoffy == UINT_FAST32_MAX) {
        cp->tilegrdoffy = cp->imgareatly;
    }
    if (!cp->tilewidth) {
        cp->tilewidth = cp->refgrdwidth - cp->tilegrdoffx;
    }
    if (!cp->tileheight) {
        cp->tileheight = cp->refgrdheight - cp->tilegrdoffy;
    }

    if (cp->numcmpts == 3) {
        mctvalid = true;
        for (cmptno = 0; cmptno < jas_image_numcmpts(image); ++cmptno) {
            if (jas_image_cmptprec(image, cmptno) !=
                jas_image_cmptprec(image, 0) ||
              jas_image_cmptsgnd(image, cmptno) !=
                jas_image_cmptsgnd(image, 0) ||
              jas_image_cmptwidth(image, cmptno) !=
                jas_image_cmptwidth(image, 0) ||
              jas_image_cmptheight(image, cmptno) !=
                jas_image_cmptheight(image, 0)) {
                mctvalid = false;
            }
        }
    } else {
        mctvalid = false;
    }
    if (mctvalid && enablemct && jas_image_colorspace(image) !=
        JAS_IMAGE_CS_RGB) {
        fprintf(stderr, "warning: color model apparently not RGB\n");
    }
    if (mctvalid && enablemct && jas_image_colorspace(image) ==
        JAS_IMAGE_CS_RGB) {
        tcp->mctid = (tcp->intmode) ? (JPC_MCT_RCT) : (JPC_MCT_ICT);
    } else {
        tcp->mctid = JPC_MCT_NONE;
    }
    tccp->qmfbid = (tcp->intmode) ? (JPC_COX_RFT) : (JPC_COX_INS);

    for (rlvlno = 0; rlvlno < tccp->maxrlvls; ++rlvlno) {
        tccp->prcwidthexpns[rlvlno] = prcwidthexpn;
        tccp->prcheightexpns[rlvlno] = prcheightexpn;
    }
    if (prcwidthexpn != 15 || prcheightexpn != 15) {
        tccp->csty |= JPC_COX_PRT;
    }

    /* Ensure that the tile width and height is valid. */
    if (!cp->tilewidth) {
        fprintf(stderr, "invalid tile width %lu\n", (unsigned long)
          cp->tilewidth);
        goto error;
    }
    if (!cp->tileheight) {
        fprintf(stderr, "invalid tile height %lu\n", (unsigned long)
          cp->tileheight);
        goto error;
    }

    /* Ensure that the tile grid offset is valid. */
    if (cp->tilegrdoffx > cp->imgareatlx ||
      cp->tilegrdoffy > cp->imgareatly ||
      cp->tilegrdoffx + cp->tilewidth < cp->imgareatlx ||
      cp->tilegrdoffy + cp->tileheight < cp->imgareatly) {
        fprintf(stderr, "invalid tile grid offset (%lu, %lu)\n",
          (unsigned long) cp->tilegrdoffx, (unsigned long)
          cp->tilegrdoffy);
        goto error;
    }

    cp->numhtiles = JPC_CEILDIV(cp->refgrdwidth - cp->tilegrdoffx,
      cp->tilewidth);
    cp->numvtiles = JPC_CEILDIV(cp->refgrdheight - cp->tilegrdoffy,
      cp->tileheight);
    cp->numtiles = cp->numhtiles * cp->numvtiles;

    if (ilyrrates && numilyrrates > 0) {
        tcp->numlyrs = numilyrrates + 1;
        if (!(tcp->ilyrrates = jas_malloc((tcp->numlyrs - 1) *
          sizeof(jpc_fix_t)))) {
            goto error;
        }
        for (i = 0; i < tcp->numlyrs - 1; ++i) {
            tcp->ilyrrates[i] = jpc_dbltofix(ilyrrates[i]);
        }
    }

    /* Ensure that the integer mode is used in the case of lossless
      coding. */
    if (cp->totalsize == UINT_FAST32_MAX && (!cp->tcp.intmode)) {
        fprintf(stderr, "cannot use real mode for lossless coding\n");
        goto error;
    }

    /* Ensure that the precinct width is valid. */
    if (prcwidthexpn > 15) {
        fprintf(stderr, "invalid precinct width\n");
        goto error;
    }

    /* Ensure that the precinct height is valid. */
    if (prcheightexpn > 15) {
        fprintf(stderr, "invalid precinct height\n");
        goto error;
    }

    /* Ensure that the code block width is valid. */
    if (cp->tccp.cblkwidthexpn < 2 || cp->tccp.cblkwidthexpn > 12) {
        fprintf(stderr, "invalid code block width %d\n",
          JPC_POW2(cp->tccp.cblkwidthexpn));
        goto error;
    }

    /* Ensure that the code block height is valid. */
    if (cp->tccp.cblkheightexpn < 2 || cp->tccp.cblkheightexpn > 12) {
        fprintf(stderr, "invalid code block height %d\n",
          JPC_POW2(cp->tccp.cblkheightexpn));
        goto error;
    }

    /* Ensure that the code block size is not too large. */
    if (cp->tccp.cblkwidthexpn + cp->tccp.cblkheightexpn > 12) {
        fprintf(stderr, "code block size too large\n");
        goto error;
    }

    /* Ensure that the number of layers is valid. */
    if (cp->tcp.numlyrs > 16384) {
        fprintf(stderr, "too many layers\n");
        goto error;
    }

    /* There must be at least one resolution level. */
    if (cp->tccp.maxrlvls < 1) {
        fprintf(stderr, "must be at least one resolution level\n");
        goto error;
    }

    /* Ensure that the number of guard bits is valid. */
    if (cp->tccp.numgbits > 8) {
        fprintf(stderr, "invalid number of guard bits\n");
        goto error;
    }

    /* Ensure that the intermediate layer rates are valid. */
    if (tcp->numlyrs > 1) {
        /* The intermediate layers rates must increase monotonically. */
        for (lyrno = 0; lyrno + 2 < tcp->numlyrs; ++lyrno) {
            if (tcp->ilyrrates[lyrno] >= tcp->ilyrrates[lyrno + 1]) {
                pm_message("Compression rate for Layer %u (%f) "
                           "is not greater than that for Layer %u (%f).  "
                           "Rates must increase at every layer",
                           (unsigned)(lyrno+1),
                           jpc_fixtodbl(tcp->ilyrrates[lyrno + 1]),
                           (unsigned)lyrno,
                           jpc_fixtodbl(tcp->ilyrrates[lyrno]));
                goto error;
            }
        }
        /* The intermediate layer rates must be less than the overall rate. */
        if (cp->totalsize != UINT_FAST32_MAX) {
            for (lyrno = 0; lyrno < tcp->numlyrs - 1; ++lyrno) {
                double const thisLyrRate = jpc_fixtodbl(tcp->ilyrrates[lyrno]);
                double const completeRate =
                    ((double) cp->totalsize) / cp->rawsize;
                if (thisLyrRate > completeRate) {
                    pm_message(
                        "Compression rate for Layer %u is %f, "
                        "which is greater than the rate for the complete "
                        "image (%f)",
                        (unsigned)lyrno, thisLyrRate, completeRate);
                    goto error;
                }
            }
        }
    }

    if (ilyrrates) {
        jas_free(ilyrrates);
    }

    return cp;

error:

    if (ilyrrates) {
        jas_free(ilyrrates);
    }
    if (tvp) {
        jas_tvparser_destroy(tvp);
    }
    if (cp) {
        cp_destroy(cp);
    }
    return 0;
}



static int
encodemainhdr(jpc_enc_t *enc) {

    uint_fast32_t const maintlrlen = 2;

    jpc_siz_t *siz;
    jpc_cod_t *cod;
    jpc_qcd_t *qcd;
    int i;
    long startoff;
    long mainhdrlen;
    jpc_enc_cp_t *cp;
    jpc_qcc_t *qcc;
    jpc_enc_tccp_t *tccp;
    uint_fast16_t cmptno;
    jpc_tsfb_band_t bandinfos[JPC_MAXBANDS];
    jpc_enc_tcp_t *tcp;
    jpc_tsfb_t *tsfb;
    jpc_tsfb_band_t *bandinfo;
    uint_fast16_t numbands;
    uint_fast16_t bandno;
    uint_fast16_t rlvlno;
    uint_fast16_t analgain;
    jpc_fix_t absstepsize;
    char buf[1024];
    jpc_com_t *com;

    cp = enc->cp;

    startoff = jas_stream_getrwcount(enc->out);

    /* Write SOC marker segment. */
    if (!(enc->mrk = jpc_ms_create(JPC_MS_SOC))) {
        return -1;
    }
    if (jpc_putms(enc->out, enc->cstate, enc->mrk)) {
        fprintf(stderr, "cannot write SOC marker\n");
        return -1;
    }
    jpc_ms_destroy(enc->mrk);
    enc->mrk = 0;

    /* Write SIZ marker segment. */
    if (!(enc->mrk = jpc_ms_create(JPC_MS_SIZ))) {
        return -1;
    }
    siz = &enc->mrk->parms.siz;
    siz->caps = 0;
    siz->xoff = cp->imgareatlx;
    siz->yoff = cp->imgareatly;
    siz->width = cp->refgrdwidth;
    siz->height = cp->refgrdheight;
    siz->tilexoff = cp->tilegrdoffx;
    siz->tileyoff = cp->tilegrdoffy;
    siz->tilewidth = cp->tilewidth;
    siz->tileheight = cp->tileheight;
    siz->numcomps = cp->numcmpts;
    siz->comps = jas_malloc(siz->numcomps * sizeof(jpc_sizcomp_t));
    assert(siz->comps);
    for (i = 0; i < cp->numcmpts; ++i) {
        siz->comps[i].prec = cp->ccps[i].prec;
        siz->comps[i].sgnd = cp->ccps[i].sgnd;
        siz->comps[i].hsamp = cp->ccps[i].sampgrdstepx;
        siz->comps[i].vsamp = cp->ccps[i].sampgrdstepy;
    }
    if (jpc_putms(enc->out, enc->cstate, enc->mrk)) {
        fprintf(stderr, "cannot write SIZ marker\n");
        return -1;
    }
    jpc_ms_destroy(enc->mrk);
    enc->mrk = 0;

    if (!(enc->mrk = jpc_ms_create(JPC_MS_COM))) {
        return -1;
    }
    sprintf(buf, "Creator: JasPer Version %s", jas_getversion());
    com = &enc->mrk->parms.com;
    com->len = strlen(buf);
    com->regid = JPC_COM_LATIN;
    if (!(com->data = JAS_CAST(unsigned char *, jas_strdup(buf)))) {
        abort();
    }
    if (jpc_putms(enc->out, enc->cstate, enc->mrk)) {
        fprintf(stderr, "cannot write COM marker\n");
        return -1;
    }
    jpc_ms_destroy(enc->mrk);
    enc->mrk = 0;

#if 0
    if (!(enc->mrk = jpc_ms_create(JPC_MS_CRG))) {
        return -1;
    }
    crg = &enc->mrk->parms.crg;
    crg->comps = jas_malloc(crg->numcomps * sizeof(jpc_crgcomp_t));
    if (jpc_putms(enc->out, enc->cstate, enc->mrk)) {
        fprintf(stderr, "cannot write CRG marker\n");
        return -1;
    }
    jpc_ms_destroy(enc->mrk);
    enc->mrk = 0;
#endif

    tcp = &cp->tcp;
    tccp = &cp->tccp;
    for (cmptno = 0; cmptno < cp->numcmpts; ++cmptno) {
        tsfb = jpc_cod_gettsfb(tccp->qmfbid, tccp->maxrlvls - 1);
        jpc_tsfb_getbands(tsfb, 0, 0, 1 << tccp->maxrlvls, 1 << tccp->maxrlvls,
          bandinfos);
        jpc_tsfb_destroy(tsfb);
        numbands = 3 * tccp->maxrlvls - 2;
        for (bandno = 0, bandinfo = bandinfos; bandno < numbands;
          ++bandno, ++bandinfo) {
            rlvlno = (bandno) ? ((bandno - 1) / 3 + 1) : 0;
            analgain = JPC_NOMINALGAIN(tccp->qmfbid, tccp->maxrlvls,
              rlvlno, bandinfo->orient);
            if (!tcp->intmode) {
                absstepsize = jpc_fix_div(jpc_inttofix(1 <<
                  (analgain + 1)), bandinfo->synenergywt);
            } else {
                absstepsize = jpc_inttofix(1);
            }
            cp->ccps[cmptno].stepsizes[bandno] =
              jpc_abstorelstepsize(absstepsize,
              cp->ccps[cmptno].prec + analgain);
        }
        cp->ccps[cmptno].numstepsizes = numbands;
    }

    if (!(enc->mrk = jpc_ms_create(JPC_MS_COD))) {
        return -1;
    }
    cod = &enc->mrk->parms.cod;
    cod->csty = cp->tccp.csty | cp->tcp.csty;
    cod->compparms.csty = cp->tccp.csty | cp->tcp.csty;
    cod->compparms.numdlvls = cp->tccp.maxrlvls - 1;
    cod->compparms.numrlvls = cp->tccp.maxrlvls;
    cod->prg = cp->tcp.prg;
    cod->numlyrs = cp->tcp.numlyrs;
    cod->compparms.cblkwidthval = JPC_COX_CBLKSIZEEXPN(cp->tccp.cblkwidthexpn);
    cod->compparms.cblkheightval =
        JPC_COX_CBLKSIZEEXPN(cp->tccp.cblkheightexpn);
    cod->compparms.cblksty = cp->tccp.cblksty;
    cod->compparms.qmfbid = cp->tccp.qmfbid;
    cod->mctrans = (cp->tcp.mctid != JPC_MCT_NONE);
    if (tccp->csty & JPC_COX_PRT) {
        for (rlvlno = 0; rlvlno < tccp->maxrlvls; ++rlvlno) {
            cod->compparms.rlvls[rlvlno].parwidthval =
                tccp->prcwidthexpns[rlvlno];
            cod->compparms.rlvls[rlvlno].parheightval =
                tccp->prcheightexpns[rlvlno];
        }
    }
    if (jpc_putms(enc->out, enc->cstate, enc->mrk)) {
        fprintf(stderr, "cannot write COD marker\n");
        return -1;
    }
    jpc_ms_destroy(enc->mrk);
    enc->mrk = 0;

    if (!(enc->mrk = jpc_ms_create(JPC_MS_QCD))) {
        return -1;
    }
    qcd = &enc->mrk->parms.qcd;
    qcd->compparms.qntsty = (tccp->qmfbid == JPC_COX_INS) ?
      JPC_QCX_SEQNT : JPC_QCX_NOQNT;
    qcd->compparms.numstepsizes = cp->ccps[0].numstepsizes;
    qcd->compparms.numguard = cp->tccp.numgbits;
    qcd->compparms.stepsizes = cp->ccps[0].stepsizes;
    if (jpc_putms(enc->out, enc->cstate, enc->mrk)) {
        return -1;
    }
    /* We do not want the step size array to be freed! */
    qcd->compparms.stepsizes = 0;
    jpc_ms_destroy(enc->mrk);
    enc->mrk = 0;

    tccp = &cp->tccp;
    for (cmptno = 1; cmptno < cp->numcmpts; ++cmptno) {
        if (!(enc->mrk = jpc_ms_create(JPC_MS_QCC))) {
            return -1;
        }
        qcc = &enc->mrk->parms.qcc;
        qcc->compno = cmptno;
        qcc->compparms.qntsty = (tccp->qmfbid == JPC_COX_INS) ?
          JPC_QCX_SEQNT : JPC_QCX_NOQNT;
        qcc->compparms.numstepsizes = cp->ccps[cmptno].numstepsizes;
        qcc->compparms.numguard = cp->tccp.numgbits;
        qcc->compparms.stepsizes = cp->ccps[cmptno].stepsizes;
        if (jpc_putms(enc->out, enc->cstate, enc->mrk)) {
            return -1;
        }
        /* We do not want the step size array to be freed! */
        qcc->compparms.stepsizes = 0;
        jpc_ms_destroy(enc->mrk);
        enc->mrk = 0;
    }

    mainhdrlen = jas_stream_getrwcount(enc->out) - startoff;
    enc->len += mainhdrlen;
    if (enc->cp->totalsize != UINT_FAST32_MAX) {
        uint_fast32_t const overhead = mainhdrlen + maintlrlen;

        if (overhead > enc->cp->totalsize) {
            pm_message("Requested limit on image size of %u bytes "
                       "is not possible because it is less than "
                       "the image metadata size (%u bytes)",
                       (unsigned)enc->cp->totalsize, (unsigned)overhead);
            return -1;
        }
        enc->mainbodysize = enc->cp->totalsize - overhead;
        /* This has never actually worked.  'totalsize' is supposed to be
           the total all-in, so if you request total size 200, you should
           get an output file 200 bytes or smaller; but we see 209 bytes.
           Furthermore, at 194 bytes, we get a warning that an empty layer
           is generated, which probably is actually an error.

           We should fix this some day.
        */
    } else {
        enc->mainbodysize = UINT_FAST32_MAX;
    }

    return 0;
}



static void
calcrdslopes(jpc_enc_cblk_t *cblk) {

    jpc_enc_pass_t *endpasses;
    jpc_enc_pass_t *pass0;
    jpc_enc_pass_t *pass1;
    jpc_enc_pass_t *pass2;
    jpc_flt_t slope0;
    jpc_flt_t slope;
    jpc_flt_t dd;
    long dr;

    endpasses = &cblk->passes[cblk->numpasses];
    pass2 = cblk->passes;
    slope0 = 0;
    while (pass2 != endpasses) {
        pass0 = 0;
        for (pass1 = cblk->passes; pass1 != endpasses; ++pass1) {
            dd = pass1->cumwmsedec;
            dr = pass1->end;
            if (pass0) {
                dd -= pass0->cumwmsedec;
                dr -= pass0->end;
            }
            if (dd <= 0) {
                pass1->rdslope = JPC_BADRDSLOPE;
                if (pass1 >= pass2) {
                    pass2 = &pass1[1];
                }
                continue;
            }
            if (pass1 < pass2 && pass1->rdslope <= 0) {
                continue;
            }
            if (!dr) {
                assert(pass0);
                pass0->rdslope = 0;
                break;
            }
            slope = dd / dr;
            if (pass0 && slope >= slope0) {
                pass0->rdslope = 0;
                break;
            }
            pass1->rdslope = slope;
            if (pass1 >= pass2) {
                pass2 = &pass1[1];
            }
            pass0 = pass1;
            slope0 = slope;
        }
    }

#if 0
    for (pass0 = cblk->passes; pass0 != endpasses; ++pass0) {
        if (pass0->rdslope > 0.0) {
            fprintf(stderr, "pass %02d nmsedec=%lf dec=%lf end=%d %lf\n",
                    pass0 - cblk->passes,
                    fixtodbl(pass0->nmsedec), pass0->wmsedec,
                    pass0->end, pass0->rdslope);
        }
    }
#endif
}



static void
traceLayerSizes(const uint_fast32_t * const lyrSizes,
                uint_fast32_t         const layerCt) {

    if (jas_getdbglevel() > 0) {
        uint_fast32_t i;
        for (i = 0; i < layerCt; ++i) {
            fprintf(stderr, "Layer %u size = ", (unsigned)i);

            if (lyrSizes[i] == UINT_FAST32_MAX)
                fprintf(stderr, "Unlimited");
            else
                fprintf(stderr, "%u", (unsigned)lyrSizes[i]);
            fprintf(stderr, "\n");
        }
    }
}



static void
computeLayerSizes(jpc_enc_t *      const encP,
                  jpc_enc_tile_t * const tileP,
                  jpc_enc_cp_t *   const cpP,
                  double           const rho,
                  long             const tilehdrlen) {

    /* Note that in allowed sizes, UINT_FAST32_MAX is a special value meaning
       "unlimited".
    */

    uint_fast32_t const lastLyrno = tileP->numlyrs - 1;

    uint_fast32_t lyrno;

    assert(tileP->numlyrs > 0);

    for (lyrno = 0; lyrno < lastLyrno; ++lyrno) {
        tileP->lyrsizes[lyrno] =
            MAX(tileP->rawsize *
                jpc_fixtodbl(cpP->tcp.ilyrrates[lyrno]),
                tilehdrlen + 1) - tilehdrlen;
    }

    tileP->lyrsizes[lastLyrno] =
        (cpP->totalsize == UINT_FAST32_MAX) ?
        UINT_FAST32_MAX : (rho * encP->mainbodysize);

    traceLayerSizes(tileP->lyrsizes, tileP->numlyrs);
}



static void
dump_layeringinfo(jpc_enc_t *enc) {

    jpc_enc_tcmpt_t *tcmpt;
    uint_fast16_t tcmptno;
    jpc_enc_rlvl_t *rlvl;
    uint_fast16_t rlvlno;
    jpc_enc_band_t *band;
    uint_fast16_t bandno;
    jpc_enc_prc_t *prc;
    uint_fast32_t prcno;
    jpc_enc_cblk_t *cblk;
    uint_fast16_t cblkno;
    jpc_enc_pass_t *pass;
    uint_fast16_t passno;
    int lyrno;
    jpc_enc_tile_t *tile;

    tile = enc->curtile;

    for (lyrno = 0; lyrno < tile->numlyrs; ++lyrno) {
        fprintf(stderr, "lyrno = %02d\n", lyrno);
        for (tcmptno = 0, tcmpt = tile->tcmpts; tcmptno < tile->numtcmpts;
          ++tcmptno, ++tcmpt) {
            for (rlvlno = 0, rlvl = tcmpt->rlvls; rlvlno < tcmpt->numrlvls;
              ++rlvlno, ++rlvl) {
                if (!rlvl->bands) {
                    continue;
                }
                for (bandno = 0, band = rlvl->bands; bandno < rlvl->numbands;
                  ++bandno, ++band) {
                    if (!band->data) {
                        continue;
                    }
                    for (prcno = 0, prc = band->prcs; prcno < rlvl->numprcs;
                      ++prcno, ++prc) {
                        if (!prc->cblks) {
                            continue;
                        }
                        for (cblkno = 0, cblk = prc->cblks; cblkno <
                          prc->numcblks; ++cblkno, ++cblk) {
                            for (passno = 0, pass = cblk->passes;
                                 passno < cblk->numpasses &&
                                     pass->lyrno == lyrno;
                                 ++passno, ++pass) {
                                fprintf(stderr,
                                        "lyrno=%02d cmptno=%02d "
                                        "rlvlno=%02d bandno=%02d "
                                        "prcno=%02d cblkno=%03d "
                                        "passno=%03d\n",
                                        lyrno, (int)tcmptno, (int)rlvlno,
                                        (int)bandno, (int)prcno, (int) cblkno,
                                        (int)passno);
                            }
                        }
                    }
                }
            }
        }
    }
}




static void
trace_layeringinfo(jpc_enc_t * const encP) {

    if (jas_getdbglevel() >= 5)
        dump_layeringinfo(encP);
}



static void
validateCumlensIncreases(const uint_fast32_t * const cumlens,
                         uint_fast32_t          const numlyrs) {
    uint_fast32_t lyrno;

    for (lyrno = 1; lyrno < numlyrs - 1; ++lyrno) {
        if (cumlens[lyrno - 1] > cumlens[lyrno]) {
            abort();
        }
    }
}



static void
findMinMaxRDSlopeValues(jpc_enc_tile_t * const tileP,
                        jpc_flt_t *      const mnrdslopeP,
                        jpc_flt_t *      const mxrdslopeP) {
/*----------------------------------------------------------------------------
  Find minimum and maximum R-D slope values.
-----------------------------------------------------------------------------*/
    jpc_flt_t mxrdslope;
    jpc_flt_t mnrdslope;
    jpc_enc_tcmpt_t * endcomps;
    jpc_enc_tcmpt_t * compP;

    mnrdslope = DBL_MAX;
    mxrdslope = 0;
    endcomps = &tileP->tcmpts[tileP->numtcmpts];

    for (compP = tileP->tcmpts; compP != endcomps; ++compP) {
        jpc_enc_rlvl_t * const endlvlsP = &compP->rlvls[compP->numrlvls];

        jpc_enc_rlvl_t * lvlP;

        for (lvlP = compP->rlvls; lvlP != endlvlsP; ++lvlP) {
            jpc_enc_band_t * endbandsP;
            jpc_enc_band_t * bandP;

            if (lvlP->bands) {
                endbandsP = &lvlP->bands[lvlP->numbands];
                for (bandP = lvlP->bands; bandP != endbandsP; ++bandP) {
                    uint_fast32_t prcno;
                    jpc_enc_prc_t * prcP;

                    if (bandP->data) {
                        for (prcno = 0, prcP = bandP->prcs;
                             prcno < lvlP->numprcs;
                             ++prcno, ++prcP) {

                            jpc_enc_cblk_t * endcblksP;
                            jpc_enc_cblk_t * cblkP;

                            if (prcP->cblks) {
                                endcblksP = &prcP->cblks[prcP->numcblks];
                                for (cblkP = prcP->cblks;
                                     cblkP != endcblksP;
                                     ++cblkP) {
                                    jpc_enc_pass_t * endpassesP;
                                    jpc_enc_pass_t * passP;

                                    calcrdslopes(cblkP);
                                    endpassesP =
                                        &cblkP->passes[cblkP->numpasses];
                                    for (passP = cblkP->passes;
                                         passP != endpassesP;
                                         ++passP) {
                                        if (passP->rdslope > 0) {
                                            mnrdslope =
                                                MIN(passP->rdslope, mnrdslope);
                                            mxrdslope =
                                                MAX(passP->rdslope, mxrdslope);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    trace("min rdslope = %f max rdslope = %f", mnrdslope, mxrdslope);

    *mnrdslopeP = mnrdslope;
    *mxrdslopeP = mxrdslope;
}



static void
performTier2CodingOneLayer(jpc_enc_t *      const encP,
                           jpc_enc_tile_t * const tileP,
                           uint_fast32_t     const lyrno,
                           jas_stream_t *   const outP,
                           const char **    const errorP) {
/*----------------------------------------------------------------------------
  Encode Layer 'lyrno' of tile *tileP to stream *outP.

  Use the pass assignment already in *tileP.
-----------------------------------------------------------------------------*/
    jpc_enc_tcmpt_t * const endcompsP = &tileP->tcmpts[tileP->numtcmpts];

    jpc_enc_tcmpt_t * compP;

    *errorP = NULL;  /* initial assumption */

    for (compP = tileP->tcmpts; compP != endcompsP && !*errorP; ++compP) {
        jpc_enc_rlvl_t * const endlvlsP = &compP->rlvls[compP->numrlvls];

        jpc_enc_rlvl_t * lvlP;

        for (lvlP = compP->rlvls; lvlP != endlvlsP && !*errorP; ++lvlP) {
            if (lvlP->bands) {
                uint_fast32_t prcno;
                for (prcno = 0; prcno < lvlP->numprcs && !*errorP; ++prcno) {
                    int rc;
                    rc = jpc_enc_encpkt(encP, outP, compP - tileP->tcmpts,
                                        lvlP - compP->rlvls, prcno, lyrno);
                    if (rc != 0)
                        pm_asprintf(errorP, "jpc_enc_encpkt() failed on "
                                    "precinct %u", (unsigned)prcno);
                }
            }
        }
    }
}



static void
assignHighSlopePassesToLayer(jpc_enc_t *      const encP,
                             jpc_enc_tile_t * const tileP,
                             uint_fast32_t     const lyrno,
                             bool              const haveThresh,
                             jpc_flt_t         const thresh) {
/*----------------------------------------------------------------------------
  Assign all passes with R-D slopes greater than or equal to 'thresh' to layer
  'lyrno' and the rest to no layer.

  If 'haveThresh' is false, assign all passes to no layer.
-----------------------------------------------------------------------------*/
    jpc_enc_tcmpt_t * endcompsP;
    jpc_enc_tcmpt_t * compP;

    endcompsP = &tileP->tcmpts[tileP->numtcmpts];
    for (compP = tileP->tcmpts; compP != endcompsP; ++compP) {
        jpc_enc_rlvl_t * const endlvlsP = &compP->rlvls[compP->numrlvls];

        jpc_enc_rlvl_t * lvlP;

        for (lvlP = compP->rlvls; lvlP != endlvlsP; ++lvlP) {
            if (lvlP->bands) {
                jpc_enc_band_t * const endbandsP =
                    &lvlP->bands[lvlP->numbands];
                jpc_enc_band_t * bandP;
                for (bandP = lvlP->bands; bandP != endbandsP; ++bandP) {
                    if (bandP->data) {
                        jpc_enc_prc_t * prcP;
                        uint_fast32_t prcno;
                        for (prcno = 0, prcP = bandP->prcs;
                             prcno < lvlP->numprcs;
                             ++prcno, ++prcP) {
                            if (prcP->cblks) {
                                jpc_enc_cblk_t * const endcblksP =
                                    &prcP->cblks[prcP->numcblks];
                                jpc_enc_cblk_t * cblkP;
                                for (cblkP = prcP->cblks;
                                     cblkP != endcblksP;
                                     ++cblkP) {
                                    if (cblkP->curpass) {
                                        jpc_enc_pass_t *  const endpassesP =
                                            &cblkP->passes[cblkP->numpasses];
                                        jpc_enc_pass_t * pass1P;
                                        jpc_enc_pass_t * passP;

                                        pass1P = cblkP->curpass;
                                        if (haveThresh) {
                                            jpc_enc_pass_t * passP;
                                            for (passP = cblkP->curpass;
                                                 passP != endpassesP;
                                                 ++passP) {
                                                if (passP->rdslope >= thresh)
                                                    pass1P = passP + 1;
                                            }
                                        }
                                        for (passP = cblkP->curpass;
                                             passP != pass1P;
                                             ++passP) {
                                            passP->lyrno = lyrno;
                                        }
                                        for (; passP != endpassesP; ++passP) {
                                            passP->lyrno = -1;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}



static void
doLayer(jpc_enc_t *      const encP,
        jpc_enc_tile_t * const tileP,
        uint_fast32_t    const lyrno,
        uint_fast32_t    const allowedSize,
        jpc_flt_t        const mnrdslope,
        jpc_flt_t        const mxrdslope,
        jas_stream_t *   const outP,
        const char **    const errorP) {
/*----------------------------------------------------------------------------
   Assign passes to layer 'lyrno' such that the cumulative size through
   this layer is as close as possible to, but not exceeding, 'allowedSize'.
-----------------------------------------------------------------------------*/
    bool      haveGoodThresh;
    jpc_flt_t goodThresh;

    if (allowedSize == UINT_FAST32_MAX) {
        /* There's no rate constraint (This can be true of the last layer,
           e.g. for lossless coding). */
        goodThresh = -1;
        haveGoodThresh = true;
        *errorP = NULL;
    } else {
        /* Iterate through successive approximations of the threshold, finding
           the threshold that gets us closest to 'allowedSize' without going
           over.  In each iteration, we do the full encoding, note the size,
           and then restore the previous state.
        */
        long pos;
        jpc_flt_t lo;
        jpc_flt_t hi;
        uint_fast32_t numiters;

        lo = mnrdslope;  /* initial value */
        hi = mxrdslope;  /* initial value */
        numiters = 0;    /* initial value */
        haveGoodThresh = false;  /* initial value */
        goodThresh = 0;     /* initial value */

        do {
            jpc_flt_t const thresh = (lo + hi) / 2;

            int rc;
            long oldpos;

            /* Save the tier 2 coding state. */
            jpc_save_t2state(encP);
            oldpos = jas_stream_tell(outP);
            assert(oldpos >= 0);

            assignHighSlopePassesToLayer(encP, tileP, lyrno, true, thresh);

            performTier2CodingOneLayer(encP, tileP, lyrno, outP, errorP);

            if (!*errorP) {
                pos = jas_stream_tell(outP);

                /* Check the rate constraint. */
                assert(pos >= 0);
                if (pos > allowedSize) {
                    /* The rate is too high. */
                    lo = thresh;
                } else if (pos <= allowedSize) {
                    /* The rate is low enough, so try higher. */
                    hi = thresh;
                    if (!haveGoodThresh || thresh < goodThresh) {
                        goodThresh = thresh;
                        haveGoodThresh = true;
                    }
                }
            }
            /* Restore the tier 2 coding state. */
            jpc_restore_t2state(encP);
            rc = jas_stream_seek(outP, oldpos, SEEK_SET);
            if (rc < 0)
                abort();

            trace("iter %u: allowedlen=%08ld actuallen=%08ld thresh=%f",
                  numiters, allowedSize, pos, thresh);

            ++numiters;
        } while (lo < hi - 1e-3 && numiters < 32 && !*errorP);
    }

    if (!*errorP) {
        if (!haveGoodThresh)
            fprintf(stderr, "warning: empty layer generated\n");

        trace("haveGoodThresh %u goodthresh %f", haveGoodThresh, goodThresh);

        assignHighSlopePassesToLayer(encP, tileP, lyrno,
                                     haveGoodThresh, goodThresh);

        performTier2CodingOneLayer(encP, tileP, lyrno, outP, errorP);
    }
}



static void
performTier2Coding(jpc_enc_t *      const encP,
                   uint_fast32_t    const numlyrs,
                   uint_fast32_t *  const cumlens,
                   const char **    const errorP) {
/*----------------------------------------------------------------------------
   Encode in 'numlyrs' layers, such that at each layer L, the size is
   cumlens[L].
-----------------------------------------------------------------------------*/
    jpc_enc_tile_t * const tileP = encP->curtile;

    jas_stream_t * outP;
    uint_fast32_t lyrno;
    jpc_flt_t mnrdslope;
    jpc_flt_t mxrdslope;

    validateCumlensIncreases(cumlens, numlyrs);

    outP = jas_stream_memopen(0, 0);

    if (!outP)
        pm_asprintf(errorP, "jas_stream_memopen() failed");
    else {
        findMinMaxRDSlopeValues(tileP, &mnrdslope, &mxrdslope);

        jpc_init_t2state(encP, 1);

        for (lyrno = 0, *errorP = NULL;
             lyrno < numlyrs && !*errorP;
             ++lyrno) {
            doLayer(encP, tileP, lyrno, cumlens[lyrno],
                    mnrdslope, mxrdslope, outP, errorP);
        }

        if (!*errorP) {
            trace_layeringinfo(encP);

            jas_stream_close(outP);
        }
    }
    JAS_DBGLOG(10, ("done doing rateallocation\n"));
}





static void
encodeTileBody(jpc_enc_t *   const encoderP,
               long          const tilehdrlen,
               const char ** const errorP) {
/*----------------------------------------------------------------------------
   Encode the body of encoder *encoderP's current tile, writing the encoded
   result to the encoder's output stream.

   Assume the tile header is already in that stream, and its length is
   'tilehdrlen'.
-----------------------------------------------------------------------------*/
    jpc_enc_tile_t * const tileP = encoderP->curtile;
    jpc_enc_cp_t *   const cp    = encoderP->cp;

    int rc;

    rc = jpc_enc_enccblks(encoderP);
    if (rc != 0)
        pm_asprintf(errorP, "jpc_enc_enccblks() failed");
    else {
        double const rho =
            (double) (tileP->brx - tileP->tlx) * (tileP->bry - tileP->tly) /
            ((cp->refgrdwidth - cp->imgareatlx) * (cp->refgrdheight -
                                                   cp->imgareatly));
        const char * error;

        tileP->rawsize = cp->rawsize * rho;

        computeLayerSizes(encoderP, tileP, cp, rho, tilehdrlen);

        performTier2Coding(encoderP, tileP->numlyrs, tileP->lyrsizes, &error);

        if (error) {
            pm_asprintf(errorP, "Tier 2 coding failed.  %s", error);
            pm_strfree(error);
        } else {
            int rc;

            rc = jpc_enc_encpkts(encoderP, encoderP->tmpstream);
            if (rc != 0)
                pm_asprintf(errorP, "jpc_enc_encpkts() failed\n");
            else
                *errorP = NULL;
        }
    }
}



static void
quantizeBand(jpc_enc_band_t * const bandP,
             jpc_enc_tile_t * const tileP,
             jpc_enc_cp_t *   const cp,
             int              const prec,
             int              const tccp_numgbits,
             int *            const numgbitsP) {

    if (bandP->data) {
        int actualnumbps;
        uint_fast32_t y;
        jpc_fix_t mxmag;

        for (y = 0, actualnumbps = 0, mxmag = 0;
             y < jas_matrix_numrows(bandP->data);
             ++y) {
            uint_fast32_t x;

            for (x = 0; x < jas_matrix_numcols(bandP->data); ++x)
                mxmag = MAX(mxmag, abs(jas_matrix_get(bandP->data, y, x)));
        }
        if (tileP->intmode)
            actualnumbps = jpc_firstone(mxmag) + 1;
        else
            actualnumbps = jpc_firstone(mxmag) + 1 - JPC_FIX_FRACBITS;

        *numgbitsP = actualnumbps - (prec - 1 + bandP->analgain);

        if (!tileP->intmode) {
            bandP->absstepsize =
                jpc_fix_div(
                    jpc_inttofix(1 << (bandP->analgain + 1)),
                    bandP->synweight);
        } else {
            bandP->absstepsize = jpc_inttofix(1);
        }
        bandP->stepsize = jpc_abstorelstepsize(
            bandP->absstepsize, prec + bandP->analgain);
        /* I couldn't figure out what the calculation with tccp_numgbits and
           stepsize does (or even what a step size is), but there is an
           assertion elsewhere than the number here is at least at large as
           the 'numbps' value for every code block, which means
           'actualnumbps'.  In practice, we saw that not be true, so we added
           the code to make 'actualnumbps' the floor here in hopes that would
           fix the problem.  But with the change, the image that caused the
           assertion failure produces incorrect output.  22.11.07
        */
        bandP->numbps =
            MAX(actualnumbps,
                tccp_numgbits + JPC_QCX_GETEXPN(bandP->stepsize) - 1);

        if (!tileP->intmode && bandP->data)
            quantize(bandP->data, bandP->absstepsize);
    } else
        *numgbitsP = 0;
}



static int
encodemainbody(jpc_enc_t *enc) {

    int tileno;
    int i;
    jpc_sot_t *sot;
    int rlvlno;
    jpc_qcc_t *qcc;
    jpc_cod_t *cod;
    int adjust;
    int j;
    int absbandno;
    long tilehdrlen;
    long tilelen;
    jpc_enc_tile_t *tile;
    jpc_enc_cp_t *cp;
    int samestepsizes;
    jpc_enc_ccp_t *ccps;
    jpc_enc_tccp_t *tccp;
    int mingbits; /* Minimum number of guard bits needed */
    const char * error;

    cp = enc->cp;

    for (tileno = 0; tileno < cp->numtiles; ++tileno) {
        uint_fast16_t cmptno;

        enc->curtile = jpc_enc_tile_create(enc->cp, enc->image, tileno);
        if (!enc->curtile)
            abort();

        tile = enc->curtile;

        if (jas_getdbglevel() >= 10) {
            jpc_enc_dump(enc);
        }

        for (cmptno = 0; cmptno < tile->numtcmpts; ++cmptno) {

            jpc_enc_tcmpt_t * const comp = &tile->tcmpts[cmptno];

            if (!cp->ccps[cmptno].sgnd) {
                adjust = 1 << (cp->ccps[cmptno].prec - 1);
                for (i = 0; i < jas_matrix_numrows(comp->data); ++i) {
                    for (j = 0; j < jas_matrix_numcols(comp->data); ++j) {
                        *jas_matrix_getref(comp->data, i, j) -= adjust;
                    }
                }
            }
        }

        if (!tile->intmode) {
            uint_fast16_t cmptno;
            for (cmptno = 0; cmptno < tile->numtcmpts; ++cmptno) {
                jpc_enc_tcmpt_t * const comp = &tile->tcmpts[cmptno];
                jas_matrix_asl(comp->data, JPC_FIX_FRACBITS);
            }
        }

        switch (tile->mctid) {
        case JPC_MCT_RCT:
            assert(jas_image_numcmpts(enc->image) == 3);
            jpc_rct(tile->tcmpts[0].data, tile->tcmpts[1].data,
                    tile->tcmpts[2].data);
            break;
        case JPC_MCT_ICT:
            assert(jas_image_numcmpts(enc->image) == 3);
            jpc_ict(tile->tcmpts[0].data, tile->tcmpts[1].data,
                    tile->tcmpts[2].data);
            break;
        default:
            break;
        }

        for (i = 0; i < jas_image_numcmpts(enc->image); ++i) {
            jpc_enc_tcmpt_t * const comp = &tile->tcmpts[i];
            jpc_tsfb_analyze(comp->tsfb,
                             ((comp->qmfbid == JPC_COX_RFT) ?
                              JPC_TSFB_RITIMODE : 0), comp->data);

        }

        for (cmptno = 0; cmptno < tile->numtcmpts; ++cmptno) {

            jpc_enc_tcmpt_t * const comp = &tile->tcmpts[cmptno];

            mingbits = 0;
            absbandno = 0;
            /* All bands must have a corresponding quantizer step size,
               even if they contain no samples and are never coded. */
            /* Some bands may not be hit by the loop below, so we must
               initialize all of the step sizes to a sane value. */
            memset(comp->stepsizes, 0, sizeof(comp->stepsizes));
            for (rlvlno = 0; rlvlno < comp->numrlvls; ++rlvlno) {
                jpc_enc_rlvl_t * const lvl = &comp->rlvls[rlvlno];

                unsigned int bandno;

                if (!lvl->bands) {
                    absbandno += rlvlno ? 3 : 1;
                    continue;
                }
                for (bandno = 0; bandno < lvl->numbands; ++bandno) {
                    jpc_enc_band_t * const band = &lvl->bands[bandno];

                    int numgbits;

                    quantizeBand(band, tile, cp,
                                 cp->ccps[cmptno].prec,
                                 cp->tccp.numgbits,
                                 &numgbits);

                    mingbits = MAX(mingbits, numgbits);

                    comp->stepsizes[absbandno] = band->stepsize;

                    ++absbandno;
                }
            }

            assert(JPC_FIX_FRACBITS >= JPC_NUMEXTRABITS);
            if (!tile->intmode) {
                jas_matrix_divpow2(comp->data,
                                   JPC_FIX_FRACBITS - JPC_NUMEXTRABITS);
            } else {
                jas_matrix_asl(comp->data, JPC_NUMEXTRABITS);
            }
        }

        if (mingbits > cp->tccp.numgbits) {
            fprintf(stderr, "error: too few guard bits (need at least %d)\n",
                    mingbits);
            return -1;
        }

        enc->tmpstream = jas_stream_memopen(0, 0);
        if (!enc->tmpstream) {
            fprintf(stderr, "cannot open tmp file\n");
            return -1;
        }

        /* Write the tile header. */
        enc->mrk = jpc_ms_create(JPC_MS_SOT);
        if (!enc->mrk)
            return -1;
        sot = &enc->mrk->parms.sot;
        sot->len = 0;
        sot->tileno = tileno;
        sot->partno = 0;
        sot->numparts = 1;
        if (jpc_putms(enc->tmpstream, enc->cstate, enc->mrk)) {
            fprintf(stderr, "cannot write SOT marker\n");
            return -1;
        }
        jpc_ms_destroy(enc->mrk);
        enc->mrk = 0;

/************************************************************************/
/************************************************************************/
/************************************************************************/

        tccp = &cp->tccp;
        for (cmptno = 0; cmptno < cp->numcmpts; ++cmptno) {
            jpc_enc_tcmpt_t * const comp = &tile->tcmpts[cmptno];
            jpc_enc_tcmpt_t * const comp0 = &tile->tcmpts[0];

            if (comp->numrlvls != tccp->maxrlvls) {
                if (!(enc->mrk = jpc_ms_create(JPC_MS_COD))) {
                    return -1;
                }
                /* XXX = this is not really correct. we are using comp #0's
                   precint sizes and other characteristics */
                cod = &enc->mrk->parms.cod;
                cod->compparms.csty = 0;
                cod->compparms.numdlvls = comp0->numrlvls - 1;
                cod->prg = tile->prg;
                cod->numlyrs = tile->numlyrs;
                cod->compparms.cblkwidthval =
                    JPC_COX_CBLKSIZEEXPN(comp0->cblkwidthexpn);
                cod->compparms.cblkheightval =
                    JPC_COX_CBLKSIZEEXPN(comp0->cblkheightexpn);
                cod->compparms.cblksty = comp0->cblksty;
                cod->compparms.qmfbid = comp0->qmfbid;
                cod->mctrans = (tile->mctid != JPC_MCT_NONE);
                for (i = 0; i < comp0->numrlvls; ++i) {
                    cod->compparms.rlvls[i].parwidthval =
                        comp0->rlvls[i].prcwidthexpn;
                    cod->compparms.rlvls[i].parheightval =
                        comp0->rlvls[i].prcheightexpn;
                }
                if (jpc_putms(enc->tmpstream, enc->cstate, enc->mrk)) {
                    return -1;
                }
                jpc_ms_destroy(enc->mrk);
                enc->mrk = 0;
            }
        }

        for (cmptno = 0; cmptno < cp->numcmpts; ++cmptno) {
            jpc_enc_tcmpt_t * const comp = &tile->tcmpts[cmptno];

            ccps = &cp->ccps[cmptno];
            if (ccps->numstepsizes == comp->numstepsizes) {
                unsigned int bandno;
                samestepsizes = 1;
                for (bandno = 0; bandno < ccps->numstepsizes; ++bandno) {
                    if (ccps->stepsizes[bandno] != comp->stepsizes[bandno]) {
                        samestepsizes = 0;
                        break;
                    }
                }
            } else {
                samestepsizes = 0;
            }
            if (!samestepsizes) {
                if (!(enc->mrk = jpc_ms_create(JPC_MS_QCC))) {
                    return -1;
                }
                qcc = &enc->mrk->parms.qcc;
                qcc->compno = cmptno;
                qcc->compparms.numguard = cp->tccp.numgbits;
                qcc->compparms.qntsty = (comp->qmfbid == JPC_COX_INS) ?
                    JPC_QCX_SEQNT : JPC_QCX_NOQNT;
                qcc->compparms.numstepsizes = comp->numstepsizes;
                qcc->compparms.stepsizes = comp->stepsizes;
                if (jpc_putms(enc->tmpstream, enc->cstate, enc->mrk)) {
                    return -1;
                }
                qcc->compparms.stepsizes = 0;
                jpc_ms_destroy(enc->mrk);
                enc->mrk = 0;
            }
        }

        /* Write a SOD marker to indicate the end of the tile header. */
        if (!(enc->mrk = jpc_ms_create(JPC_MS_SOD))) {
            return -1;
        }
        if (jpc_putms(enc->tmpstream, enc->cstate, enc->mrk)) {
            fprintf(stderr, "cannot write SOD marker\n");
            return -1;
        }
        jpc_ms_destroy(enc->mrk);
        enc->mrk = 0;
        tilehdrlen = jas_stream_getrwcount(enc->tmpstream);

/************************************************************************/
/************************************************************************/
/************************************************************************/

        encodeTileBody(enc, tilehdrlen, &error);
            /* Encodes current tile; writes to output file */

        if (error) {
            fprintf(stderr, "Failed to encode body of tile %u.  %s\n",
                    tileno, error);
            pm_strfree(error);
            return -1;
        }
        tilelen = jas_stream_tell(enc->tmpstream);

        if (jas_stream_seek(enc->tmpstream, 6, SEEK_SET) < 0) {
            return -1;
        }
        jpc_putuint32(enc->tmpstream, tilelen);

        if (jas_stream_seek(enc->tmpstream, 0, SEEK_SET) < 0) {
            return -1;
        }
        if (jpc_putdata(enc->out, enc->tmpstream, -1)) {
            return -1;
        }
        enc->len += tilelen;

        jas_stream_close(enc->tmpstream);
        enc->tmpstream = 0;

        jpc_enc_tile_destroy(enc->curtile);
        enc->curtile = 0;

    }

    return 0;
}



int
jpc_encode(jas_image_t *image, jas_stream_t *out, char *optstr) {

    jpc_enc_t *enc;
    jpc_enc_cp_t *cp;

    enc = 0;
    cp = 0;

    jpc_initluts();

    if (!(cp = cp_create(optstr, image))) {
        fprintf(stderr, "invalid JP encoder options\n");
        goto error;
    }

    if (!(enc = jpc_enc_create(cp, out, image))) {
        goto error;
    }
    cp = 0;

    /* Encode the main header. */
    if (encodemainhdr(enc)) {
        goto error;
    }

    /* Encode the main body.  This constitutes most of the encoding work. */
    if (encodemainbody(enc)) {
        goto error;
    }

    /* Write EOC marker segment. */
    if (!(enc->mrk = jpc_ms_create(JPC_MS_EOC))) {
        goto error;
    }
    if (jpc_putms(enc->out, enc->cstate, enc->mrk)) {
        fprintf(stderr, "cannot write EOI marker\n");
        goto error;
    }
    jpc_ms_destroy(enc->mrk);
    enc->mrk = 0;

    if (jas_stream_flush(enc->out)) {
        goto error;
    }

    jpc_enc_destroy(enc);

    return 0;

error:
    if (cp) {
        cp_destroy(cp);
    }
    if (enc) {
        jpc_enc_destroy(enc);
    }
    return -1;
}



/*****************************************************************************\
* Encoder constructor and destructor.
\*****************************************************************************/

jpc_enc_t *
jpc_enc_create(jpc_enc_cp_t *cp, jas_stream_t *out, jas_image_t *image) {

    jpc_enc_t *enc;

    enc = 0;

    if (!(enc = jas_malloc(sizeof(jpc_enc_t)))) {
        goto error;
    }

    enc->image = image;
    enc->out = out;
    enc->cp = cp;
    enc->cstate = 0;
    enc->tmpstream = 0;
    enc->mrk = 0;
    enc->curtile = 0;

    if (!(enc->cstate = jpc_cstate_create())) {
        goto error;
    }
    enc->len = 0;
    enc->mainbodysize = 0;

    return enc;

error:

    if (enc) {
        jpc_enc_destroy(enc);
    }
    return 0;
}



void
jpc_enc_destroy(jpc_enc_t *enc) {

    /* The image object (i.e., enc->image) and output stream object
       (i.e., enc->out) are created outside of the encoder.
       Therefore, they must not be destroyed here.
    */

    if (enc->curtile) {
        jpc_enc_tile_destroy(enc->curtile);
    }
    if (enc->cp) {
        cp_destroy(enc->cp);
    }
    if (enc->cstate) {
        jpc_cstate_destroy(enc->cstate);
    }
    if (enc->tmpstream) {
        jas_stream_close(enc->tmpstream);
    }

    jas_free(enc);
}



void
quantize(jas_matrix_t *data, jpc_fix_t stepsize) {

    int i;
    int j;
    jpc_fix_t t;

    if (stepsize == jpc_inttofix(1)) {
        return;
    }

    for (i = 0; i < jas_matrix_numrows(data); ++i) {
        for (j = 0; j < jas_matrix_numcols(data); ++j) {
            t = jas_matrix_get(data, i, j);

{
    if (t < 0) {
        t = jpc_fix_neg(jpc_fix_div(jpc_fix_neg(t), stepsize));
    } else {
        t = jpc_fix_div(t, stepsize);
    }
}

            jas_matrix_set(data, i, j, t);
        }
    }
}



/*****************************************************************************\
* Tile constructors and destructors.
\*****************************************************************************/

jpc_enc_tile_t *
jpc_enc_tile_create(jpc_enc_cp_t *cp, jas_image_t *image, int tileno) {

    jpc_enc_tile_t *tile;
    uint_fast32_t htileno;
    uint_fast32_t vtileno;
    uint_fast16_t lyrno;
    uint_fast16_t cmptno;
    jpc_enc_tcmpt_t *tcmpt;

    if (!(tile = jas_malloc(sizeof(jpc_enc_tile_t)))) {
        goto error;
    }

    /* Initialize a few members used in error recovery. */
    tile->tcmpts = 0;
    tile->lyrsizes = 0;
    tile->numtcmpts = cp->numcmpts;
    tile->pi = 0;

    tile->tileno = tileno;
    htileno = tileno % cp->numhtiles;
    vtileno = tileno / cp->numhtiles;

    /* Calculate the coordinates of the top-left and bottom-right
      corners of the tile. */
    tile->tlx = JAS_MAX(cp->tilegrdoffx + htileno * cp->tilewidth,
      cp->imgareatlx);
    tile->tly = JAS_MAX(cp->tilegrdoffy + vtileno * cp->tileheight,
      cp->imgareatly);
    tile->brx = JAS_MIN(cp->tilegrdoffx + (htileno + 1) * cp->tilewidth,
      cp->refgrdwidth);
    tile->bry = JAS_MIN(cp->tilegrdoffy + (vtileno + 1) * cp->tileheight,
      cp->refgrdheight);

    /* Initialize some tile coding parameters. */
    tile->intmode = cp->tcp.intmode;
    tile->csty = cp->tcp.csty;
    tile->prg = cp->tcp.prg;
    tile->mctid = cp->tcp.mctid;

    tile->numlyrs = cp->tcp.numlyrs;
    if (!(tile->lyrsizes = jas_malloc(tile->numlyrs *
      sizeof(uint_fast32_t)))) {
        goto error;
    }
    for (lyrno = 0; lyrno < tile->numlyrs; ++lyrno) {
        tile->lyrsizes[lyrno] = 0;
    }

    /* Allocate an array for the per-tile-component information. */
    if (!(tile->tcmpts = jas_malloc(cp->numcmpts * sizeof(jpc_enc_tcmpt_t)))) {
        goto error;
    }
    /* Initialize a few members critical for error recovery. */
    for (cmptno = 0, tcmpt = tile->tcmpts; cmptno < cp->numcmpts;
      ++cmptno, ++tcmpt) {
        tcmpt->rlvls = 0;
        tcmpt->tsfb = 0;
        tcmpt->data = 0;
    }
    /* Initialize the per-tile-component information. */
    for (cmptno = 0, tcmpt = tile->tcmpts; cmptno < cp->numcmpts;
      ++cmptno, ++tcmpt) {
        if (!tcmpt_create(tcmpt, cp, image, tile)) {
            goto error;
        }
    }

    /* Initialize the synthesis weights for the MCT. */
    switch (tile->mctid) {
    case JPC_MCT_RCT:
        tile->tcmpts[0].synweight = jpc_dbltofix(sqrt(3.0));
        tile->tcmpts[1].synweight = jpc_dbltofix(sqrt(0.6875));
        tile->tcmpts[2].synweight = jpc_dbltofix(sqrt(0.6875));
        break;
    case JPC_MCT_ICT:
        tile->tcmpts[0].synweight = jpc_dbltofix(sqrt(3.0000));
        tile->tcmpts[1].synweight = jpc_dbltofix(sqrt(3.2584));
        tile->tcmpts[2].synweight = jpc_dbltofix(sqrt(2.4755));
        break;
    default:
    case JPC_MCT_NONE:
        for (cmptno = 0, tcmpt = tile->tcmpts; cmptno < cp->numcmpts;
          ++cmptno, ++tcmpt) {
            tcmpt->synweight = JPC_FIX_ONE;
        }
        break;
    }

    if (!(tile->pi = jpc_enc_pi_create(cp, tile))) {
        goto error;
    }

    return tile;

error:

    if (tile) {
        jpc_enc_tile_destroy(tile);
    }
    return 0;
}



void
jpc_enc_tile_destroy(jpc_enc_tile_t *tile) {

    jpc_enc_tcmpt_t *tcmpt;
    uint_fast16_t cmptno;

    if (tile->tcmpts) {
        for (cmptno = 0, tcmpt = tile->tcmpts; cmptno <
          tile->numtcmpts; ++cmptno, ++tcmpt) {
            tcmpt_destroy(tcmpt);
        }
        jas_free(tile->tcmpts);
    }
    if (tile->lyrsizes) {
        jas_free(tile->lyrsizes);
    }
    if (tile->pi) {
        jpc_pi_destroy(tile->pi);
    }
    jas_free(tile);
}



static jpc_enc_tcmpt_t *
tcmpt_create(jpc_enc_tcmpt_t * const tcmpt,
             jpc_enc_cp_t *    const cp,
             jas_image_t *     const image,
             jpc_enc_tile_t *  const tile) {

    uint_fast16_t cmptno;
    uint_fast16_t rlvlno;
    jpc_enc_rlvl_t *rlvl;
    uint_fast32_t tlx;
    uint_fast32_t tly;
    uint_fast32_t brx;
    uint_fast32_t bry;
    uint_fast32_t cmpttlx;
    uint_fast32_t cmpttly;
    jpc_enc_ccp_t *ccp;
    jpc_tsfb_band_t bandinfos[JPC_MAXBANDS];

    tcmpt->tile = tile;
    tcmpt->tsfb = 0;
    tcmpt->data = 0;
    tcmpt->rlvls = 0;

    /* Deduce the component number. */
    cmptno = tcmpt - tile->tcmpts;

    ccp = &cp->ccps[cmptno];

    /* Compute the coordinates of the top-left and bottom-right
      corners of this tile-component. */
    tlx = JPC_CEILDIV(tile->tlx, ccp->sampgrdstepx);
    tly = JPC_CEILDIV(tile->tly, ccp->sampgrdstepy);
    brx = JPC_CEILDIV(tile->brx, ccp->sampgrdstepx);
    bry = JPC_CEILDIV(tile->bry, ccp->sampgrdstepy);

    /* Create a sequence to hold the tile-component sample data. */
    tcmpt->data = jas_seq2d_create(tlx, tly, brx, bry);
    if (!tcmpt->data)
        goto error;

    /* Get the image data associated with this tile-component. */
    cmpttlx = JPC_CEILDIV(cp->imgareatlx, ccp->sampgrdstepx);
    cmpttly = JPC_CEILDIV(cp->imgareatly, ccp->sampgrdstepy);
    if (jas_image_readcmpt(image, cmptno, tlx - cmpttlx, tly - cmpttly,
                           brx - tlx, bry - tly, tcmpt->data)) {
        goto error;
    }

    tcmpt->synweight = 0;
    tcmpt->qmfbid = cp->tccp.qmfbid;
    tcmpt->numrlvls = cp->tccp.maxrlvls;
    tcmpt->numbands = 3 * tcmpt->numrlvls - 2;

    tcmpt->tsfb = jpc_cod_gettsfb(tcmpt->qmfbid, tcmpt->numrlvls - 1);

    if (!tcmpt->tsfb)
        goto error;

    for (rlvlno = 0; rlvlno < tcmpt->numrlvls; ++rlvlno) {
        tcmpt->prcwidthexpns[rlvlno] = cp->tccp.prcwidthexpns[rlvlno];
        tcmpt->prcheightexpns[rlvlno] = cp->tccp.prcheightexpns[rlvlno];
    }
    tcmpt->cblkwidthexpn = cp->tccp.cblkwidthexpn;
    tcmpt->cblkheightexpn = cp->tccp.cblkheightexpn;
    tcmpt->cblksty = cp->tccp.cblksty;
    tcmpt->csty = cp->tccp.csty;

    tcmpt->numstepsizes = tcmpt->numbands;
    assert(tcmpt->numstepsizes <= JPC_MAXBANDS);
    memset(tcmpt->stepsizes, 0,
           sizeof(tcmpt->numstepsizes * sizeof(uint_fast16_t)));

    /* Retrieve information about the various bands. */
    jpc_tsfb_getbands(tcmpt->tsfb, jas_seq2d_xstart(tcmpt->data),
                      jas_seq2d_ystart(tcmpt->data),
                      jas_seq2d_xend(tcmpt->data),
                      jas_seq2d_yend(tcmpt->data), bandinfos);

    tcmpt->rlvls = jas_malloc(tcmpt->numrlvls * sizeof(jpc_enc_rlvl_t));
    if (!tcmpt->rlvls)
        goto error;

    for (rlvlno = 0, rlvl = tcmpt->rlvls; rlvlno < tcmpt->numrlvls;
      ++rlvlno, ++rlvl) {
        rlvl->bands = 0;
        rlvl->tcmpt = tcmpt;
    }
    for (rlvlno = 0, rlvl = tcmpt->rlvls; rlvlno < tcmpt->numrlvls;
      ++rlvlno, ++rlvl) {
        if (!rlvl_create(rlvl, cp, tcmpt, bandinfos)) {
            goto error;
        }
    }

    return tcmpt;

error:

    tcmpt_destroy(tcmpt);
    return 0;

}



static void
tcmpt_destroy(jpc_enc_tcmpt_t *tcmpt) {

    jpc_enc_rlvl_t *rlvl;
    uint_fast16_t rlvlno;

    if (tcmpt->rlvls) {
        for (rlvlno = 0, rlvl = tcmpt->rlvls; rlvlno < tcmpt->numrlvls;
          ++rlvlno, ++rlvl) {
            rlvl_destroy(rlvl);
        }
        jas_free(tcmpt->rlvls);
    }

    if (tcmpt->data) {
        jas_seq2d_destroy(tcmpt->data);
    }
    if (tcmpt->tsfb) {
        jpc_tsfb_destroy(tcmpt->tsfb);
    }
}



static jpc_enc_rlvl_t *
rlvl_create(jpc_enc_rlvl_t *rlvl, jpc_enc_cp_t *cp,
            jpc_enc_tcmpt_t *tcmpt, jpc_tsfb_band_t *bandinfos) {

    uint_fast16_t rlvlno;
    uint_fast32_t tlprctlx;
    uint_fast32_t tlprctly;
    uint_fast32_t brprcbrx;
    uint_fast32_t brprcbry;
    uint_fast16_t bandno;
    jpc_enc_band_t *band;

    /* Deduce the resolution level. */
    rlvlno = rlvl - tcmpt->rlvls;

    /* Initialize members required for error recovery. */
    rlvl->bands = 0;
    rlvl->tcmpt = tcmpt;

    /* Compute the coordinates of the top-left and bottom-right
      corners of the tile-component at this resolution. */
    rlvl->tlx =
        JPC_CEILDIVPOW2(jas_seq2d_xstart(tcmpt->data), tcmpt->numrlvls -
                        1 - rlvlno);
    rlvl->tly =
        JPC_CEILDIVPOW2(jas_seq2d_ystart(tcmpt->data), tcmpt->numrlvls -
                        1 - rlvlno);
    rlvl->brx =
        JPC_CEILDIVPOW2(jas_seq2d_xend(tcmpt->data), tcmpt->numrlvls -
                        1 - rlvlno);
    rlvl->bry =
        JPC_CEILDIVPOW2(jas_seq2d_yend(tcmpt->data), tcmpt->numrlvls -
                        1 - rlvlno);

    if (rlvl->tlx >= rlvl->brx || rlvl->tly >= rlvl->bry) {
        rlvl->numhprcs = 0;
        rlvl->numvprcs = 0;
        rlvl->numprcs = 0;
        return rlvl;
    }

    rlvl->numbands = (!rlvlno) ? 1 : 3;
    rlvl->prcwidthexpn = cp->tccp.prcwidthexpns[rlvlno];
    rlvl->prcheightexpn = cp->tccp.prcheightexpns[rlvlno];
    if (!rlvlno) {
        rlvl->cbgwidthexpn = rlvl->prcwidthexpn;
        rlvl->cbgheightexpn = rlvl->prcheightexpn;
    } else {
        rlvl->cbgwidthexpn = rlvl->prcwidthexpn - 1;
        rlvl->cbgheightexpn = rlvl->prcheightexpn - 1;
    }
    rlvl->cblkwidthexpn =
        JAS_MIN(cp->tccp.cblkwidthexpn, rlvl->cbgwidthexpn);
    rlvl->cblkheightexpn =
        JAS_MIN(cp->tccp.cblkheightexpn, rlvl->cbgheightexpn);

    /* Compute the number of precincts. */
    tlprctlx = JPC_FLOORTOMULTPOW2(rlvl->tlx, rlvl->prcwidthexpn);
    tlprctly = JPC_FLOORTOMULTPOW2(rlvl->tly, rlvl->prcheightexpn);
    brprcbrx = JPC_CEILTOMULTPOW2(rlvl->brx, rlvl->prcwidthexpn);
    brprcbry = JPC_CEILTOMULTPOW2(rlvl->bry, rlvl->prcheightexpn);
    rlvl->numhprcs = JPC_FLOORDIVPOW2(brprcbrx - tlprctlx, rlvl->prcwidthexpn);
    rlvl->numvprcs =
        JPC_FLOORDIVPOW2(brprcbry - tlprctly, rlvl->prcheightexpn);
    rlvl->numprcs = rlvl->numhprcs * rlvl->numvprcs;

    rlvl->bands = jas_malloc(rlvl->numbands * sizeof(jpc_enc_band_t));
    if (!rlvl->bands)
        goto error;

    for (bandno = 0, band = rlvl->bands; bandno < rlvl->numbands;
         ++bandno, ++band) {
        band->prcs = 0;
        band->data = 0;
        band->rlvl = rlvl;
    }
    for (bandno = 0, band = rlvl->bands; bandno < rlvl->numbands;
         ++bandno, ++band) {
        if (!band_create(band, cp, rlvl, bandinfos)) {
            goto error;
        }
    }

    return rlvl;
error:

    rlvl_destroy(rlvl);
    return 0;
}



static void
rlvl_destroy(jpc_enc_rlvl_t *rlvl) {

    jpc_enc_band_t *band;
    uint_fast16_t bandno;

    if (rlvl->bands) {
        for (bandno = 0, band = rlvl->bands; bandno < rlvl->numbands;
          ++bandno, ++band) {
            band_destroy(band);
        }
        jas_free(rlvl->bands);
    }
}



static jpc_enc_band_t *
band_create(jpc_enc_band_t *band, jpc_enc_cp_t *cp,
            jpc_enc_rlvl_t *rlvl, jpc_tsfb_band_t *bandinfos) {

    uint_fast16_t bandno;
    uint_fast16_t gblbandno;
    uint_fast16_t rlvlno;
    jpc_tsfb_band_t *bandinfo;
    jpc_enc_tcmpt_t *tcmpt;
    uint_fast32_t prcno;
    jpc_enc_prc_t *prc;

    tcmpt = rlvl->tcmpt;
    band->data = 0;
    band->prcs = 0;
    band->rlvl = rlvl;

    /* Deduce the resolution level and band number. */
    rlvlno = rlvl - rlvl->tcmpt->rlvls;
    bandno = band - rlvl->bands;
    gblbandno = (!rlvlno) ? 0 : (3 * (rlvlno - 1) + bandno + 1);

    bandinfo = &bandinfos[gblbandno];

    if (bandinfo->xstart != bandinfo->xend &&
        bandinfo->ystart != bandinfo->yend) {
        band->data = jas_seq2d_create(0, 0, 0, 0);
        if (!band->data)
            goto error;
        jas_seq2d_bindsub(band->data, tcmpt->data, bandinfo->locxstart,
                          bandinfo->locystart, bandinfo->locxend,
                          bandinfo->locyend);
        jas_seq2d_setshift(band->data, bandinfo->xstart, bandinfo->ystart);
    }
    band->orient = bandinfo->orient;
    band->analgain = JPC_NOMINALGAIN(cp->tccp.qmfbid, tcmpt->numrlvls, rlvlno,
                                     band->orient);
    band->numbps = 0;
    band->absstepsize = 0;
    band->stepsize = 0;
    band->synweight = bandinfo->synenergywt;

    if (band->data) {
        band->prcs = jas_malloc(rlvl->numprcs * sizeof(jpc_enc_prc_t));
        if (!band->prcs)
            goto error;
        for (prcno = 0, prc = band->prcs;
             prcno < rlvl->numprcs;
             ++prcno, ++prc) {
            prc->cblks = 0;
            prc->incltree = 0;
            prc->nlibtree = 0;
            prc->savincltree = 0;
            prc->savnlibtree = 0;
            prc->band = band;
        }
        for (prcno = 0, prc = band->prcs;
             prcno < rlvl->numprcs;
             ++prcno, ++prc) {
            if (!prc_create(prc, cp, band)) {
                goto error;
            }
        }
    }

    return band;

error:
    band_destroy(band);
    return 0;
}



static void
band_destroy(jpc_enc_band_t *band) {

    jpc_enc_prc_t *prc;
    jpc_enc_rlvl_t *rlvl;
    uint_fast32_t prcno;

    if (band->prcs) {
        rlvl = band->rlvl;
        for (prcno = 0, prc = band->prcs; prcno < rlvl->numprcs;
          ++prcno, ++prc) {
            prc_destroy(prc);
        }
        jas_free(band->prcs);
    }
    if (band->data) {
        jas_seq2d_destroy(band->data);
    }
}



static jpc_enc_prc_t *
prc_create(jpc_enc_prc_t *prc, jpc_enc_cp_t *cp, jpc_enc_band_t *band) {

    uint_fast32_t prcno;
    uint_fast32_t prcxind;
    uint_fast32_t prcyind;
    uint_fast32_t cbgtlx;
    uint_fast32_t cbgtly;
    uint_fast32_t tlprctlx;
    uint_fast32_t tlprctly;
    uint_fast32_t tlcbgtlx;
    uint_fast32_t tlcbgtly;
    uint_fast16_t rlvlno;
    jpc_enc_rlvl_t *rlvl;
    uint_fast32_t tlcblktlx;
    uint_fast32_t tlcblktly;
    uint_fast32_t brcblkbrx;
    uint_fast32_t brcblkbry;
    uint_fast32_t cblkno;
    jpc_enc_cblk_t *cblk;
    jpc_enc_tcmpt_t *tcmpt;

    prc->cblks = 0;
    prc->incltree = 0;
    prc->savincltree = 0;
    prc->nlibtree = 0;
    prc->savnlibtree = 0;

    rlvl = band->rlvl;
    tcmpt = rlvl->tcmpt;
    rlvlno = rlvl - tcmpt->rlvls;
    prcno = prc - band->prcs;
    prcxind = prcno % rlvl->numhprcs;
    prcyind = prcno / rlvl->numhprcs;
    prc->band = band;

    tlprctlx = JPC_FLOORTOMULTPOW2(rlvl->tlx, rlvl->prcwidthexpn);
    tlprctly = JPC_FLOORTOMULTPOW2(rlvl->tly, rlvl->prcheightexpn);

    if (!rlvlno) {
        tlcbgtlx = tlprctlx;
        tlcbgtly = tlprctly;
    } else {
        tlcbgtlx = JPC_CEILDIVPOW2(tlprctlx, 1);
        tlcbgtly = JPC_CEILDIVPOW2(tlprctly, 1);
    }

    /* Compute the coordinates of the top-left and bottom-right corners of the
       precinct.
    */
    cbgtlx = tlcbgtlx + (prcxind << rlvl->cbgwidthexpn);
    cbgtly = tlcbgtly + (prcyind << rlvl->cbgheightexpn);
    prc->tlx = JAS_MAX(jas_seq2d_xstart(band->data), cbgtlx);
    prc->tly = JAS_MAX(jas_seq2d_ystart(band->data), cbgtly);
    prc->brx = JAS_MIN(jas_seq2d_xend(band->data), cbgtlx +
                       (1 << rlvl->cbgwidthexpn));
    prc->bry = JAS_MIN(jas_seq2d_yend(band->data), cbgtly +
                       (1 << rlvl->cbgheightexpn));

    if (prc->tlx < prc->brx && prc->tly < prc->bry) {
        /* The precinct contains at least one code block. */

        tlcblktlx = JPC_FLOORTOMULTPOW2(prc->tlx, rlvl->cblkwidthexpn);
        tlcblktly = JPC_FLOORTOMULTPOW2(prc->tly, rlvl->cblkheightexpn);
        brcblkbrx = JPC_CEILTOMULTPOW2(prc->brx, rlvl->cblkwidthexpn);
        brcblkbry = JPC_CEILTOMULTPOW2(prc->bry, rlvl->cblkheightexpn);
        prc->numhcblks = JPC_FLOORDIVPOW2(brcblkbrx - tlcblktlx,
                                          rlvl->cblkwidthexpn);
        prc->numvcblks = JPC_FLOORDIVPOW2(brcblkbry - tlcblktly,
                                          rlvl->cblkheightexpn);
        prc->numcblks = prc->numhcblks * prc->numvcblks;

        if (!(prc->incltree = jpc_tagtree_create(prc->numhcblks,
                                                 prc->numvcblks))) {
            goto error;
        }
        if (!(prc->nlibtree = jpc_tagtree_create(prc->numhcblks,
                                                 prc->numvcblks))) {
            goto error;
        }
        if (!(prc->savincltree = jpc_tagtree_create(prc->numhcblks,
                                                    prc->numvcblks))) {
            goto error;
        }
        if (!(prc->savnlibtree = jpc_tagtree_create(prc->numhcblks,
                                                    prc->numvcblks))) {
            goto error;
        }

        prc->cblks = jas_malloc(prc->numcblks * sizeof(jpc_enc_cblk_t));

        if (!prc->cblks)
            goto error;
        for (cblkno = 0, cblk = prc->cblks;
             cblkno < prc->numcblks;
             ++cblkno, ++cblk) {
            cblk->passes = 0;
            cblk->stream = 0;
            cblk->mqenc = 0;
            cblk->data = 0;
            cblk->flags = 0;
            cblk->prc = prc;
        }
        for (cblkno = 0, cblk = prc->cblks;
             cblkno < prc->numcblks;
             ++cblkno, ++cblk) {
            if (!cblk_create(cblk, cp, prc)) {
                goto error;
            }
        }
    } else {
        /* The precinct does not contain any code blocks. */
        prc->tlx = prc->brx;
        prc->tly = prc->bry;
        prc->numcblks = 0;
        prc->numhcblks = 0;
        prc->numvcblks = 0;
        prc->cblks = 0;
        prc->incltree = 0;
        prc->nlibtree = 0;
        prc->savincltree = 0;
        prc->savnlibtree = 0;
    }

    return prc;

error:
    prc_destroy(prc);
    return 0;
}



static void
prc_destroy(jpc_enc_prc_t *prc) {

    jpc_enc_cblk_t *cblk;
    uint_fast32_t cblkno;

    if (prc->cblks) {
        for (cblkno = 0, cblk = prc->cblks; cblkno < prc->numcblks;
          ++cblkno, ++cblk) {
            cblk_destroy(cblk);
        }
        jas_free(prc->cblks);
    }
    if (prc->incltree) {
        jpc_tagtree_destroy(prc->incltree);
    }
    if (prc->nlibtree) {
        jpc_tagtree_destroy(prc->nlibtree);
    }
    if (prc->savincltree) {
        jpc_tagtree_destroy(prc->savincltree);
    }
    if (prc->savnlibtree) {
        jpc_tagtree_destroy(prc->savnlibtree);
    }
}



static jpc_enc_cblk_t *
cblk_create(jpc_enc_cblk_t *cblk, jpc_enc_cp_t *cp, jpc_enc_prc_t *prc) {

    jpc_enc_band_t *band;
    uint_fast32_t cblktlx;
    uint_fast32_t cblktly;
    uint_fast32_t cblkbrx;
    uint_fast32_t cblkbry;
    jpc_enc_rlvl_t *rlvl;
    uint_fast32_t cblkxind;
    uint_fast32_t cblkyind;
    uint_fast32_t cblkno;
    uint_fast32_t tlcblktlx;
    uint_fast32_t tlcblktly;

    cblkno = cblk - prc->cblks;
    cblkxind = cblkno % prc->numhcblks;
    cblkyind = cblkno / prc->numhcblks;
    rlvl = prc->band->rlvl;
    cblk->prc = prc;

    cblk->numpasses = 0;
    cblk->passes = 0;
    cblk->numencpasses = 0;
    cblk->numimsbs = 0;
    cblk->numlenbits = 0;
    cblk->stream = 0;
    cblk->mqenc = 0;
    cblk->flags = 0;
    cblk->numbps = 0;
    cblk->curpass = 0;
    cblk->data = 0;
    cblk->savedcurpass = 0;
    cblk->savednumlenbits = 0;
    cblk->savednumencpasses = 0;

    band = prc->band;
    tlcblktlx = JPC_FLOORTOMULTPOW2(prc->tlx, rlvl->cblkwidthexpn);
    tlcblktly = JPC_FLOORTOMULTPOW2(prc->tly, rlvl->cblkheightexpn);
    cblktlx =
        JAS_MAX(tlcblktlx + (cblkxind << rlvl->cblkwidthexpn), prc->tlx);
    cblktly =
        JAS_MAX(tlcblktly + (cblkyind << rlvl->cblkheightexpn), prc->tly);
    cblkbrx =
        JAS_MIN(tlcblktlx + ((cblkxind + 1) << rlvl->cblkwidthexpn), prc->brx);
    cblkbry =
        JAS_MIN(tlcblktly + ((cblkyind + 1) << rlvl->cblkheightexpn),
                prc->bry);

    assert(cblktlx < cblkbrx && cblktly < cblkbry);
    cblk->data = jas_seq2d_create(0, 0, 0, 0);
    if (!cblk->data)
        goto error;
    jas_seq2d_bindsub(cblk->data, band->data,
                      cblktlx, cblktly, cblkbrx, cblkbry);

    return cblk;

error:
    cblk_destroy(cblk);
    return 0;
}



static void
cblk_destroy(jpc_enc_cblk_t *cblk) {

    uint_fast16_t passno;
    jpc_enc_pass_t *pass;
    if (cblk->passes) {
        for (passno = 0, pass = cblk->passes; passno < cblk->numpasses;
          ++passno, ++pass) {
            pass_destroy(pass);
        }
        jas_free(cblk->passes);
    }
    if (cblk->stream) {
        jas_stream_close(cblk->stream);
    }
    if (cblk->mqenc) {
        jpc_mqenc_destroy(cblk->mqenc);
    }
    if (cblk->data) {
        jas_seq2d_destroy(cblk->data);
    }
    if (cblk->flags) {
        jas_seq2d_destroy(cblk->flags);
    }
}



static void
pass_destroy(jpc_enc_pass_t *pass) {

    /* XXX - need to free resources here */
}



void
jpc_enc_dump(jpc_enc_t *enc) {

    jpc_enc_tile_t *tile;
    jpc_enc_tcmpt_t *tcmpt;
    jpc_enc_rlvl_t *rlvl;
    jpc_enc_band_t *band;
    jpc_enc_prc_t *prc;
    jpc_enc_cblk_t *cblk;
    uint_fast16_t cmptno;
    uint_fast16_t rlvlno;
    uint_fast16_t bandno;
    uint_fast32_t prcno;
    uint_fast32_t cblkno;

    tile = enc->curtile;

    for (cmptno = 0, tcmpt = tile->tcmpts;
         cmptno < tile->numtcmpts;
         ++cmptno, ++tcmpt) {
        fprintf(stderr, "  tcmpt %5d %5d %5d %5d\n",
                (int)jas_seq2d_xstart(tcmpt->data),
                (int)jas_seq2d_ystart(tcmpt->data),
                (int)jas_seq2d_xend(tcmpt->data),
                (int)jas_seq2d_yend(tcmpt->data));
        for (rlvlno = 0, rlvl = tcmpt->rlvls;
             rlvlno < tcmpt->numrlvls;
          ++rlvlno, ++rlvl) {
            fprintf(stderr, "    rlvl %5d %5d %5d %5d\n",
                    (int)rlvl->tlx, (int)rlvl->tly,
                    (int)rlvl->brx, (int)rlvl->bry);
            for (bandno = 0, band = rlvl->bands; bandno < rlvl->numbands;
              ++bandno, ++band) {
                if (!band->data) {
                    continue;
                }
                fprintf(stderr, "      band %5d %5d %5d %5d\n",
                        (int)jas_seq2d_xstart(band->data),
                        (int)jas_seq2d_ystart(band->data),
                        (int)jas_seq2d_xend(band->data),
                        (int)jas_seq2d_yend(band->data));
                for (prcno = 0, prc = band->prcs;
                     prcno < rlvl->numprcs;
                     ++prcno, ++prc) {
                    fprintf(stderr, "        prc %5d %5d %5d %5d (%5d %5d)\n",
                            (int)prc->tlx, (int)prc->tly,
                            (int)prc->brx, (int)prc->bry,
                            (int)(prc->brx - prc->tlx),
                            (int)(prc->bry - prc->tly));
                    if (!prc->cblks) {
                        continue;
                    }
                    for (cblkno = 0, cblk = prc->cblks; cblkno < prc->numcblks;
                      ++cblkno, ++cblk) {
                        fprintf(stderr, "         cblk %5d %5d %5d %5d\n",
                                (int)jas_seq2d_xstart(cblk->data),
                                (int)jas_seq2d_ystart(cblk->data),
                                (int)jas_seq2d_xend(cblk->data),
                                (int)jas_seq2d_yend(cblk->data));
                    }
                }
            }
        }
    }
}



/*
 * Copyright (c) 1999-2000 Image Power, Inc. and the University of
 *   British Columbia.
 * Copyright (c) 2001-2002 Michael David Adams.
 * All rights reserved.
 */

/* __START_OF_JASPER_LICENSE__
 *
 * JasPer Software License
 *
 * IMAGE POWER JPEG-2000 PUBLIC LICENSE
 * ************************************
 *
 * GRANT:
 *
 * Permission is hereby granted, free of charge, to any person (the "User")
 * obtaining a copy of this software and associated documentation, to deal
 * in the JasPer Software without restriction, including without limitation
 * the right to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the JasPer Software (in source and binary forms),
 * and to permit persons to whom the JasPer Software is furnished to do so,
 * provided further that the License Conditions below are met.
 *
 * License Conditions
 * ******************
 *
 * A.  Redistributions of source code must retain the above copyright notice,
 * and this list of conditions, and the following disclaimer.
 *
 * B.  Redistributions in binary form must reproduce the above copyright
 * notice, and this list of conditions, and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 *
 * C.  Neither the name of Image Power, Inc. nor any other contributor
 * (including, but not limited to, the University of British Columbia and
 * Michael David Adams) may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * D.  User agrees that it shall not commence any action against Image Power,
 * Inc., the University of British Columbia, Michael David Adams, or any
 * other contributors (collectively "Licensors") for infringement of any
 * intellectual property rights ("IPR") held by the User in respect of any
 * technology that User owns or has a right to license or sublicense and
 * which is an element required in order to claim compliance with ISO/IEC
 * 15444-1 (i.e., JPEG-2000 Part 1).  "IPR" means all intellectual property
 * rights worldwide arising under statutory or common law, and whether
 * or not perfected, including, without limitation, all (i) patents and
 * patent applications owned or licensable by User; (ii) rights associated
 * with works of authorship including copyrights, copyright applications,
 * copyright registrations, mask work rights, mask work applications,
 * mask work registrations; (iii) rights relating to the protection of
 * trade secrets and confidential information; (iv) any right analogous
 * to those set forth in subsections (i), (ii), or (iii) and any other
 * proprietary rights relating to intangible property (other than trademark,
 * trade dress, or service mark rights); and (v) divisions, continuations,
 * renewals, reissues and extensions of the foregoing (as and to the extent
 * applicable) now existing, hereafter filed, issued or acquired.
 *
 * E.  If User commences an infringement action against any Licensor(s) then
 * such Licensor(s) shall have the right to terminate User's license and
 * all sublicenses that have been granted hereunder by User to other parties.
 *
 * F.  This software is for use only in hardware or software products that
 * are compliant with ISO/IEC 15444-1 (i.e., JPEG-2000 Part 1).  No license
 * or right to this Software is granted for products that do not comply
 * with ISO/IEC 15444-1.  The JPEG-2000 Part 1 standard can be purchased
 * from the ISO.
 *
 * THIS DISCLAIMER OF WARRANTY CONSTITUTES AN ESSENTIAL PART OF THIS LICENSE.
 * NO USE OF THE JASPER SOFTWARE IS AUTHORIZED HEREUNDER EXCEPT UNDER
 * THIS DISCLAIMER.  THE JASPER SOFTWARE IS PROVIDED BY THE LICENSORS AND
 * CONTRIBUTORS UNDER THIS LICENSE ON AN ``AS-IS'' BASIS, WITHOUT WARRANTY
 * OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, WITHOUT LIMITATION,
 * WARRANTIES THAT THE JASPER SOFTWARE IS FREE OF DEFECTS, IS MERCHANTABLE,
 * IS FIT FOR A PARTICULAR PURPOSE OR IS NON-INFRINGING.  THOSE INTENDING
 * TO USE THE JASPER SOFTWARE OR MODIFICATIONS THEREOF FOR USE IN HARDWARE
 * OR SOFTWARE PRODUCTS ARE ADVISED THAT THEIR USE MAY INFRINGE EXISTING
 * PATENTS, COPYRIGHTS, TRADEMARKS, OR OTHER INTELLECTUAL PROPERTY RIGHTS.
 * THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE JASPER SOFTWARE
 * IS WITH THE USER.  SHOULD ANY PART OF THE JASPER SOFTWARE PROVE DEFECTIVE
 * IN ANY RESPECT, THE USER (AND NOT THE INITIAL DEVELOPERS, THE UNIVERSITY
 * OF BRITISH COLUMBIA, IMAGE POWER, INC., MICHAEL DAVID ADAMS, OR ANY
 * OTHER CONTRIBUTOR) SHALL ASSUME THE COST OF ANY NECESSARY SERVICING,
 * REPAIR OR CORRECTION.  UNDER NO CIRCUMSTANCES AND UNDER NO LEGAL THEORY,
 * WHETHER TORT (INCLUDING NEGLIGENCE), CONTRACT, OR OTHERWISE, SHALL THE
 * INITIAL DEVELOPER, THE UNIVERSITY OF BRITISH COLUMBIA, IMAGE POWER, INC.,
 * MICHAEL DAVID ADAMS, ANY OTHER CONTRIBUTOR, OR ANY DISTRIBUTOR OF THE
 * JASPER SOFTWARE, OR ANY SUPPLIER OF ANY OF SUCH PARTIES, BE LIABLE TO
 * THE USER OR ANY OTHER PERSON FOR ANY INDIRECT, SPECIAL, INCIDENTAL, OR
 * CONSEQUENTIAL DAMAGES OF ANY CHARACTER INCLUDING, WITHOUT LIMITATION,
 * DAMAGES FOR LOSS OF GOODWILL, WORK STOPPAGE, COMPUTER FAILURE OR
 * MALFUNCTION, OR ANY AND ALL OTHER COMMERCIAL DAMAGES OR LOSSES, EVEN IF
 * SUCH PARTY HAD BEEN INFORMED, OR OUGHT TO HAVE KNOWN, OF THE POSSIBILITY
 * OF SUCH DAMAGES.  THE JASPER SOFTWARE AND UNDERLYING TECHNOLOGY ARE NOT
 * FAULT-TOLERANT AND ARE NOT DESIGNED, MANUFACTURED OR INTENDED FOR USE OR
 * RESALE AS ON-LINE CONTROL EQUIPMENT IN HAZARDOUS ENVIRONMENTS REQUIRING
 * FAIL-SAFE PERFORMANCE, SUCH AS IN THE OPERATION OF NUCLEAR FACILITIES,
 * AIRCRAFT NAVIGATION OR COMMUNICATION SYSTEMS, AIR TRAFFIC CONTROL, DIRECT
 * LIFE SUPPORT MACHINES, OR WEAPONS SYSTEMS, IN WHICH THE FAILURE OF THE
 * JASPER SOFTWARE OR UNDERLYING TECHNOLOGY OR PRODUCT COULD LEAD DIRECTLY
 * TO DEATH, PERSONAL INJURY, OR SEVERE PHYSICAL OR ENVIRONMENTAL DAMAGE
 * ("HIGH RISK ACTIVITIES").  LICENSOR SPECIFICALLY DISCLAIMS ANY EXPRESS
 * OR IMPLIED WARRANTY OF FITNESS FOR HIGH RISK ACTIVITIES.  USER WILL NOT
 * KNOWINGLY USE, DISTRIBUTE OR RESELL THE JASPER SOFTWARE OR UNDERLYING
 * TECHNOLOGY OR PRODUCTS FOR HIGH RISK ACTIVITIES AND WILL ENSURE THAT ITS
 * CUSTOMERS AND END-USERS OF ITS PRODUCTS ARE PROVIDED WITH A COPY OF THE
 * NOTICE SPECIFIED IN THIS SECTION.
 *
 * __END_OF_JASPER_LICENSE__
 */

