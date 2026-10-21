#include "coverflow.h"

#include <math.h>

/* Tuned in RPCS3 against the design mockup, for a CF_FOV camera. */
#define CF_CENTER_Z 8.45f  /* depth of the selected cover */
#define CF_SIDE_Z   9.55f  /* depth of every other cover, ~13% smaller on screen */
#define CF_SIDE_X   1.6f   /* gap between the selected cover and its neighbours */
#define CF_STACK_X  0.72f  /* gap between consecutive side covers */
#define CF_ANGLE    1.01f  /* side cover rotation, about 58 degrees */
#define CF_DIM      0.45f  /* how much darker side covers get */
#define CF_SPEED    0.22f  /* fraction of the remaining distance covered per frame */

void coverflow_pose(float offset, cf_pose *out)
{
    float a = fabsf(offset);
    float s = offset < 0.0f ? -1.0f : 1.0f;

    if (a < 1.0f) {
        /* Between the center slot and the first side slot: blend. */
        out->x = offset * CF_SIDE_X;
        out->z = CF_CENTER_Z + (CF_SIDE_Z - CF_CENTER_Z) * a;
        out->angle = s * CF_ANGLE * a;
        out->focus = 1.0f - a;
    } else {
        out->x = s * (CF_SIDE_X + (a - 1.0f) * CF_STACK_X);
        out->z = CF_SIDE_Z;
        out->angle = s * CF_ANGLE;
        out->focus = 0.0f;
    }
    out->shade = 1.0f - CF_DIM * (1.0f - out->focus);

    if (a <= CF_VISIBLE - 1.0f)
        out->alpha = 1.0f;
    else if (a >= CF_VISIBLE)
        out->alpha = 0.0f;
    else
        out->alpha = CF_VISIBLE - a;
}

float coverflow_step(float pos, float target)
{
    float d = target - pos;

    if (fabsf(d) < 0.002f)
        return target;
    return pos + d * CF_SPEED;
}
