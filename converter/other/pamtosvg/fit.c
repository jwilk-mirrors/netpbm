/* fit.c: turn a bitmap representation of a curve into a list of splines.
    Some of the ideas, but not the code, comes from the Phoenix thesis.
   See README for the reference.

   The code was partially derived from limn.

   Copyright (C) 1992 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <math.h>
#include <limits.h>
#include <float.h>
#include <string.h>
#include <assert.h>

#include "pm_c_util.h"
#include "mallocvar.h"

#include "autotrace.h"
#include "fit.h"
#include "message.h"
#include "logreport.h"
#include "spline.h"
#include "point.h"
#include "vector.h"
#include "curve.h"
#include "pxl-outline.h"
#include "epsilon.h"

#define CUBE(x) ((x) * (x) * (x))

typedef enum {LINEEND_INIT, LINEEND_TERM} LineEnd;

static LineEnd
otherEnd(LineEnd const thisEnd) {

    switch (thisEnd) {
    case LINEEND_INIT: return LINEEND_TERM;
    case LINEEND_TERM: return LINEEND_INIT;
    }
    assert(false);  /* All cases handled above */
    return LINEEND_INIT;  /* silence bogus compiler warning */
}



/* We need to manipulate lists of array indices.  */

typedef struct IndexList {
    unsigned int * data;
    unsigned int   length;
} IndexList;

/* The usual accessor macros.  */
#define GET_INDEX(i_l, n)  ((i_l).data[n])
#define INDEX_LIST_LENGTH(iL)  ((iL).length)
#define GET_LAST_INDEX(iL)  ((iL).data[INDEX_LIST_LENGTH(iL) - 1])




static pm_pixelcoord
intCoordFmReal(Point const realCoord) {
/*----------------------------------------------------------------------------
  Turn an real point into a integer one.
-----------------------------------------------------------------------------*/

    pm_pixelcoord intCoord;

    intCoord.col = ROUND(realCoord.x);
    intCoord.row = ROUND(realCoord.y);

    return intCoord;
}


/* Lists of array indices (well, that is what we use it for).  */

static IndexList
indexList_new(void) {

    IndexList indexList;

    indexList.data = NULL;
    INDEX_LIST_LENGTH(indexList) = 0;

  return indexList;
}



static void
indexList_free(IndexList * const indexListP) {

    if (INDEX_LIST_LENGTH(*indexListP) > 0) {
        free(indexListP->data);
        indexListP->data = NULL;
        INDEX_LIST_LENGTH(*indexListP) = 0;
    }
}



static void
indexList_append(IndexList *  const listP,
                 unsigned int const  newIndex) {

    INDEX_LIST_LENGTH(*listP)++;
    REALLOCARRAY_NOFAIL(listP->data, INDEX_LIST_LENGTH(*listP));
    listP->data[INDEX_LIST_LENGTH(*listP) - 1] = newIndex;
}



static float
distance(Point const p1,
         Point const p2) {
/*----------------------------------------------------------------------------
  Return the Euclidean distance between 'p1' and 'p2'.
-----------------------------------------------------------------------------*/
    float const x = p1.x - p2.x, y = p1.y - p2.y, z = p1.z - p2.z;

    return (float) sqrt(SQR(x) + SQR(y) + SQR(z));
}



static void
appendCorner(IndexList       *  const cornerListP,
             unsigned int       const pixelSeq,
             pixel_outline_type const outline,
             float              const angle,
             char               const logType) {

    pm_pixelcoord const coord = O_COORDINATE(outline, pixelSeq);

    indexList_append(cornerListP, pixelSeq);
    LOG4(" (%d,%d)%c%.3f", coord.col, coord.row, logType, angle);
}



static void
findVectors(unsigned int       const testIndex,
            pixel_outline_type const outline,
            Vector *      const inP,
            Vector *      const outP,
            unsigned int       const cornerSurround) {
/*----------------------------------------------------------------------------
  Return the difference vectors coming in and going out of the outline
  OUTLINE at the point whose index is TEST_INDEX.  In Phoenix,
  Schneider looks at a single point on either side of the point we're
  considering.  That works for him because his points are not touching.
  But our points *are* touching, and so we have to look at
  'cornerSurround' points on either side, to get a better picture of
  the outline's shape.
-----------------------------------------------------------------------------*/
    pm_pixelcoord const candidate = O_COORDINATE(outline, testIndex);

    unsigned int i;
    unsigned int doneCt;

    inP->dx  = inP->dy  = inP->dz  = 0.0;
    outP->dx = outP->dy = outP->dz = 0.0;

    /* Add up the differences from p of the `corner_surround' points before p.
    */
    for (i = O_PREV(outline, testIndex), doneCt = 0;
         doneCt < cornerSurround;
         i = O_PREV(outline, i), ++doneCt)
        *inP = vector_sum(*inP, vector_IPointDiff(O_COORDINATE(outline, i),
                                                  candidate));

    /* And the points after p. */
    for (i = O_NEXT(outline, testIndex), doneCt = 0;
         doneCt < cornerSurround;
         i = O_NEXT(outline, i), ++doneCt)
        *outP = vector_sum(*outP, vector_IPointDiff(O_COORDINATE(outline, i),
                                                    candidate));
}



static void
lookAheadForBetterCorner(pixel_outline_type  const outline,
                         unsigned int        const basePixelSeq,
                         float               const baseCornerAngle,
                         unsigned int        const cornerSurround,
                         unsigned int        const cornerAlwaysThreshold,
                         unsigned int *      const highestExaminedP,
                         float *             const bestCornerAngleP,
                         unsigned int *      const bestCornerIndexP,
                         IndexList *         const equallyGoodListP,
                         IndexList *         const cornerListP,
                         at_exception_type * const exceptionP) {
/*----------------------------------------------------------------------------
   'basePixelSeq' is the sequence position within 'outline' of a pixel
   that has a sufficiently small angle (to wit 'baseCornerAngle') to
   be a corner.  We look ahead in 'outline' for an even better one.
   We'll look up to 'cornerSurround' pixels ahead.

   We return the pixel sequence of the best corner we find (which could
   be the base) as *bestCornerIndexP.  Its angle is *bestCornerAngleP.

   We return as *highestExaminedP the pixel sequence of the last pixel
   we examined in our search (Caller can use this information to avoid
   examining them again).

   And we have this really dirty side effect: If we encounter any
   corner whose angle is less than 'cornerAlwaysThreshold', we add
   that to the list *cornerListP along the way.
-----------------------------------------------------------------------------*/
    float bestCornerAngle;
    unsigned bestCornerIndex;
    IndexList equallyGoodList;
    unsigned int q;
    unsigned int i;

    bestCornerIndex = basePixelSeq;     /* initial assumption */
    bestCornerAngle = baseCornerAngle;    /* initial assumption */

    equallyGoodList = indexList_new();

    q = basePixelSeq;
    i = basePixelSeq + 1;  /* Start with the next pixel */

    while (i < bestCornerIndex + cornerSurround &&
           i < O_LENGTH(outline) &&
           !at_exception_got_fatal(exceptionP)) {

        Vector inVector, outVector;
        float cornerAngle;

        /* Check the angle.  */

        q = i % O_LENGTH(outline);
        findVectors(q, outline, &inVector, &outVector, cornerSurround);
        cornerAngle = vector_angle(inVector, outVector, exceptionP);
        if (!at_exception_got_fatal(exceptionP)) {
            /* Perhaps the angle is sufficiently small that we want to
               consider this a corner, even if it's not the best
               (unless we've already wrapped around in the search, in
               which case we have already added the corner, and we
               don't want to add it again).
            */
            if (cornerAngle <= cornerAlwaysThreshold && q >= basePixelSeq)
                appendCorner(cornerListP, q, outline, cornerAngle, '\\');

            if (epsilon_equal(cornerAngle, bestCornerAngle))
                indexList_append(&equallyGoodList, q);
            else if (cornerAngle < bestCornerAngle) {
                bestCornerAngle = cornerAngle;
                /* We want to check `cornerSurround' pixels beyond the
                   new best corner.
                */
                i = bestCornerIndex = q;
                indexList_free(&equallyGoodList);
                equallyGoodList = indexList_new();
            }
            ++i;
        }
    }
    *bestCornerAngleP = bestCornerAngle;
    *bestCornerIndexP = bestCornerIndex;
    *equallyGoodListP = equallyGoodList;
    *highestExaminedP = q;
}



static void
establishCornerSearchLimits(pixel_outline_type  const outline,
                            fitting_opts_type * const fittingOptsP,
                            unsigned int *      const firstP,
                            unsigned int *      const lastP) {
/*----------------------------------------------------------------------------
   Determine where in the outline 'outline' we should look for corners.
-----------------------------------------------------------------------------*/
    assert(O_LENGTH(outline) >= 1);
    assert(O_LENGTH(outline) - 1 >= fittingOptsP->corner_surround);

    *firstP = 0;
    *lastP  = O_LENGTH(outline) - 1;
    if (outline.open) {
        *firstP += fittingOptsP->corner_surround;
        *lastP  -= fittingOptsP->corner_surround;
    }
}



static void
removeAdjacentCorners(IndexList *         const listP,
                      unsigned int        const lastIndex,
                      bool                const mustRemoveAdjCorners,
                      at_exception_type * const exception) {
/*----------------------------------------------------------------------------
   Remove adjacent points from the index list LIST.  We do this by first
   sorting the list and then running through it.  Since these lists are
   quite short, a straight selection sort (e.g., p.139 of the Art of
   Computer Programming, vol.3) is good enough.  LAST_INDEX is the index
   of the last pixel on the outline, i.e., the next one is the first
   pixel. We need this for checking the adjacency of the last corner.

   We need to do this because the adjacent corners turn into
   two-pixel-long curves, which can be fit only by straight lines.
-----------------------------------------------------------------------------*/
    unsigned int j;
    unsigned int last;
    IndexList newList;

    newList = indexList_new();  /* initial value */

    for (j = INDEX_LIST_LENGTH (*listP) - 1; j > 0; --j) {
        unsigned int search;
        unsigned int maxIndex;
            /* We find maximal element below `j' */

        for (search = 0, maxIndex = j; search < j; ++search)
            if (GET_INDEX (*listP, search) > GET_INDEX (*listP, maxIndex))
                maxIndex = search;

        if (maxIndex != j) {
            unsigned int const temp = GET_INDEX (*listP, j);
            GET_INDEX (*listP, j) = GET_INDEX (*listP, maxIndex);
            GET_INDEX (*listP, maxIndex) = temp;
        }
    }

    /* The list is sorted.  Now look for adjacent entries.  Each time through
       the loop we insert the current entry and, if appropriate, the next
       entry.
    */
    for (j = 0; j < INDEX_LIST_LENGTH(*listP) - 1; ++j) {
        unsigned int const current = GET_INDEX(*listP, j);
        unsigned int const next    = GET_INDEX(*listP, j + 1);

        /* We should never have inserted the same element twice.  */
        /* assert (current != next); */

        if ((mustRemoveAdjCorners) && ((next == current + 1) ||
                                       (next == current)))
            ++j;

        indexList_append(&newList, current);
    }

    /* Don't append the last element if it is 1) adjacent to the previous one;
       or 2) adjacent to the very first one.
    */
    last = GET_LAST_INDEX(*listP);
    if (INDEX_LIST_LENGTH(newList) == 0
        || !(last == GET_LAST_INDEX(newList) + 1
             || (last == lastIndex && GET_INDEX(*listP, 0) == 0)))
        indexList_append(&newList, last);

    indexList_free(listP);
    *listP = newList;
}



/* A ``knee'' is a point which forms a ``right angle'' with its
   predecessor and successor.  See the documentation (the `Removing
   knees' section) for an example and more details.

   The argument CLOCKWISE tells us which direction we're moving.  (We
   can't figure that information out from just the single segment with
   which we are given to work.)

   We should never find two consecutive knees.

   Since the first and last points are corners (unless the curve is
   cyclic), it doesn't make sense to remove those.
*/

/* This evaluates to true if the vector V is zero in one direction and
   nonzero in the other.  */
#define ONLY_ONE_ZERO(v)                                                \
  (((v).dx == 0.0 && (v).dy != 0.0) || ((v).dy == 0.0 && (v).dx != 0.0))

/* There are four possible cases for knees, one for each of the four
   corners of a rectangle; and then the cases differ depending on which
   direction we are going around the curve.  The tests are listed here
   in the order of upper left, upper right, lower right, lower left.
   Perhaps there is some simple pattern to the
   clockwise/counterclockwise differences, but I don't see one.  */
#define CLOCKWISE_KNEE(prev_delta, next_delta)                                                  \
  ((prev_delta.dx == -1.0 && next_delta.dy == 1.0)                                              \
   || (prev_delta.dy == 1.0 && next_delta.dx == 1.0)                                    \
   || (prev_delta.dx == 1.0 && next_delta.dy == -1.0)                                   \
   || (prev_delta.dy == -1.0 && next_delta.dx == -1.0))

#define COUNTERCLOCKWISE_KNEE(prev_delta, next_delta)                                   \
  ((prev_delta.dy == 1.0 && next_delta.dx == -1.0)                                              \
   || (prev_delta.dx == 1.0 && next_delta.dy == 1.0)                                    \
   || (prev_delta.dy == -1.0 && next_delta.dx == 1.0)                                   \
   || (prev_delta.dx == -1.0 && next_delta.dy == -1.0))



static void
remove_knee_points(curve * const curveP,
                   bool    const clockwise) {

    unsigned int const offset = CURVE_CYCLIC(curveP) ? 0 : 1;
    curve * const trimmedCurveP = copy_most_of_curve(curveP);

    pm_pixelcoord previous;
    unsigned int i;

    if (!CURVE_CYCLIC(curveP))
        append_pixel(trimmedCurveP, intCoordFmReal(CURVE_POINT(curveP, 0)));

    previous = intCoordFmReal(CURVE_POINT(curveP,
                                          CURVE_PREV(curveP, offset)));

    for (i = offset; i < CURVE_LENGTH(curveP) - offset; ++i) {
        pm_pixelcoord const current =
            intCoordFmReal(CURVE_POINT(curveP, i));
        pm_pixelcoord const next =
            intCoordFmReal(CURVE_POINT(curveP, CURVE_NEXT(curveP, i)));
        Vector const prev_delta = vector_IPointDiff(previous, current);
        Vector const next_delta = vector_IPointDiff(next, current);

        if (ONLY_ONE_ZERO(prev_delta) && ONLY_ONE_ZERO(next_delta)
            && ((clockwise && CLOCKWISE_KNEE(prev_delta, next_delta))
                || (!clockwise
                    && COUNTERCLOCKWISE_KNEE(prev_delta, next_delta))))
            LOG2(" (%d,%d)", current.col, current.row);
        else {
            previous = current;
            append_pixel(trimmedCurveP, current);
        }
    }

    if (!CURVE_CYCLIC(curveP))
        append_pixel(trimmedCurveP,
                     intCoordFmReal(LAST_CURVE_POINT(curveP)));

    if (CURVE_LENGTH(trimmedCurveP) == CURVE_LENGTH(curveP))
        LOG(" (none)");

    LOG(".\n");

    move_curve(curveP, trimmedCurveP);
}



static void
filter(curve *             const curveP,
       fitting_opts_type * const fittingOptsP) {
/*----------------------------------------------------------------------------
  Smooth the curve by adding in neighboring points.  Do this
  fittingOptsP->filter_iterations times.  But don't change the corners.
-----------------------------------------------------------------------------*/
    unsigned int const offset = CURVE_CYCLIC(curveP) ? 0 : 1;

    unsigned int iteration, thisPoint;
    Point prevNewPoint;

    /* We must have at least three points -- the previous one, the current
       one, and the next one.  But if we don't have at least five, we will
       probably collapse the curve down onto a single point, which means
       we won't be able to fit it with a spline.
    */
    if (CURVE_LENGTH(curveP) < 5) {
        LOG1("Length is %u, not enough to filter.\n", CURVE_LENGTH(curveP));
        return;
    }

    prevNewPoint.x = FLT_MAX;
    prevNewPoint.y = FLT_MAX;
    prevNewPoint.z = FLT_MAX;

    for (iteration = 0;
         iteration < fittingOptsP->filter_iterations;
         ++iteration) {
        curve * const newcurveP = copy_most_of_curve(curveP);

        bool collapsed;

        collapsed = false;  /* initial value */

        /* Keep the first point on the curve.  */
        if (offset)
            append_point(newcurveP, CURVE_POINT(curveP, 0));

        for (thisPoint = offset;
             thisPoint < CURVE_LENGTH(curveP) - offset;
             ++thisPoint) {
            Vector in, out, sum;
            Point newPoint;

            /* Calculate the vectors in and out, computed by looking
               at n points on either side of this_point.  Experimental
               it was found that 2 is optimal.
            */

            signed int prev, prevprev; /* have to be signed */
            unsigned int next, nextnext;
            Point candidate = CURVE_POINT(curveP, thisPoint);

            prev = CURVE_PREV(curveP, thisPoint);
            prevprev = CURVE_PREV(curveP, prev);
            next = CURVE_NEXT(curveP, thisPoint);
            nextnext = CURVE_NEXT(curveP, next);

            /* Add up the differences from p of the `surround' points
               before p.
            */
            in.dx = in.dy = in.dz = 0.0;

            in = vector_sum(in,
                            vector_fromTwoPoints(CURVE_POINT(curveP, prev),
                                                 candidate));
            if (prevprev >= 0)
                in = vector_sum(
                    in,
                    vector_fromTwoPoints(CURVE_POINT(curveP, prevprev),
                                         candidate));

            /* And the points after p.  Don't use more points after p than we
               ended up with before it.
            */
            out.dx = out.dy = out.dz = 0.0;

            out = vector_sum(
                out,
                vector_fromTwoPoints(CURVE_POINT(curveP, next), candidate));
            if (nextnext < CURVE_LENGTH(curveP))
                out = vector_sum(
                    out,
                    vector_fromTwoPoints(CURVE_POINT(curveP, nextnext),
                                         candidate));

            /* Start with the old point.  */
            newPoint = candidate;
            sum = vector_sum(in, out);
            /* We added 2*n+2 points, so we have to divide the sum by 2*n+2 */
            newPoint.x += sum.dx / 6;
            newPoint.y += sum.dy / 6;
            newPoint.z += sum.dz / 6;
            if (fabs(prevNewPoint.x - newPoint.x) < 0.3
                && fabs (prevNewPoint.y - newPoint.y) < 0.3
                && fabs (prevNewPoint.z - newPoint.z) < 0.3) {
                collapsed = true;
                break;
            }

            /* Put the newly computed point into a separate curve, so it
               doesn't affect future computation (on this iteration).
            */
            append_point(newcurveP, prevNewPoint = newPoint);
        }

        if (collapsed)
            free_curve(newcurveP);
        else {
            /* Just as with the first point, we have to keep the last
               point.
            */
            if (offset)
                append_point(newcurveP, LAST_CURVE_POINT(curveP));

            /* Set the original curve to the newly filtered one, and go
               again.
            */
            move_curve(curveP, newcurveP);
        }
    }
    log_curve(curveP, false);
}



static void
removeAdjacent(IndexList *         const cornerListP,
               pixel_outline_type  const outline,
               fitting_opts_type * const fittingOptsP,
               at_exception_type * const exception) {

    /* We never want two corners next to each other, since the
       only way to fit such a ``curve'' would be with a straight
       line, which usually interrupts the continuity dreadfully.
    */

    if (INDEX_LIST_LENGTH(*cornerListP) > 0)
        removeAdjacentCorners(
            cornerListP,
            O_LENGTH(outline) - (outline.open ? 2 : 1),
            fittingOptsP->remove_adjacent_corners,
            exception);
}



static IndexList
findCorners(pixel_outline_type  const outline,
            fitting_opts_type * const fittingOptsP,
            at_exception_type * const exceptionP) {

    /* We consider a point to be a corner if (1) the angle defined by
       the `corner_surround' points coming into it and going out from
       it is less than `corner_threshold' degrees, and no point within
       `corner_surround' points has a smaller angle; or (2) the angle
       is less than `corner_always_threshold' degrees.
    */
    unsigned int p;
    unsigned int firstPixelSeq, lastPixelSeq;
    IndexList cornerList;

    cornerList = indexList_new();

    if (O_LENGTH(outline) <= fittingOptsP->corner_surround * 2 + 1)
        return cornerList;

    establishCornerSearchLimits(outline, fittingOptsP,
                                &firstPixelSeq, &lastPixelSeq);

    /* Consider each pixel on the outline in turn.  */
    for (p = firstPixelSeq; p <= lastPixelSeq;) {
        Vector inVector, outVector;
        float cornerAngle;

        /* Check if the angle is small enough.  */
        findVectors(p, outline, &inVector, &outVector,
                     fittingOptsP->corner_surround);
        cornerAngle = vector_angle(inVector, outVector, exceptionP);
        if (at_exception_got_fatal(exceptionP))
            goto cleanup;

        if (fabs(cornerAngle) <= fittingOptsP->corner_threshold) {
            /* We want to keep looking, instead of just appending the
               first pixel we find with a small enough angle, since there
               might be another corner within `corner_surround' pixels, with
               a smaller angle.  If that is the case, we want that one.

               If we come across a corner that is just as good as the
               best one, we should make it a corner, too.  This
               happens, for example, at the points on the `W' in some
               typefaces, where the "points" are flat.
            */
            float bestCornerAngle;
            unsigned bestCornerIndex;
            IndexList equallyGoodList;
            unsigned int q;

            if (cornerAngle <= fittingOptsP->corner_always_threshold)
                /* The angle is sufficiently small that we want to
                   consider this a corner, even if it's not the best.
                */
                appendCorner(&cornerList, p, outline, cornerAngle, '\\');

            lookAheadForBetterCorner(outline, p, cornerAngle,
                                     fittingOptsP->corner_surround,
                                     fittingOptsP->corner_always_threshold,
                                     &q,
                                     &bestCornerAngle, &bestCornerIndex,
                                     &equallyGoodList,
                                     &cornerList,
                                     exceptionP);

            if (at_exception_got_fatal(exceptionP))
                goto cleanup;

            /* `q' is the index of the last point lookAhead checked.
               He added the corner if `bestCornerAngle' is less than
               `corner_always_threshold'.  If we've wrapped around, we
               added the corner on the first pass.  Otherwise, we add
               the corner now.
            */
            if (bestCornerAngle > fittingOptsP->corner_always_threshold
                && bestCornerIndex >= p) {

                unsigned int j;

                appendCorner(&cornerList, bestCornerIndex,
                             outline, bestCornerAngle, '/');

                for (j = 0; j < INDEX_LIST_LENGTH (equallyGoodList); ++j)
                    appendCorner(&cornerList, GET_INDEX(equallyGoodList, j),
                                 outline, bestCornerAngle, '@');
            }
            indexList_free(&equallyGoodList);

            /* If we wrapped around in our search, we're done;
               otherwise, we move on to the pixel after the highest
               one we just checked.
            */
            p = (q < p) ? O_LENGTH(outline) : q + 1;
        } else
            ++p;
    }
    removeAdjacent(&cornerList, outline, fittingOptsP, exceptionP);

cleanup:
    return cornerList;
}



static void
makeOutlineOneCurve(pixel_outline_type const outline,
                    curve_list_type *  const curveListP) {
/*----------------------------------------------------------------------------
   Add to *curveListP a single curve that represents the outline 'outline'.

   That curve does not have beginning and ending slope information.
-----------------------------------------------------------------------------*/
    curve * curveP;
    unsigned int pixelSeq;

    curveP = new_curve();

    for (pixelSeq = 0; pixelSeq < O_LENGTH(outline); ++pixelSeq)
        append_pixel(curveP, O_COORDINATE(outline, pixelSeq));

    if (outline.open)
        CURVE_CYCLIC(curveP) = false;
    else
        CURVE_CYCLIC(curveP) = true;

    /* Make it a one-curve cycle */
    NEXT_CURVE(curveP)     = curveP;
    PREVIOUS_CURVE(curveP) = curveP;

    append_curve(curveListP, curveP);
}



static void
addCurveStartingAtCorner(pixel_outline_type const outline,
                         IndexList          const cornerList,
                         unsigned int       const cornerSeq,
                         curve_list_type *  const curveListP,
                         curve **           const curCurvePP) {
/*----------------------------------------------------------------------------
   Add to the list *curveListP a new curve that starts at the cornerSeq'th
   corner in outline 'outline' (whose corners are 'cornerList') and
   goes to the next corner (or the end of the outline if no next corner).

   Furthermore, add that curve to the curve chain whose end is pointed
   to by *curCurvePP (NULL means chain is empty).

   Don't include beginning and ending slope information for that curve.
-----------------------------------------------------------------------------*/
    unsigned int const cornerPixelSeq = GET_INDEX(cornerList, cornerSeq);

    unsigned int lastPixelSeq;
    curve * curveP;
    unsigned int pixelSeq;

    if (cornerSeq + 1 >= cornerList.length)
        /* No more corners, so we go through the end of the outline. */
        lastPixelSeq = O_LENGTH(outline) - 1;
    else
        /* Go through the next corner */
        lastPixelSeq = GET_INDEX(cornerList, cornerSeq + 1);

    curveP = new_curve();

    for (pixelSeq = cornerPixelSeq; pixelSeq <= lastPixelSeq; ++pixelSeq)
        append_pixel(curveP, O_COORDINATE(outline, pixelSeq));

    append_curve(curveListP, curveP);
    {
        /* Add the new curve to the outline chain */

        curve * const oldCurCurveP = *curCurvePP;

        if (oldCurCurveP) {
            NEXT_CURVE(oldCurCurveP) = curveP;
            PREVIOUS_CURVE(curveP)   = oldCurCurveP;
        }
        *curCurvePP = curveP;
    }
}



static void
divideOutlineWithCorners(pixel_outline_type const outline,
                         IndexList          const cornerList,
                         curve_list_type *  const curveListP) {
/*----------------------------------------------------------------------------
   Divide the outline 'outline' into curves at the corner points
   'cornerList' and add each curve to *curveListP.

   Each curve contains the corners at each end.

   The last curve is special.  It consists of the pixels (inclusive)
   between the last corner and the end of the outline, and the
   beginning of the outline and the first corner.

   We link the curves in a chain.  If the outline (and therefore the
   curve list) is closed, the chain is a cycle of all the curves.  If
   it is open, the chain is a linear chain of all the curves except
   the last one (the one that goes from the last corner to the first
   corner).

   Assume there is at least one corner.

   The curves do not have beginning and ending slope information.
-----------------------------------------------------------------------------*/
    unsigned int const firstCurveSeq = CURVE_LIST_LENGTH(*curveListP);
        /* Index in curve list of the first curve we add */
    unsigned int cornerSeq;
    curve * curCurveP;
        /* Pointer to the curve we most recently added for this outline.
           Null if none
        */

    assert(cornerList.length > 0);

    curCurveP = NULL;  /* No curves in outline chain yet */

    if (outline.open) {
        /* Start with a curve that contains the points up to the first
           corner
        */
        curve * curveP;
        unsigned int pixelSeq;

        curveP = new_curve();

        for (pixelSeq = 0; pixelSeq <= GET_INDEX(cornerList, 0); ++pixelSeq)
            append_pixel(curveP, O_COORDINATE(outline, pixelSeq));

        append_curve(curveListP, curveP);
        curCurveP = curveP;  /* Only curve in outline chain now */
    } else {
        /* We'll pick up the pixels before the first corner at the end */
    }
    /* Add to the list a curve that starts at each corner and goes
       through the following corner, or the end of the outline if
       there is no following corner.  Do it in order of the corners.
    */
    for (cornerSeq = 0; cornerSeq < cornerList.length; ++cornerSeq)
        addCurveStartingAtCorner(outline, cornerList, cornerSeq, curveListP,
                                 &curCurveP);

    if (!outline.open) {
        /* Come around to the start of the curve list -- add the pixels
           before the first corner to the last curve, and chain the last
           curve to the first one.
        */
        curve * const firstCurveP = CURVE_LIST_ELT(*curveListP, firstCurveSeq);

        unsigned int pixelSeq;

        for (pixelSeq = 0; pixelSeq <= GET_INDEX(cornerList, 0); ++pixelSeq)
            append_pixel(curCurveP, O_COORDINATE(outline, pixelSeq));

        NEXT_CURVE(curCurveP)       = firstCurveP;
        PREVIOUS_CURVE(firstCurveP) = curCurveP;
    }
}



static curve_list_array_type
split_at_corners(pixel_outline_list_type const pixelList,
                 fitting_opts_type *     const fittingOptsP,
                 at_exception_type *     const exception) {
/*----------------------------------------------------------------------------
   Find the corners in 'pixelList', the list of points.  (Presumably we
   can't fit a single spline around a corner.)  The general strategy
   is to look through all the points, remembering which we want to
   consider corners.  Then go through that list, producing the
   curve_list.  This is dictated by the fact that 'pixelList' does not
   necessarily start on a corner---it just starts at the character's
   first outline pixel, going left-to-right, top-to-bottom.  But we
   want all our splines to start and end on real corners.

   For example, consider the top of a capital `C' (this is in cmss20):
                     x
                     ***********
                  ******************

   'pixelList' will start at the pixel below the `x'.  If we considered
   this pixel a corner, we would wind up matching a very small segment
   from there to the end of the line, probably as a straight line, which
   is certainly not what we want.

   'pixelList' has one element for each closed outline on the character.
   To preserve this information, we return an array of curve_lists, one
   element (which in turn consists of several curves, one between each
   pair of corners) for each element in 'pixelList'.

   The curves we return do not have beginning and ending slope
   information.
-----------------------------------------------------------------------------*/
    unsigned outlineSeq;
    curve_list_array_type curveArray;

    curveArray = new_curve_list_array();

    LOG("\nFinding corners:\n");

    for (outlineSeq = 0;
         outlineSeq < O_LIST_LENGTH(pixelList);
         ++outlineSeq) {

        pixel_outline_type const outline =
            O_LIST_OUTLINE(pixelList, outlineSeq);

        IndexList       cornerList;
        curve_list_type curveList;

        curveList = new_curve_list();

        CURVE_LIST_CLOCKWISE(curveList) = O_CLOCKWISE(outline);
        curveList.color = outline.color;
        curveList.open  = outline.open;

        LOG1("#%u:", outlineSeq);

        /* If the outline does not have enough points, we can't do
           anything.  The endpoints of the outlines are automatically
           corners.  We need at least `corner_surround' more pixels on
           either side of a point before it is conceivable that we might
           want another corner.
        */
        if (O_LENGTH(outline) > fittingOptsP->corner_surround * 2 + 2)
            cornerList = findCorners(outline, fittingOptsP, exception);

        else {
            int const surround = (O_LENGTH(outline) - 3) / 2;
            if (surround >= 2) {
                unsigned int const oldCornerSurround =
                    fittingOptsP->corner_surround;
                fittingOptsP->corner_surround = surround;
                cornerList = findCorners(outline, fittingOptsP, exception);
                fittingOptsP->corner_surround = oldCornerSurround;
            } else {
                cornerList.length = 0;
                cornerList.data = NULL;
            }
        }

        if (cornerList.length == 0)
            /* No corners.  Use all of the pixel outline as the one curve. */
            makeOutlineOneCurve(outline, &curveList);
        else
            divideOutlineWithCorners(outline, cornerList, &curveList);

        LOG1(" [%u].\n", cornerList.length);
        indexList_free(&cornerList);

        /* And now add the just-completed curve list to the array.  */
        append_curve_list(&curveArray, curveList);
    }

    return curveArray;
}



static void
removeKnees(curve_list_type const curveList) {
/*----------------------------------------------------------------------------
  Remove the extraneous ``knee'' points before filtering.  Since the
  corners have already been found, we don't need to worry about
  removing a point that should be a corner.
-----------------------------------------------------------------------------*/
    unsigned int curveSeq;

    LOG("\nRemoving knees:\n");

    for (curveSeq = 0; curveSeq < curveList.length; ++curveSeq) {
        LOG1("#%u:", curveSeq);
        remove_knee_points(CURVE_LIST_ELT(curveList, curveSeq),
                           CURVE_LIST_CLOCKWISE(curveList));
    }
}



static void
computePointWeights(curve_list_type     const curveList,
                    fitting_opts_type * const fittingOptsP,
                    distance_map_type * const distP) {

    unsigned int const height = distP->height;

    unsigned int curveSeq;

    for (curveSeq = 0; curveSeq < curveList.length; ++curveSeq) {
        unsigned pointSeq;
        curve_type const curve = CURVE_LIST_ELT(curveList, curveSeq);
        for (pointSeq = 0; pointSeq < CURVE_LENGTH(curve); ++pointSeq) {
            Point * const coordP = &CURVE_POINT(curve, pointSeq);
            unsigned int x = coordP->x;
            unsigned int y = height - (unsigned int)coordP->y - 1;

            float width, w;

            /* Each (x, y) is a point on the skeleton of the curve, which
               might be offset from the true centerline, where the width
               is maximal.  Therefore, use as the local line width the
               maximum distance over the neighborhood of (x, y).
            */
            width = distP->d[y][x];  /* initial value */
            if (y - 1 >= 0) {
                if ((w = distP->d[y-1][x]) > width)
                    width = w;
                if (x - 1 >= 0) {
                    if ((w = distP->d[y][x-1]) > width)
                        width = w;
                    if ((w = distP->d[y-1][x-1]) > width)
                        width = w;
                }
                if (x + 1 < distP->width) {
                    if ((w = distP->d[y][x+1]) > width)
                        width = w;
                    if ((w = distP->d[y-1][x+1]) > width)
                        width = w;
                }
            }
            if (y + 1 < height) {
                if ((w = distP->d[y+1][x]) > width)
                    width = w;
                if (x - 1 >= 0 && (w = distP->d[y+1][x-1]) > width)
                    width = w;
                if (x + 1 < distP->width && (w = distP->d[y+1][x+1]) > width)
                    width = w;
            }
            coordP->z = width * (fittingOptsP->width_weight_factor);
        }
    }
}



static void
filterCurves(curve_list_type     const curveList,
             fitting_opts_type * const fittingOptsP) {

    unsigned int curveSeq;

    LOG("\nFiltering curves:\n");

    for (curveSeq = 0; curveSeq < curveList.length; ++curveSeq) {
        LOG1("#%u: ", curveSeq);
        filter(CURVE_LIST_ELT(curveList, curveSeq), fittingOptsP);
    }
}



static void
logSplinesForCurve(unsigned int     const curveSeq,
                   spline_list_type const curveSplines) {

    unsigned int splineSeq;

    LOG1("Fitted splines for curve #%u:\n", curveSeq);
    for (splineSeq = 0;
         splineSeq < SPLINE_LIST_LENGTH(curveSplines);
         ++splineSeq) {
        LOG1("  %u: ", splineSeq);
        if (log_file)
            print_spline(log_file, SPLINE_LIST_ELT(curveSplines, splineSeq));
    }
}



static void
changeBadLines(spline_list_type *        const splineListP,
               const fitting_opts_type * const fittingOptsP) {

  /* Unfortunately, we cannot tell in isolation whether a given spline
     should be changed to a line or not.  That can only be known after the
     entire curve has been fit to a list of splines.  (The curve is the
     pixel outline between two corners.)  After subdividing the curve, a
     line may very well fit a portion of the curve just as well as the
     spline---but unless a spline is truly close to being a line, it
     should not be combined with other lines.
  */

    unsigned int const length = SPLINE_LIST_LENGTH(*splineListP);

    unsigned int thisSpline;
    bool foundCubic;

    LOG1("\nChecking for bad lines (length %u):\n", length);

    /* First see if there are any splines in the fitted shape.  */
    for (thisSpline = 0, foundCubic = false;
         thisSpline < length;
         ++thisSpline) {
        if (SPLINE_DEGREE(SPLINE_LIST_ELT(*splineListP, thisSpline)) ==
            CUBICTYPE) {
            foundCubic = true;
            break;
        }
    }

    /* If so, change lines back to splines (we haven't done anything to
       their control points, so we only have to change the degree) unless
       the spline is close enough to being a line.
    */
    if (foundCubic) {
        unsigned int thisSpline;

        for (thisSpline = 0; thisSpline < length; ++thisSpline) {
            spline_type const s = SPLINE_LIST_ELT(*splineListP, thisSpline);

            if (SPLINE_DEGREE(s) == LINEARTYPE) {
                LOG1("  #%u: ", thisSpline);
                if (SPLINE_LINEARITY(s) >
                    fittingOptsP->line_reversion_threshold) {
                    LOG("reverted, ");
                    SPLINE_DEGREE(SPLINE_LIST_ELT(*splineListP, thisSpline))
                        = CUBICTYPE;
                }
                LOG1("linearity %.3f.\n", SPLINE_LINEARITY(s));
            }
        }
    } else
        LOG("  No lines.\n");
}



static bool
splineLinearEnough(spline_type *             const splineP,
                   curve_type                const curve,
                   const fitting_opts_type * const fittingOptsP) {

    /* Supposing that we have accepted the error, another question arises:
       would we be better off just using a straight line?
    */

    float A, B, C;
    unsigned int thisPoint;
    float dist;
    float startEndDist;
    float threshold;

    LOG ("Checking linearity:\n");

    A = END_POINT(*splineP).x - BEG_POINT(*splineP).x;
    B = END_POINT(*splineP).y - BEG_POINT(*splineP).y;
    C = END_POINT(*splineP).z - BEG_POINT(*splineP).z;

    startEndDist = (float) (SQR(A) + SQR(B) + SQR(C));
    LOG1("start_end_distance is %.3f.\n", sqrt(startEndDist));

    LOG3("  Line endpoints are (%.3f, %.3f, %.3f) and ",
         BEG_POINT(*splineP).x,
         BEG_POINT(*splineP).y,
         BEG_POINT(*splineP).z);
    LOG3("(%.3f, %.3f, %.3f)\n",
         END_POINT(*splineP).x, END_POINT(*splineP).y, END_POINT(*splineP).z);

    /* LOG3("  Line is %.3fx + %.3fy + %.3f = 0.\n", A, B, C); */

    for (thisPoint = 0, dist = 0.0;
         thisPoint < CURVE_LENGTH(curve);
         ++thisPoint) {

        float       const t           = CURVE_T(curve, thisPoint);
        Point const splinePoint = evaluate_spline(*splineP, t);

        float const a = splinePoint.x - BEG_POINT(*splineP).x;
        float const b = splinePoint.y - BEG_POINT(*splineP).y;
        float const c = splinePoint.z - BEG_POINT(*splineP).z;

        float const w = (A*a + B*b + C*c) / startEndDist;

        dist += (float)sqrt(SQR(a-A*w) + SQR(b-B*w) + SQR(c-C*w));
    }
    LOG1("  Total distance is %.3f, ", dist);

    dist /= (CURVE_LENGTH (curve) - 1);

    LOG1 ("which is %.3f normalized.\n", dist);

    /* We want reversion of short curves to splines to be more likely than
       reversion of long curves, hence the second division by the curve
       length, for use in `change_bad_lines'.
    */
    SPLINE_LINEARITY(*splineP) = dist;
    LOG1("  Final linearity: %.3f.\n", SPLINE_LINEARITY (*splineP));

    if (startEndDist * (float) 0.5 > fittingOptsP->line_threshold)
        threshold = fittingOptsP->line_threshold;
    else
        threshold = startEndDist * (float) 0.5;
    LOG1("threshold is %.3f .\n", threshold);

    if (dist < threshold)
        return true;
    else
        return false;
}



/* Forward declaration for recursion */

static spline_list_type *
fitCurve(curve *                   const curveP,
         Vector               const begSlope,
         Vector               const endSlope,
         const fitting_opts_type * const fittingOptsP,
         at_exception_type *       const exceptionP);



static spline_list_type *
fitWithLine(curve * const curveP) {
/*----------------------------------------------------------------------------
  Return a list of splines that fit curve *curveP in a very simple way:
  a single spline which is a straight line through the first and last
  points on the curve.

  This simplicity is useful only on a very short curve.
-----------------------------------------------------------------------------*/
    spline_type line;

    LOG("Fitting with straight line:\n");

    SPLINE_DEGREE(line) = LINEARTYPE;
    BEG_POINT(line)     = CONTROL1(line) = CURVE_POINT(curveP, 0);
    END_POINT(line)     = CONTROL2(line) = LAST_CURVE_POINT(curveP);

    /* Make sure that this line is never changed to a cubic.  */
    SPLINE_LINEARITY(line) = 0;

    if (log_file) {
        LOG("  ");
        print_spline(log_file, line);
    }

    return new_spline_list_with_spline(line);
}



#define B0(t) CUBE ((float) 1.0 - (t))
#define B1(t) ((float) 3.0 * (t) * SQR ((float) 1.0 - (t)))
#define B2(t) ((float) 3.0 * SQR (t) * ((float) 1.0 - (t)))
#define B3(t) CUBE (t)

static spline_type
fitOneSpline(curve *             const curveP,
             Vector         const begSlope,
             Vector         const endSlope,
             at_exception_type * const exceptionP) {
/*----------------------------------------------------------------------------
  Return a spline that fits the points of curve *curveP,
  with slope 'begSlope' at its beginning and 'endSlope' at its end.

  Make it a cubic spline.
-----------------------------------------------------------------------------*/
    /* We already have the start and end points of the spline, so all we need
       are the control points.  And we know in what direction each control
       point is from its respective end point, so all we need to figure out is
       its distance.  (The control point's distance from the end point is an
       indication of how long the curve goes in its direction).

       We call the distance from an end point to the associated control point
       "alpha".

       We want to find starting and ending alpha that minimize the
       least-square error in approximating *curveP with the spline.

       How we do that is a complete mystery to me, but the original author
       said to see pp. 57-59 of the Phoenix thesis.  I haven't seen that.

       In our expression of the math here, we use a struct with "beg" and
       "end" members where the paper uses a matrix with "1" and "2"
       subscripts, respectively.  A C array is a closer match to a math
       matrix, but we think the struct is easier to read.

       The B?(t) here corresponds to B_i^3(U_i) there.
       The Bernstein polynomials of degree n are defined by
       B_i^n(t) = { n \choose i } t^i (1-t)^{n-i}, i = 0..n
    */
    struct VectorPair {
        Vector beg;
        Vector end;
    };
    struct VectorPair tang;

    spline_type spline;
    Vector begVector, endVector;
    unsigned int i;
    struct VectorPair * A;  /* malloc'ed array */
        /* I don't know the meaning of this array, but it is one entry for
           each point in the curve (A[i] is for the ith point in the curve).
        */
    struct {
        struct { float beg; float end; } beg;
        struct { float beg; float end; } end;
    } C;
    struct { float beg; float end; } X;

    tang.beg = begSlope; tang.end = endSlope;

    MALLOCARRAY_NOFAIL(A, CURVE_LENGTH(curveP));

    BEG_POINT(spline) = CURVE_POINT(curveP, 0);
    END_POINT(spline) = LAST_CURVE_POINT(curveP);
    begVector = vector_fromPoint(BEG_POINT(spline));
    endVector = vector_fromPoint(END_POINT(spline));

    for (i = 0; i < CURVE_LENGTH(curveP); ++i) {
        A[i].beg = vector_scaled(tang.beg, B1(CURVE_T(curveP, i)));
        A[i].end = vector_scaled(tang.end, B2(CURVE_T(curveP, i)));
    }

    C.beg.beg = 0.0; C.beg.end = 0.0; C.end.end = 0.0;  /* initial value */

    X.beg = 0.0; X.end = 0.0; /* initial value */

    for (i = 0; i < CURVE_LENGTH(curveP); ++i) {
        struct VectorPair * const AP = &A[i];
        Vector temp, temp0, temp1, temp2, temp3;

        C.beg.beg += vector_dotProduct(AP->beg, AP->beg);
        C.beg.end += vector_dotProduct(AP->beg, AP->end);
        /* C.end.beg = vector_dotProduct(AP->end, AP->beg)
           is done outside of loop */
        C.end.end += vector_dotProduct(AP->end, AP->end);

        /* Now the right-hand side of the equation in the paper.  */
        temp0 = vector_scaled(begVector, B0(CURVE_T(curveP, i)));
        temp1 = vector_scaled(begVector, B1(CURVE_T(curveP, i)));
        temp2 = vector_scaled(endVector, B2(CURVE_T(curveP, i)));
        temp3 = vector_scaled(endVector, B3(CURVE_T(curveP, i)));

        temp = vector_fromPoint(
            vector_diffPoint(
                CURVE_POINT(curveP, i),
                vector_sum(temp0, vector_sum(temp1,
                                             vector_sum(temp2, temp3)))));

        X.beg += vector_dotProduct(temp, AP->beg);
        X.end += vector_dotProduct(temp, AP->end);
    }
    free(A);

    C.end.beg = C.beg.end;

    {
        float const XCendDet  = X.beg * C.end.end - X.end * C.beg.end;
        float const CbegXDet  = C.beg.beg * X.end - C.beg.end * X.beg;
        float const CDet = C.beg.beg * C.end.end - C.end.beg * C.beg.end;

        if (CDet == 0.0) {
            LOG("zero determinant of C matrix");
            at_exception_fatal(exceptionP, "zero determinant of C matrix");
        } else {
            struct { float beg; float end; } alpha;  /* constant */
            alpha.beg = XCendDet / CDet;
            alpha.end = CbegXDet / CDet;

            CONTROL1(spline) = vector_sumPoint(
                BEG_POINT(spline), vector_scaled(tang.beg, alpha.beg));
            CONTROL2(spline) = vector_sumPoint(
                END_POINT(spline), vector_scaled(tang.end, alpha.end));
            SPLINE_DEGREE(spline) = CUBICTYPE;
        }
    }
    return spline;
}



static void
logSplineFit(spline_type const spline) {

    if (SPLINE_DEGREE(spline) == LINEARTYPE)
        LOG("  fitted to line:\n");
    else
        LOG("  fitted to spline:\n");

    if (log_file) {
        LOG ("    ");
        print_spline(log_file, spline);
    }
}



static Vector
findHalfTangent(LineEnd      const toWhichEnd,
                curve *      const curveP,
                unsigned int const tangentSurround) {
/*----------------------------------------------------------------------------
  Find the slope in the vicinity of one of the ends of the curve *curveP,
  as specified by 'toWhichEnd'.

  To wit, this is the mean slope between the end point and each of the
  'tangentSurround' adjacent points, up to half the curve.

  For example, if 'toWhichEnd' is LINEEND_INIT and 'tangentSurround' is 3 and
  the curve is 10 points long, we imagine a line through Point 0 and Point 1,
  another through Point 0 and Point 2, and a third through Point 0 and Point
  3.  We return the mean of the slopes of those 3 lines.

  Don't consider points that are identical to the end point (as there could
  be no slope between those points) -- they're part of the count, but don't
  contribute to the slope.  If _all_ of the points to be considered are
  identical to the end point, arbitrarily return a horizontal slope.

  Return the slope as an unnormalized vector.  (I don't know if that was
  intended by the designer, since it isn't sensible unless it's a
  computational efficiency thing; it's just how I found the code).

  It is possible for the mean described above to be the zero vector, because
  the mean of a vector pointing left and one pointing right is the zero
  vector.  In that case, we use fewer "tangentSurround" points.
-----------------------------------------------------------------------------*/
    Point  const tangentPoint =
        CURVE_POINT(curveP,
                    toWhichEnd == LINEEND_INIT ? 0 : CURVE_LENGTH(curveP) - 1);
    Vector  const zeroZero = { 0.0, 0.0 };

    unsigned int surroundCt;
    bool         gotNonzero;
    Vector  mean;

    for (surroundCt = MIN(CURVE_LENGTH(curveP) / 2, tangentSurround),
             gotNonzero = false;
         !gotNonzero;
         --surroundCt) {

        unsigned int i;
        Vector sum;
        unsigned int n;

        for (i = 0, n = 0, sum = zeroZero; i < surroundCt; ++i) {
            unsigned int const thisIndex =
                toWhichEnd == LINEEND_INIT ?
                    i + 1 :  CURVE_LENGTH(curveP) - 1 - i;
            Point const thisPoint = CURVE_POINT(curveP, thisIndex);

            if (!point_equal(thisPoint, tangentPoint)) {
                /* Perhaps we should weight the tangent from `thisPoint' by
                   some factor dependent on the distance from the tangent
                   point.
                */
                sum = vector_sum(sum, vector_pointDirection(thisPoint,
                                                            tangentPoint));
                ++n;
            }
        }
        mean = n > 0 ? vector_scaled(sum, 1.0 / n) : vector_horizontal();

        if (vector_equal(mean, vector_zero())) {
            /* We have points on multiple sides of the endpoint whose vectors
               happen to add up to zero, which is not usable.
            */
            assert(surroundCt > 0);
        } else
            gotNonzero = true;
    }

    return mean;
}



static void
findTangent(curve *       const curveP,
            LineEnd       const toWhichEnd,
            curve *       const adjacentCurveP,
            unsigned int  const tangentSurround,
            Vector *      const tangentP) {
/*----------------------------------------------------------------------------
  Find an approximation to the slope of *curveP (i.e. slope of tangent
  line) at an endpoint (per 'toWhichEnd').

  This approximation is the mean of the slopes between the end of the curve
  and the 'tangentSurround' points leading up to it (but not more than one
  point beyond the midpoint of the curve).  Note that the curve may loop.
  Since there is no slope between two identical points, we ignore points that
  are identical to the endpoint.  They count toward the limit; they just
  aren't included in the result.  If none of the points to be considered are
  distinct from the endpoint, we arbitrarily consider the curve to be
  horizontal there.

  If 'adjacentCurveP' is non-null, average this slope with the slope of the
  other end of curve *adjacentCurveP, which we assume is adjacent.  Adjacent
  means the previous curve in the outline chain for the slope at the start
  point ('toWhichEnd' == LINEEND_BEG), the next curve otherwise.  If *curveP
  is cyclic, then it is its own adjacent curve.

  It is important to compute an accurate approximation, because the
  control points that we eventually decide upon to fit the curve will
  be placed on the half-lines defined by the slopes and endpoints, and
  we never recompute the tangent after this.
-----------------------------------------------------------------------------*/
    Vector const slopeThisCurve =
        findHalfTangent(toWhichEnd, curveP, tangentSurround);

    LOG2("  tangent to %s of curve %lx: ",
         toWhichEnd == LINEEND_INIT ? "start" : "end", (unsigned long)curveP);

    LOG3("(this curve half tangent (%.3f,%.3f,%.3f)) ",
         slopeThisCurve.dx, slopeThisCurve.dy, slopeThisCurve.dz);

    if (adjacentCurveP) {
        Vector const slopeAdjCurve =
            findHalfTangent(otherEnd(toWhichEnd),
                            adjacentCurveP,
                            tangentSurround);

        LOG3("(adjacent curve half tangent (%.3f,%.3f,%.3f)) ",
             slopeAdjCurve.dx, slopeAdjCurve.dy, slopeAdjCurve.dz);
        *tangentP = vector_scaled(vector_sum(slopeThisCurve, slopeAdjCurve),
                                  0.5);
    } else
        *tangentP = slopeThisCurve;

    LOG3("(%.3f,%.3f,%.3f).\n", tangentP->dx, tangentP->dy, tangentP->dz);
}



static void
findError(curve *             const curveP,
          spline_type         const spline,
          float *             const errorP,
          unsigned int *      const worstPointP,
          at_exception_type * const exceptionP) {
/*----------------------------------------------------------------------------
  Tell how good a fit 'spline' is for *curveP.

  Return the error (maximum Euclidean distance between a point on
  *curveP and the corresponding point on 'spline') as *errorP and the
  sequence number of the point on the curve where the error is
  greatest as *worstPointP.

  If there are multiple equally bad points, return an arbitrary one of
  them as *worstPointP.
-----------------------------------------------------------------------------*/
    unsigned int thisPoint;
    float totalError;
    float worstError;
    unsigned int worstPoint;

    assert(CURVE_LENGTH(curveP) > 0);

    totalError = 0.0;  /* initial value */
    worstError = FLT_MIN; /* initial value */
    worstPoint = 0;

    for (thisPoint = 0; thisPoint < CURVE_LENGTH(curveP); ++thisPoint) {
        Point const curvePoint = CURVE_POINT(curveP, thisPoint);
        float const t = CURVE_T(curveP, thisPoint);
        Point const splinePoint = evaluate_spline(spline, t);
        float const thisError = distance(curvePoint, splinePoint);
        if (thisError >= worstError) {
            worstPoint = thisPoint;
            worstError = thisError;
        }
        totalError += thisError;
    }

    if (epsilon_equal(totalError, 0.0))
        LOG("  Every point fits perfectly.\n");
    else {
        LOG5("  Worst error (at (%.3f,%.3f,%.3f), point #%u) was %.3f.\n",
             CURVE_POINT(curveP, worstPoint).x,
             CURVE_POINT(curveP, worstPoint).y,
             CURVE_POINT(curveP, worstPoint).z,
             worstPoint, worstError);
        LOG1("  Total error was %.3f.\n", totalError);
        LOG2("  Average error (over %u points) was %.3f.\n",
                 CURVE_LENGTH(curveP), totalError / CURVE_LENGTH(curveP));
    }
    assert(worstPoint < CURVE_LENGTH(curveP));
    *errorP      = worstError;
    *worstPointP = worstPoint;
}



static void
setInitialParameterValues(curve * const curveP) {
/*----------------------------------------------------------------------------
   Fill in the 't' values in *curveP.

   The t value for point P on a curve is the distance P is along the
   curve from the initial point, normalized so the entire curve is
   length 1.0 (i.e. t of the initial point is 0.0; t of the final
   point is 1.0).

   There are a lot of curves that pass through the points indicated by
   *curveP, but for practical computation of t, we just take the
   piecewise linear locus that runs through all of them.  That means
   we can just step through *curveP, adding up the distance from one
   point to the next to get the t value for each point.

   This is the "chord-length parameterization" method, which is
   described in Plass & Stone.
-----------------------------------------------------------------------------*/
    unsigned int p;

    LOG("\nAssigning initial t values:\n  ");

    CURVE_T(curveP, 0) = 0.0;

    for (p = 1; p < CURVE_LENGTH(curveP); ++p) {
        Point const point      = CURVE_POINT(curveP, p);
        Point const previous_p = CURVE_POINT(curveP, p - 1);
        float const d = distance(point, previous_p);
        CURVE_T(curveP, p) = CURVE_T(curveP, p - 1) + d;
    }

    assert(LAST_CURVE_T(curveP) != 0.0);

    /* Normalize to a curve length of 1.0 */

    for (p = 1; p < CURVE_LENGTH(curveP); ++p)
        CURVE_T(curveP, p) = CURVE_T(curveP, p) / LAST_CURVE_T(curveP);

    log_entire_curve(curveP);
}



static void
subdivideCurve(curve *                   const curveP,
               unsigned int              const subdivisionIndex,
               const fitting_opts_type * const fittingOptsP,
               curve **                  const leftCurvePP,
               curve **                  const rghtCurvePP,
               Vector *             const joinSlopeP) {
/*----------------------------------------------------------------------------
  Split curve *curveP into two, at 'subdivisionIndex'.  (Actually,
  leave *curveP alone, but return as *leftCurvePP and *rghtCurvePP
  two new curves that are the pieces).

  Return as *joinSlopeP what should be the slope where the subcurves
  join, i.e. the slope of the end of the left subcurve and of the start
  of the right subcurve.

  To be precise, the point with sequence number 'subdivisionIndex'
  becomes the first pixel of the right-hand curve.
-----------------------------------------------------------------------------*/
    curve * leftCurveP;
    curve * rghtCurveP;

    leftCurveP = new_curve();
    rghtCurveP = new_curve();

    LOG4("  Subdividing curve %lx into %lx and %lx at point #%u\n",
         (unsigned long)curveP,
         (unsigned long)leftCurveP, (unsigned long)rghtCurveP,
         subdivisionIndex);

    /* The last point of the left-hand curve will also be the first
       point of the right-hand curve.
    */
    assert(subdivisionIndex < CURVE_LENGTH(curveP));
    CURVE_LENGTH(leftCurveP) = subdivisionIndex + 1;
    CURVE_LENGTH(rghtCurveP) = CURVE_LENGTH(curveP) - subdivisionIndex;

    MALLOCARRAY_NOFAIL(leftCurveP->point_list, CURVE_LENGTH(leftCurveP));
    memcpy(leftCurveP->point_list, &curveP->point_list[0],
           CURVE_LENGTH(leftCurveP) * sizeof(curveP->point_list[0]));

    MALLOCARRAY_NOFAIL(rghtCurveP->point_list, CURVE_LENGTH(rghtCurveP));
    memcpy(rghtCurveP->point_list, &curveP->point_list[subdivisionIndex],
           CURVE_LENGTH(rghtCurveP) * sizeof(curveP->point_list[0]));

    /* We have to set up the two curves before finding the slope at
       the subdivision point.  The slope at that point must be the
       same for both curves, or noticeable bumps will occur in the
       character.  But we want to use information on both sides of the
       point to compute the slope, hence we use adjacentCurveP.
    */
    findTangent(leftCurveP,
                LINEEND_TERM,
                /* adjacentCurveP: */ rghtCurveP,
                fittingOptsP->tangent_surround, joinSlopeP);

    *leftCurvePP = leftCurveP;
    *rghtCurvePP = rghtCurveP;
}



static spline_list_type *
leftRightConcat(const spline_list_type *  const leftSplineListP,
                const spline_list_type *  const rghtSplineListP,
                at_exception_type *       const exceptionP) {
/*----------------------------------------------------------------------------
   Return a spline list which is the concatenation of the spline lists
   obtained by splitting a curve in two and fitting each independently.
   NULL for a spline list pointer means Caller was unable to fit a list
   of splines to that side of the curve.
-----------------------------------------------------------------------------*/
    spline_list_type * retval;

    retval = new_spline_list();

    if (leftSplineListP == NULL) {
        LOG("Could not fit spline to left curve.\n");
        at_exception_warning(exceptionP, "Could not fit left spline list");
    } else
        concat_spline_lists(retval, *leftSplineListP);

    if (rghtSplineListP == NULL) {
        LOG("Could not fit spline to right curve.\n");
        at_exception_warning(exceptionP, "Could not fit right spline list");
    } else
        concat_spline_lists(retval, *rghtSplineListP);

    return retval;
}



static unsigned int
divisionPoint(curve *      const curveP,
              unsigned int const worstFitPoint) {
/*----------------------------------------------------------------------------
   Return the sequence number of the point at which we should divide
   curve *curveP for the purpose of doing a separate fit of each side,
   assuming the point which least matches a single spline is sequence
   number 'worstFitPoint'.

   We get as close as we can to that while still having at least two
   points on each side.

   Assume the curve is at least 4 points long.

   The return value is the sequence number of the first point of the
   second (right-hand) subcurve.
-----------------------------------------------------------------------------*/
    assert(CURVE_LENGTH(curveP) >= 4);

    return MAX(2, MIN(worstFitPoint, CURVE_LENGTH(curveP) - 2));
}



static spline_list_type *
divideAndFit(curve *                   const curveP,
             Vector               const begSlope,
             Vector               const endSlope,
             unsigned int              const subdivisionIndex,
             const fitting_opts_type * const fittingOptsP,
             at_exception_type *       const exceptionP) {
/*----------------------------------------------------------------------------
  Same as fitWithLeastSquares() (i.e. return a list of splines that fit
  the curve *curveP), except assuming no single spline will fit the
  entire curve.

  Divide it into two curves at 'subdivisionIndex' and fit each
  separately to a list of splines.  Return the concatenation of those
  spline lists.

  Assume 'subdivisionIndex' leaves at least two pixels on each side.
-----------------------------------------------------------------------------*/
    spline_list_type * retval;
    curve * leftCurveP;
        /* The beginning (lower indexes) subcurve */
    curve * rghtCurveP;
        /* The other subcurve */
    Vector joinSlope;
        /* The slope of the end of the left subcurve and start of the right
           subcurve.
        */
    spline_list_type * leftSplineListP;

    assert(subdivisionIndex > 1);
    assert(subdivisionIndex < CURVE_LENGTH(curveP)-1);
    subdivideCurve(curveP, subdivisionIndex, fittingOptsP,
                   &leftCurveP, &rghtCurveP, &joinSlope);

    leftSplineListP = fitCurve(leftCurveP, begSlope, joinSlope,
                               fittingOptsP, exceptionP);

    if (!at_exception_got_fatal(exceptionP)) {
        spline_list_type * rghtSplineListP;

        rghtSplineListP = fitCurve(rghtCurveP, joinSlope, endSlope,
                                   fittingOptsP, exceptionP);

        if (!at_exception_got_fatal(exceptionP)) {
            if (leftSplineListP == NULL && rghtSplineListP == NULL)
                retval = NULL;
            else
                retval = leftRightConcat(leftSplineListP, rghtSplineListP,
                                         exceptionP);

            if (rghtSplineListP) {
                free_spline_list(*rghtSplineListP);
                free(rghtSplineListP);
            }
        }
        if (leftSplineListP) {
            free_spline_list(*leftSplineListP);
            free(leftSplineListP);
        }
    }

    free_curve(leftCurveP);
    free_curve(rghtCurveP);

    return retval;
}



static spline_list_type *
fitWithLeastSquares(curve *                   const curveP,
                    Vector               const begSlope,
                    Vector               const endSlope,
                    const fitting_opts_type * const fittingOptsP,
                    at_exception_type *       const exceptionP) {
/*----------------------------------------------------------------------------
  The least squares method is well described in Schneider's thesis.
  Briefly, we try to fit the entire curve with one spline.  If that
  fails, we subdivide the curve.
-----------------------------------------------------------------------------*/
    spline_list_type * retval;
    spline_type spline;

    LOG("\nFitting with least squares:\n");

    /* Phoenix reduces the number of points with a "linear spline
       technique."  But for fitting letterforms, that is
       inappropriate.  We want all the points we can get.
    */

    setInitialParameterValues(curveP);

    if (CURVE_CYCLIC(curveP) && CURVE_LENGTH(curveP) < 4) {
        unsigned i;
        for (i = 0; i < CURVE_LENGTH(curveP); ++i) {
            Point const point = CURVE_POINT(curveP, i);
            fprintf(stderr, "point %u = (%f, %f)\n", i, point.x, point.y);
        }
    }

    /* Try a single spline over whole curve */

    spline = fitOneSpline(curveP, begSlope, endSlope, exceptionP);
    if (!at_exception_got_fatal(exceptionP)) {
        float error;
        unsigned int worstPoint;

        logSplineFit(spline);

        findError(curveP, spline, &error, &worstPoint, exceptionP);
        assert(worstPoint < CURVE_LENGTH(curveP));

        if (error < fittingOptsP->error_threshold && !CURVE_CYCLIC(curveP)) {
            /* The points were fitted adequately with a spline.  But
               see if the "curve" that was fit should really just be a
               straight line.
            */
            if (splineLinearEnough(&spline, curveP, fittingOptsP)) {
                SPLINE_DEGREE(spline) = LINEARTYPE;
                LOG("Changed to line.\n");
            }
            retval = new_spline_list_with_spline(spline);
            LOG1("Accepted error of %.3f.\n", error);
        } else {
            /* We couldn't fit the curve acceptably with a single spline,
               so divide into two curves and try to fit each separately.
            */
            unsigned int const divIndex = divisionPoint(curveP, worstPoint);
            LOG1("\nSubdividing at point #%u\n", divIndex);
            LOG4("  Worst match point: (%.3f,%.3f), #%u.  Error %.3f\n",
                 CURVE_POINT(curveP, worstPoint).x,
                 CURVE_POINT(curveP, worstPoint).y, worstPoint, error);

            retval = divideAndFit(curveP, begSlope, endSlope, divIndex,
                                  fittingOptsP, exceptionP);
        }
    } else
        retval = NULL; /* quiet compiler warning */

    return retval;
}



static spline_list_type *
fitCurve(curve *                   const curveP,
         Vector               const begSlope,
         Vector               const endSlope,
         const fitting_opts_type * const fittingOptsP,
         at_exception_type *       const exceptionP) {
/*----------------------------------------------------------------------------
  Transform a set of locations to a list of splines (the fewer the
  better).  We are guaranteed that *curveP does not contain any corners.
  We return NULL if we cannot fit the points at all.
-----------------------------------------------------------------------------*/
    spline_list_type * fittedSplinesP;

    if (CURVE_LENGTH(curveP) < 2) {
        LOG("Tried to fit curve with fewer than two points");
        at_exception_warning(exceptionP,
                             "Tried to fit curve with less than two points");
        fittedSplinesP = NULL;
    } else if (CURVE_LENGTH(curveP) < 4)
        fittedSplinesP = fitWithLine(curveP);
    else
        fittedSplinesP =
            fitWithLeastSquares(curveP, begSlope, endSlope, fittingOptsP,
                                exceptionP);

    return fittedSplinesP;
}



static void
fitCurves(curve_list_type           const curveList,
          pixel                     const color,
          const fitting_opts_type * const fittingOptsP,
          spline_list_type *        const splinesP,
          at_exception_type *       const exceptionP) {

    spline_list_type curveListSplines;
    unsigned int curveSeq;

    curveListSplines = empty_spline_list();

    curveListSplines.open      = curveList.open;
    curveListSplines.clockwise = curveList.clockwise;
    curveListSplines.color     = color;

    for (curveSeq = 0;
         curveSeq < curveList.length && !at_exception_got_fatal(exceptionP);
         ++curveSeq) {

        curve * const curveP = CURVE_LIST_ELT(curveList, curveSeq);

        Vector begSlope, endSlope;
        spline_list_type * curveSplinesP;

        LOG2("\nFitting curve #%u (%lx):\n", curveSeq, (unsigned long)curveP);

        LOG("Finding tangents:\n");
        findTangent(curveP, LINEEND_INIT,
                    CURVE_CYCLIC(curveP) ? curveP : NULL,
                    fittingOptsP->tangent_surround,
                    &begSlope);
        findTangent(curveP, LINEEND_TERM,
                    CURVE_CYCLIC(curveP) ? curveP : NULL,
                    fittingOptsP->tangent_surround, &endSlope);

        curveSplinesP = fitCurve(curveP, begSlope, endSlope, fittingOptsP,
                                 exceptionP);
        if (!at_exception_got_fatal(exceptionP)) {
            if (curveSplinesP == NULL) {
                LOG1("Could not fit curve #%u", curveSeq);
                at_exception_warning(exceptionP, "Could not fit curve");
            } else {
                logSplinesForCurve(curveSeq, *curveSplinesP);

                /* After fitting, we may need to change some would-be lines
                   back to curves, because they are in a list with other
                   curves.
                */
                changeBadLines(curveSplinesP, fittingOptsP);

                concat_spline_lists(&curveListSplines, *curveSplinesP);
                free_spline_list(*curveSplinesP);
                free(curveSplinesP);
            }
        }
    }
    if (at_exception_got_fatal(exceptionP))
        free_spline_list(curveListSplines);
    else
        *splinesP = curveListSplines;
}



static void
logFittedSplines(spline_list_type const curve_list_splines) {

    unsigned int splineSeq;

    LOG("\nFitted splines are:\n");
    for (splineSeq = 0;
         splineSeq < SPLINE_LIST_LENGTH(curve_list_splines);
         ++splineSeq) {
        LOG1("  %u: ", splineSeq);
        print_spline(log_file,
                     SPLINE_LIST_ELT(curve_list_splines, splineSeq));
    }
}



static void
fitCurveList(curve_list_type     const curveList,
             fitting_opts_type * const fittingOptsP,
             distance_map_type * const dist,
             pixel               const color,
             spline_list_type *  const splineListP,
             at_exception_type * const exception) {
/*----------------------------------------------------------------------------
  Fit the list of curves CURVE_LIST to a list of splines, and return
  it.  CURVE_LIST represents a single closed paths, e.g., either the
  inside or outside outline of an `o'.
-----------------------------------------------------------------------------*/
    curve_type curve;
    spline_list_type curveListSplines;

    removeKnees(curveList);

    if (dist != NULL)
        computePointWeights(curveList, fittingOptsP, dist);

    /* We filter all the curves in 'curveList' at once; otherwise, we
       would look at an unfiltered curve when computing tangents.
    */
    filterCurves(curveList, fittingOptsP);

    /* Make the first point in the first curve also be the last point in
       the last curve, so the fit to the whole curve list will begin and
       end at the same point.  This may cause slight errors in computing
       the tangents and t values, but it's worth it for the continuity.
       Of course we don't want to do this if the two points are already
       the same, as they are if the curve is cyclic.  (We don't append it
       earlier, in `split_at_corners', because that confuses the
       filtering.)  Finally, we can't append the point if the curve is
       exactly three points long, because we aren't adding any more data,
       and three points isn't enough to determine a spline.  Therefore,
       the fitting will fail.
    */
    curve = CURVE_LIST_ELT(curveList, 0);
    if (CURVE_CYCLIC(curve))
        append_point(curve, CURVE_POINT(curve, 0));

    /* Finally, fit each curve in the list to a list of splines.  */

    fitCurves(curveList, color, fittingOptsP, &curveListSplines, exception);
    if (!at_exception_got_fatal(exception)) {
        if (log_file)
            logFittedSplines(curveListSplines);
        *splineListP = curveListSplines;
    }
}



static void
fitCurvesToSplines(curve_list_array_type    const curveArray,
                   fitting_opts_type *      const fittingOptsP,
                   distance_map_type *      const dist,
                   unsigned short           const width,
                   unsigned short           const height,
                   at_exception_type *      const exception,
                   at_progress_func               notifyProgress,
                   void *                   const progressData,
                   at_testcancel_func             testCancel,
                   void *                   const testcancelData,
                   spline_list_array_type * const splineListArrayP) {

    unsigned splineListSeq;
    bool cancelled;
    spline_list_array_type splineListArray;

    splineListArray = new_spline_list_array();
    splineListArray.centerline          = fittingOptsP->centerline;
    splineListArray.preserve_width      = fittingOptsP->preserve_width;
    splineListArray.width_weight_factor = fittingOptsP->width_weight_factor;
    splineListArray.backgroundSpec      = fittingOptsP->backgroundSpec;
    splineListArray.background_color    = fittingOptsP->background_color;
    /* Set dummy values. Real value is set in upper context. */
    splineListArray.width  = width;
    splineListArray.height = height;

    for (splineListSeq = 0, cancelled = false;
         splineListSeq < CURVE_LIST_ARRAY_LENGTH(curveArray) &&
             !at_exception_got_fatal(exception) && !cancelled;
         ++splineListSeq) {

        curve_list_type const curveList =
            CURVE_LIST_ARRAY_ELT(curveArray, splineListSeq);

        spline_list_type curveSplineList;

        if (notifyProgress)
            notifyProgress((((float)splineListSeq)/
                            ((float)CURVE_LIST_ARRAY_LENGTH(curveArray) *
                             (float)3.0) + (float)0.333),
                           progressData);
        if (testCancel && testCancel(testcancelData))
            cancelled = true;

        LOG1("\nFitting curve list #%u:\n", splineListSeq);

        fitCurveList(curveList, fittingOptsP, dist, curveList.color,
                     &curveSplineList, exception);
        if (!at_exception_got_fatal(exception))
            append_spline_list(&splineListArray, curveSplineList);
    }
    *splineListArrayP = splineListArray;
}



void
fit_outlines_to_splines(pixel_outline_list_type  const pixelOutlineList,
                        fitting_opts_type *      const fittingOptsP,
                        distance_map_type *      const dist,
                        unsigned short           const width,
                        unsigned short           const height,
                        at_exception_type *      const exception,
                        at_progress_func               notifyProgress,
                        void *                   const progressData,
                        at_testcancel_func             testCancel,
                        void *                   const testcancelData,
                        spline_list_array_type * const splineListArrayP) {
/*----------------------------------------------------------------------------
   Transform a list of pixels in the outlines of the original character to
   a list of spline lists fitted to those pixels.
-----------------------------------------------------------------------------*/
    curve_list_array_type const curveListArray =
        split_at_corners(pixelOutlineList, fittingOptsP, exception);

    fitCurvesToSplines(curveListArray, fittingOptsP, dist, width, height,
                       exception, notifyProgress, progressData,
                       testCancel, testcancelData, splineListArrayP);

    free_curve_list_array(&curveListArray, notifyProgress, progressData);

    flush_log_output();
}



