/*============================================================================
                                pnmindex
==============================================================================

  Build a visual index of a bunch of PNM images.

  This used to be a C shell program, and then a BASH program.  Neither
  were portable enough, and the program is too complex for either of
  those languages anyway, so now it's in C.

  By Bryan Henderson 2005.04.24.

  Contributed to the public domain by its author.

============================================================================*/

#define _DEFAULT_SOURCE /* New name for SVID & BSD source defines */
#define _C99_SOURCE  /* Make sure snprintf() is in stdio.h */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */
#define _BSD_SOURCE   /* Make sure strdup is in string.h */

#include <assert.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>


#include "pm_config.h"
#include "pm_c_util.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "nstring.h"
#include "pm_system.h"
#include "pnm.h"

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    unsigned int  inputFileCount;
    const char ** inputFileName;
    unsigned int  size;
    unsigned int  across;
    unsigned int  colors;
    unsigned int  black;
    unsigned int  noquant;
    const char *  title;
    unsigned int  verbose;
};

static bool verbose;



static const char *
shellQuote(const char * const arg) {
/*----------------------------------------------------------------------------
   A string that in a Bourne shell command forms a single token whose value
   understood by the shell is 'arg'.

   For example,

     'arg'           result
     --------------  --------------

     hello           'hello'
     two words       'two words'
     'Bryan's'       \''Bryan'\''s'\'

   Note that in the last example the result is a concatenation of two
   single-quoted strings and 3 esaped single quotes outside of those quoted
   strings.

   You can use this safely to insert an arbitrary string from an untrusted
   source into a shell command without worrying about it changing the nature
   of the shell command.

   In newly malloced storage.
-----------------------------------------------------------------------------*/
    unsigned int const worstCaseResultLen = 3 * strlen(arg) + 1;

    char * buffer;
    unsigned int i;
    unsigned int cursor;
    bool inquotes;

    MALLOCARRAY_NOFAIL(buffer, worstCaseResultLen);

    for (i = 0, cursor=0, inquotes=false; i < strlen(arg); ++i) {
        if (arg[i] == '\'') {
            if (inquotes) {
                buffer[cursor++] = '\'';  /* Close the quotation */
                inquotes = false;
            }
            assert(!inquotes);
            buffer[cursor++] = '\\';  /* Add escaped */
            buffer[cursor++] = '\'';  /*    single quote */
        } else {
            if (!inquotes) {
                buffer[cursor++] = '\'';
                inquotes = true;
            }
            assert(inquotes);
            buffer[cursor++] = arg[i];
        }
    }
    if (inquotes)
        buffer[cursor++] = '\'';   /* Close the final quotation */

    buffer[cursor++] = '\0';  /* Terminate string */

    assert(cursor <= worstCaseResultLen);

    return buffer;
}




static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {

    unsigned int option_def_index;
    optEntry *option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int quant;
    unsigned int sizeSpec, colorsSpec, acrossSpec, titleSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "black",       OPT_FLAG,   NULL,
            &cmdlineP->black,         0);
    OPTENT3(0, "noquant",     OPT_FLAG,   NULL,
            &cmdlineP->noquant,       0);
    OPTENT3(0, "quant",       OPT_FLAG,   NULL,
            &quant,                   0);
    OPTENT3(0, "verbose",     OPT_FLAG,   NULL,
            &cmdlineP->verbose,       0);
    OPTENT3(0, "size",        OPT_UINT,   &cmdlineP->size,
            &sizeSpec,                0);
    OPTENT3(0, "colors",      OPT_UINT,   &cmdlineP->colors,
            &colorsSpec,              0);
    OPTENT3(0, "across",      OPT_UINT,   &cmdlineP->across,
            &acrossSpec,              0);
    OPTENT3(0, "title",       OPT_STRING, &cmdlineP->title,
            &titleSpec,               0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdline_p and others. */

    if (quant && cmdlineP->noquant)
        pm_error("You can't specify both -quant and -noquant");

    if (colorsSpec) {
        if (cmdlineP->colors == 0)
            pm_error("-colors value must be positive");
    } else
        cmdlineP->colors = 256;

    if (sizeSpec) {
        if (cmdlineP->size == 0)
            pm_error("-size value must be positive");
    } else
        cmdlineP->size = 100;

    if (acrossSpec) {
        if (cmdlineP->across == 0)
            pm_error("-across value must be positive");
    } else
        cmdlineP->across = 6;

    if (!titleSpec)
        cmdlineP->title = NULL;

    if (colorsSpec && cmdlineP->noquant)
        pm_error("-colors doesn't make any sense with -noquant");

    if (argc-1 < 1)
        pm_error("You must name at least one file that contains an image "
                 "to go into the index");

    cmdlineP->inputFileCount = argc-1;

    MALLOCARRAY_NOFAIL(cmdlineP->inputFileName, cmdlineP->inputFileCount);

    {
        unsigned int i;
        for (i = 0; i < cmdlineP->inputFileCount; ++i) {
            cmdlineP->inputFileName[i] = strdup(argv[i+1]);
            if (cmdlineP->inputFileName[i] == NULL)
                pm_error("Unable to allocate memory for a file name");
        }
    }
}



static void
freeCmdline(struct CmdlineInfo const cmdline) {

    unsigned int i;

    for (i = 0; i < cmdline.inputFileCount; ++i)
        pm_strfree(cmdline.inputFileName[i]);

    free(cmdline.inputFileName);

}



static void
validateTmpdir(const char * const dirNm) {
/*----------------------------------------------------------------------------
   Abort program if 'dirNm' contains special characters that would cause
   trouble if included in a shell command.

   The cleanest thing to do would be to allow any characters in the directory
   name and just quote and escape the properly every time we include the name
   of a temporary file in a shell command, but we're too lazy for that.
-----------------------------------------------------------------------------*/
    if (
        strchr(dirNm, '\\') ||
        strchr(dirNm, '\'') ||
        strchr(dirNm, ' ') ||
        strchr(dirNm, '"') ||
        strchr(dirNm, '$') ||
        strchr(dirNm, '`') ||
        strchr(dirNm, '!')
        ) {
        pm_error("TMPDIR environment variable contains a shell special "
                 "character; this program cannot use that.");
    }
}



static void
makeTempDir(const char ** const tempDirP) {

    const char * const tmpdir = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";

    validateTmpdir(tmpdir);

    const char * mytmpdir;
    int rc;

    pm_asprintf(&mytmpdir, "%s/pnmindex_%d", tmpdir, getpid());

    rc = pm_mkdir(mytmpdir, 0700);
    if (rc != 0)
        pm_error("Unable to create temporary file directory '%s'.  mkdir() "
                 "fails with errno %d (%s)",
                 mytmpdir, errno, strerror(errno));

    *tempDirP = mytmpdir;
}



static void
removeTempDir(const char * const tempDir) {

    int rc;

    rc = rmdir(tempDir);
    if (rc != 0)
        pm_error("Failed to remove temporary file directory '%s'.  "
                 "rmdir() fails with errno %d (%s)",
                 tempDir, errno, strerror(errno));
}


static const char *
rowFileName(const char * const dirName,
            unsigned int const row) {

    const char * fileName;

    pm_asprintf(&fileName, "%s/pi.%u", dirName, row);

    return fileName;
}



static void
makeTitle(const char * const title,
          unsigned int const rowNumber,
          bool         const blackBackground,
          const char * const dirNm) {
/*----------------------------------------------------------------------------
   Create a PBM file containing the text 'title'.

   If 'blackBackground' is true, make it white on black; otherwise, black
   on white.

   Name the file like a thumbnail row file for row number 'rowNumber',
   in directory named 'dirNm'.
-----------------------------------------------------------------------------*/
    const char * const invertStage = blackBackground ? "| pnminvert " : "";

    const char * fileName;
    FILE * outFP;
    struct bufferDesc titleBuf;
    const char * shellCommand;
    int termStatus;

    titleBuf.size              = strlen(title);
    titleBuf.buffer            = (unsigned char *)title;
    titleBuf.bytesTransferredP = NULL;

    fileName = rowFileName(dirNm, rowNumber);
    outFP = pm_openw(fileName);
    pm_strfree(fileName);

    pm_asprintf(&shellCommand, "pbmtext %s", invertStage);

    pm_system2(&pm_feed_from_memory, &titleBuf,
               &pm_accept_to_filestream, outFP,
               shellCommand, &termStatus);

    pm_strfree(shellCommand);

    if (termStatus != 0)
        pm_error("Failed to generate title image");

    pm_close(outFP);
}



static void
copyImage(const char * const inputFileNm,
          const char * const outputFileNm) {

    int termStatus;

    pm_system2_lp("cat",
                  &pm_feed_from_file, (void*)inputFileNm,
                  &pm_accept_to_file, (void*)outputFileNm,
                  &termStatus,
                  "cat", NULL);

    if (termStatus != 0)
        pm_error("'cat' to copy image '%s' to '%s' failed, "
                 "termination status = %d",
                 inputFileNm, outputFileNm, termStatus);
}



static void
copyScaleQuantImage(const char * const inputFileNm,
                    const char * const outputFileNm,
                    int          const format,
                    unsigned int const size,
                    unsigned int const quant,
                    unsigned int const colorCt) {

    const char * scaleCommand;
    int termStatus;

    switch (PNM_FORMAT_TYPE(format)) {
    case PBM_TYPE:
        pm_asprintf(&scaleCommand,
                    "pamscale -quiet -xysize %u %u "
                    "| pamditherbw",
                    size, size);
        break;

    case PGM_TYPE:
        pm_asprintf(&scaleCommand,
                    "pamscale -quiet -xysize %u %u",
                    size, size);
        break;

    case PPM_TYPE:
        if (quant)
            pm_asprintf(&scaleCommand,
                        "pamscale -quiet -xysize %u %u "
                        "| pnmquant -quiet %u ",
                        size, size, colorCt);
        else
            pm_asprintf(&scaleCommand,
                        "pamscale -quiet -xysize %u %u ",
                        size, size);
        break;
    default:
        pm_error("Unrecognized Netpbm format: %d", format);
    }

    pm_system2(pm_feed_from_file, (void*)inputFileNm,
               pm_accept_to_file, (void*)outputFileNm,
               scaleCommand,
               &termStatus);

    if (termStatus != 0)
        pm_message("Shell command '%s' failed.  Termination status=%d",
                   scaleCommand, termStatus);

    pm_strfree(scaleCommand);
}



static int
formatTypeMax(int const typeA,
              int const typeB) {

    if (typeA == PPM_TYPE || typeB == PPM_TYPE)
        return PPM_TYPE;
    else if (typeA == PGM_TYPE || typeB == PGM_TYPE)
        return PGM_TYPE;
    else
        return PBM_TYPE;
}



static const char *
thumbnailFileName(const char * const dirName,
                  unsigned int const row,
                  unsigned int const col) {

    const char * fileName;

    pm_asprintf(&fileName, "%s/pi.%u.%u", dirName, row, col);

    return fileName;
}



static const char *
thumbnailFileList(const char * const dirName,
                  unsigned int const row,
                  unsigned int const cols) {

    unsigned int const maxListSize = 4096;

    char * list;
    unsigned int col;

    list = malloc(maxListSize);
    if (list == NULL)
        pm_error("Unable to allocate %u bytes for file list", maxListSize);

    list[0] = '\0';

    for (col = 0; col < cols; ++col) {
        const char * const fileName = thumbnailFileName(dirName, row, col);

        if (strlen(list) + strlen(fileName) + 1 > maxListSize - 1)
            pm_error("File name list too long for this program to handle.");
        else {
            strcat(list, " ");
            strcat(list, fileName);
        }
        pm_strfree(fileName);
    }

    return list;
}



static void
makeImageFile(const char * const thumbnailFileNm,
              const char * const inputFileNm,
              bool         const blackBackground,
              const char * const outputFileNm) {
/*----------------------------------------------------------------------------
   Create one thumbnail image.  It consists of the image in the file named
   'thumbnailFileName' with text of that name appended to the bottom.

   Write the result to the file named 'outputFileName'.

   'blackBackground' means give the image a black background where padding
   is necessary and make the text white on black.  If false, give the image
   a white background instead.
-----------------------------------------------------------------------------*/
    const char * const blackWhiteOpt = blackBackground ? "-black" : "-white";
    const char * const invertStage   = blackBackground ? "| pnminvert " : "";
    const char * const thumbnailFileNmToken = shellQuote(thumbnailFileNm);

    struct bufferDesc fileNmBuf;
    const char * shellCommand;
    int termStatus;

    fileNmBuf.size              = strlen(inputFileNm);
    fileNmBuf.buffer            = (unsigned char *)inputFileNm;
    fileNmBuf.bytesTransferredP = NULL;

    pm_asprintf(&shellCommand,
                "pbmtext "
                "%s"
                "| pamcat %s -topbottom %s - ",
                invertStage, blackWhiteOpt, thumbnailFileNmToken);

    pm_system2(&pm_feed_from_memory, &fileNmBuf,
               &pm_accept_to_file, (void*)outputFileNm,
               shellCommand,
               &termStatus);

    if (termStatus != 0)
        pm_error("Shell command '%s' to add file name to thumbnail image "
                 "of file '%s' failed, termination Status = %d",
                 shellCommand, inputFileNm, termStatus);

    pm_strfree(thumbnailFileNmToken);
    pm_strfree(shellCommand);
}



static void
makeThumbnail(const char *  const inputFileName,
              unsigned int  const size,
              bool          const black,
              bool          const quant,
              unsigned int  const colorCt,
              const char *  const tempDir,
              unsigned int  const row,
              unsigned int  const col,
              int *         const formatP) {

    const char * const fileName = thumbnailFileName(tempDir, row, col);

    FILE * ifP;
    int imageCols, imageRows, format;
    xelval maxval;
    const char * tmpfile;

    ifP = pm_openr(inputFileName);
    pnm_readpnminit(ifP, &imageCols, &imageRows, &maxval, &format);
    pm_close(ifP);

    pm_asprintf(&tmpfile, "%s/pi.tmp", tempDir);

    if (imageCols < size && imageRows < size)
        copyImage(inputFileName, tmpfile);
    else
        copyScaleQuantImage(inputFileName, tmpfile, format,
                            size, quant, colorCt);

    makeImageFile(tmpfile, inputFileName, black, fileName);

    unlink(tmpfile);

    pm_strfree(fileName);
    pm_strfree(tmpfile);

    *formatP = format;
}



static void
unlinkThumbnailFiles(const char * const dirName,
                     unsigned int const row,
                     unsigned int const cols) {

    unsigned int col;

    for (col = 0; col < cols; ++col) {
        const char * const fileName = thumbnailFileName(dirName, row, col);

        unlink(fileName);

        pm_strfree(fileName);
    }
}



static void
unlinkRowFiles(const char * const dirName,
               unsigned int const rows) {

    unsigned int row;

    for (row = 0; row < rows; ++row) {
        const char * const fileName = rowFileName(dirName, row);

        unlink(fileName);

        pm_strfree(fileName);
    }
}



static void
combineIntoRowAndDelete(unsigned int const row,
                        unsigned int const colCt,
                        int          const maxFormatType,
                        bool         const blackBackground,
                        bool         const quant,
                        unsigned int const colorCt,
                        const char * const tempDir) {
/*----------------------------------------------------------------------------
   Combine the 'colCt' thumbnails for row 'row' into a PNM file in directory
   'tempDir'.

   Each thumbnail is from a specially named file in 'tempDir' whose name
   indicates its position in the row, and the output is also specially named
   with a name indicating it is row 'row'.

   Where the thumnails are different heights, pad with black if
   'blackBackground' is true; white otherwise.

   If 'quant', color-quantize the result to have no more than 'colorCt'
   colors, choosing the colors that best represent all the pixels in the
   result.

   'maxFormatType' is the most expressive format of all the thumbnail files;
   results are undefined if it is not.
-----------------------------------------------------------------------------*/
    const char * const blackWhiteOpt = blackBackground ? "-black" : "-white";
    const char * const fileNm        = rowFileName(tempDir, row);

    const char * quantStage;
    const char * fileList;
    const char * shellCommand;
    int termStatus;

    unlink(fileNm);

    if (maxFormatType == PPM_TYPE && quant)
        pm_asprintf(&quantStage, "| pnmquant -quiet %u ", colorCt);
    else
        quantStage = strdup("");

    fileList = thumbnailFileList(tempDir, row, colCt);

    pm_asprintf(&shellCommand, "pamcat %s -leftright -jbottom %s "
                "%s",
                blackWhiteOpt, fileList, quantStage);

    pm_system2(&pm_feed_null, NULL,
               &pm_accept_to_file, (void*)fileNm,
               shellCommand,
               &termStatus);

    if (termStatus != 0)
        pm_error("Shell command '%s' to create row of thumbnails failed.  "
                 "Termination status = %d", shellCommand, termStatus);

    pm_strfree(fileList);
    pm_strfree(quantStage);
    pm_strfree(fileNm);
    pm_strfree(shellCommand);

    unlinkThumbnailFiles(tempDir, row, colCt);
}



static const char *
rowFileList(const char * const dirName,
            unsigned int const rows) {

    unsigned int const maxListSize = 4096;

    unsigned int row;
    char * list;

    list = malloc(maxListSize);
    if (list == NULL)
        pm_error("Unable to allocate %u bytes for file list", maxListSize);

    list[0] = '\0';

    for (row = 0; row < rows; ++row) {
        const char * const fileName = rowFileName(dirName, row);

        if (strlen(list) + strlen(fileName) + 1 > maxListSize - 1)
            pm_error("File name list too long for this program to handle.");

        else {
            strcat(list, " ");
            strcat(list, fileName);
        }
        pm_strfree(fileName);
    }

    return list;
}



static void
writeRowsAndDelete(unsigned int const rowCt,
                   int          const maxFormatType,
                   bool         const blackBackground,
                   bool         const quant,
                   unsigned int const colorCt,
                   const char * const tempDir) {
/*----------------------------------------------------------------------------
   Write the PNM image containing the 'rowCt' rows of thumbnails to Standard
   Output.  Take each row of thumbnails from a specially named PNM file in
   directory 'tempDir', and unlink it from that directory.

   Where the rows are different widths, pad with black if 'blackBackground'
   is true; white otherwise.

   If 'quant', color-quantize the result to have no more than 'colorCt'
   colors, choosing the colors that best represent all the pixels in the
   result.

   'maxFormatType' is the most expressive format of all the row files; results
   are undefined if it is not.
-----------------------------------------------------------------------------*/
    const char * const blackWhiteOpt = blackBackground ? "-black" : "-white";
    const char * const plainOpt      = pm_plain_output ? "-plain" : "";

    const char * quantStage;
    const char * fileList;
    const char * shellCommand;
    int termStatus;

    if (maxFormatType == PPM_TYPE && quant)
        pm_asprintf(&quantStage, "| pnmquant -quiet %u ", colorCt);
    else
        quantStage = strdup("");

    fileList = rowFileList(tempDir, rowCt);

    pm_asprintf(&shellCommand, "pamcat %s %s -topbottom %s %s",
                plainOpt, blackWhiteOpt, fileList, quantStage);

    /* Do pamcat/pnmquant command with no Standard Input and writing to
       our Standard Output
    */
    pm_system2(&pm_feed_null, NULL,
               NULL, NULL,
               shellCommand,
               &termStatus);

    if (termStatus != 0)
        pm_error("Shell command '%s' to assemble %u rows of thumbnails and "
                 "write them out failed; termination status = %d",
                 shellCommand, rowCt, termStatus);

    pm_strfree(shellCommand);
    pm_strfree(fileList);
    pm_strfree(quantStage);

    unlinkRowFiles(tempDir, rowCt);
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    const char * tempDir;
    int maxFormatType;
    unsigned int colsInRow;
    unsigned int rowsDone;
    unsigned int i;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    verbose = cmdline.verbose;

    makeTempDir(&tempDir);

    rowsDone = 0;  /* initial value */

    if (cmdline.title)
        makeTitle(cmdline.title, rowsDone++, cmdline.black, tempDir);

    for (i = 0, colsInRow = 0, maxFormatType = PBM_TYPE;
         i < cmdline.inputFileCount;
         ++i) {

        const char * const inputFileName = cmdline.inputFileName[i];

        int format;

        makeThumbnail(inputFileName, cmdline.size, cmdline.black,
                      !cmdline.noquant, cmdline.colors, tempDir,
                      rowsDone, colsInRow, &format);

        maxFormatType = formatTypeMax(maxFormatType, PNM_FORMAT_TYPE(format));

        ++colsInRow;
        if (colsInRow >= cmdline.across || i == cmdline.inputFileCount-1) {
            combineIntoRowAndDelete(
                rowsDone, colsInRow, maxFormatType,
                cmdline.black, !cmdline.noquant, cmdline.colors,
                tempDir);
            ++rowsDone;
            colsInRow = 0;
        }
    }

    writeRowsAndDelete(rowsDone, maxFormatType, cmdline.black,
                       !cmdline.noquant, cmdline.colors, tempDir);

    removeTempDir(tempDir);

    freeCmdline(cmdline);

    pm_close(stdout);

    return 0;
}


