/*=============================================================================
                            pamdepth
===============================================================================
  Change the maxval in a Netpbm image.

  This replaces Pnmdepth.

  By Bryan Henderson January 2006.

  Contributed to the public domain by its author.
=============================================================================*/
#include <assert.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;
    unsigned int newMaxval;
    unsigned int verbose;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec strings we return are stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "verbose",  OPT_STRING, NULL,
            &cmdlineP->verbose, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 < 1)
        pm_error("You must specify at least one argument -- the new maxval");
    else {
        int const intval = atoi(argv[1]);

        if (intval < 1)
            pm_error("New maxval must be at least 1.  You specified %d",
                     intval);
        else if (intval > PNM_OVERALLMAXVAL)
            pm_error("newmaxval (%d) is too large.  "
                     "The maximum allowed by the PNM formats is %u.",
                     intval, PNM_OVERALLMAXVAL);
        else
            cmdlineP->newMaxval = intval;

        if (argc-1 < 2)
            cmdlineP->inputFileName = "-";
        else
            cmdlineP->inputFileName = argv[2];
    }
}



static void
createSampleMap(sample   const oldMaxval,
                sample   const newMaxval,
                sample** const sampleMapP) {

    unsigned int i;

    sample * sampleMap;

    MALLOCARRAY_NOFAIL(sampleMap, oldMaxval+1);

    for (i = 0; i <= oldMaxval; ++i)
        sampleMap[i] = ROUNDDIV(i * newMaxval, oldMaxval);

    *sampleMapP = sampleMap;
}



static void
transformRaster(struct pam * const inpamP,
                struct pam * const outpamP) {

    tuple * tuplerow;
    unsigned int row;
    sample * sampleMap;  /* malloc'ed */

    createSampleMap(inpamP->maxval, outpamP->maxval, &sampleMap);

    assert(inpamP->height == outpamP->height);
    assert(inpamP->width  == outpamP->width);
    assert(inpamP->depth  == outpamP->depth);

    tuplerow = pnm_allocpamrow(inpamP);

    for (row = 0; row < inpamP->height; ++row) {
        unsigned int col;
        pnm_readpamrow(inpamP, tuplerow);

        for (col = 0; col < inpamP->width; ++col) {
            unsigned int plane;
            for (plane = 0; plane < inpamP->depth; ++plane)
                tuplerow[col][plane] = sampleMap[tuplerow[col][plane]];
        }
        pnm_writepamrow(outpamP, tuplerow);
        }

    pnm_freepamrow(tuplerow);

    free(sampleMap);
}



int
main(int argc, const char * argv[]) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    struct pam inpam;
    struct pam outpam;
    int eof;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    eof = FALSE;
    while (!eof) {
        pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

        outpam = inpam;  /* initial value */

        outpam.file = stdout;
        outpam.maxval = cmdline.newMaxval;

        if (PNM_FORMAT_TYPE(inpam.format) == PBM_TYPE) {
            outpam.format = PGM_TYPE;
        } else
            outpam.format = inpam.format;

        if (streq(inpam.tuple_type, PAM_PBM_TUPLETYPE)) {
            pm_message("promoting from black and white to grayscale");
            strcpy(outpam.tuple_type, PAM_PGM_TUPLETYPE);
        } else if (streq(inpam.tuple_type, PAM_PBM_ALPHA_TUPLETYPE)) {
            pm_message("promoting from black and white to grayscale");
            strcpy(outpam.tuple_type, PAM_PGM_ALPHA_TUPLETYPE);
        } else
            strcpy(outpam.tuple_type, inpam.tuple_type);

        pnm_writepaminit(&outpam);

        transformRaster(&inpam, &outpam);

        pnm_nextimage(ifP, &eof);
    }
    pm_close(ifP);
    pm_close(stdout);

    return 0;
}



