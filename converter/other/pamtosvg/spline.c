/* spline.c: spline and spline list (represented as arrays) manipulation. */

#include <assert.h>

#include "mallocvar.h"

#include "message.h"
#include "point.h"
#include "spline.h"
#include "vector.h"

/* Print a spline in human-readable form.  */

void
print_spline (FILE *f, spline_type s)
{
  assert(SPLINE_DEGREE (s) == LINEARTYPE || SPLINE_DEGREE (s) == CUBICTYPE);

  if (SPLINE_DEGREE (s) == LINEARTYPE)
    fprintf (f, "(%.3f,%.3f)--(%.3f,%.3f).\n",
                BEG_POINT (s).x, BEG_POINT (s).y,
                END_POINT (s).x, END_POINT (s).y);

  else if (SPLINE_DEGREE (s) == CUBICTYPE)
    fprintf (f, "(%.3f,%.3f)..ctrls(%.3f,%.3f)&(%.3f,%.3f)..(%.3f,%.3f).\n",
             BEG_POINT (s).x, BEG_POINT (s).y,
             CONTROL1  (s).x, CONTROL1  (s).y,
             CONTROL2  (s).x, CONTROL2  (s).y,
             END_POINT (s).x, END_POINT (s).y);
}



/* Evaluate the spline S at a given T value.  This is an implementation
   of de Casteljau's algorithm.  See Schneider's thesis, p.37.
   The variable names are taken from there.  */

Point
evaluate_spline (spline_type s, float t)
{
  spline_type V[4];    /* We need degree+1 splines, but assert degree <= 3.  */
  signed i, j;
  float one_minus_t = (float) 1.0 - t;
  polynomial_degree degree = SPLINE_DEGREE (s);

  for (i = 0; i <= degree; i++)
    {
      V[0].v[i].x = s.v[i].x;
      V[0].v[i].y = s.v[i].y;
      V[0].v[i].z = s.v[i].z;
    }

  for (j = 1; j <= degree; j++)
    for (i = 0; i <= degree - j; i++)
      {
        Point t1 = point_scaled(V[j - 1].v[i], one_minus_t);
        Point t2 = point_scaled(V[j - 1].v[i + 1], t);
        Point temp = point_sum(t1, t2);
        V[j].v[i].x = temp.x;
        V[j].v[i].y = temp.y;
        V[j].v[i].z = temp.z;
      }

  return V[degree].v[0];
}



/* Return a new, empty, spline list.  */

spline_list_type *
new_spline_list (void)
{
  spline_list_type *answer;

  MALLOCVAR(answer);
  *answer = empty_spline_list();
  return answer;
}



spline_list_type
empty_spline_list (void)
{
  spline_list_type answer;
  SPLINE_LIST_DATA (answer) = NULL;
  SPLINE_LIST_LENGTH (answer) = 0;
  return answer;
}



/* Return a new spline list with SPLINE as the first element.  */

spline_list_type *
new_spline_list_with_spline (spline_type spline)
{
  spline_list_type *answer;

  answer = new_spline_list();
  MALLOCVAR(SPLINE_LIST_DATA(*answer));
  SPLINE_LIST_ELT (*answer, 0) = spline;
  SPLINE_LIST_LENGTH (*answer) = 1;

  return answer;
}



/* Free the storage in a spline list.  We don't have to free the
   elements, since they are arrays in automatic storage.  And we don't
   want to free the list if it was empty.  */



void
free_spline_list(spline_list_type spline_list) {

    if (SPLINE_LIST_DATA(spline_list) != NULL)
        free(SPLINE_LIST_DATA(spline_list));
}



/* Append the spline S to the list SPLINE_LIST.  */

void
append_spline (spline_list_type *l, spline_type s)
{
  assert (l != NULL);

  SPLINE_LIST_LENGTH (*l)++;
  REALLOCARRAY(SPLINE_LIST_DATA(*l), SPLINE_LIST_LENGTH(*l));
  LAST_SPLINE_LIST_ELT (*l) = s;
}



/* Tack the elements in the list S2 onto the end of S1.
   S2 is not changed.  */

void
concat_spline_lists (spline_list_type *s1, spline_list_type s2)
{
  unsigned this_spline;
  unsigned new_length;

  assert (s1 != NULL);

  new_length = SPLINE_LIST_LENGTH (*s1) + SPLINE_LIST_LENGTH (s2);

  REALLOCARRAY_NOFAIL(SPLINE_LIST_DATA(*s1), new_length);

  for (this_spline = 0; this_spline < SPLINE_LIST_LENGTH (s2); this_spline++)
    SPLINE_LIST_ELT (*s1, SPLINE_LIST_LENGTH (*s1)++)
      = SPLINE_LIST_ELT (s2, this_spline);
}



/* Return a new, empty, spline list array.  */

spline_list_array_type
new_spline_list_array (void)
{
  spline_list_array_type answer;

  SPLINE_LIST_ARRAY_DATA (answer) = NULL;
  SPLINE_LIST_ARRAY_LENGTH (answer) = 0;

  return answer;
}



/* Free the storage in a spline list array.  We don't
   want to free the list if it is empty.  */
void
free_spline_list_array (spline_list_array_type *spline_list_array)
{
  unsigned this_list;

  for (this_list = 0;
       this_list < SPLINE_LIST_ARRAY_LENGTH (*spline_list_array);
       this_list++)
    free_spline_list (SPLINE_LIST_ARRAY_ELT (*spline_list_array, this_list));

  if (SPLINE_LIST_ARRAY_DATA (*spline_list_array) != NULL)
    free (SPLINE_LIST_ARRAY_DATA (*spline_list_array));

  flush_log_output ();
}



/* Append the spline S to the list SPLINE_LIST_ARRAY.  */

void
append_spline_list (spline_list_array_type *l, spline_list_type s)
{
  SPLINE_LIST_ARRAY_LENGTH (*l)++;
  REALLOCARRAY_NOFAIL(SPLINE_LIST_ARRAY_DATA(*l),
                      SPLINE_LIST_ARRAY_LENGTH(*l));
  LAST_SPLINE_LIST_ARRAY_ELT (*l) = s;
}



