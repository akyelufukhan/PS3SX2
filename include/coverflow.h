#ifndef PS3SX2_COVERFLOW_H
#define PS3SX2_COVERFLOW_H

/*
 * Layout math for the cover carousel, kept free of any PS3 code so it can be
 * unit tested. Units are Tiny3D world units: x to the right, y up, z into the screen.
 */

#define CF_FOV      45.0f  /* camera field of view in degrees; narrow keeps side covers slim */
#define CF_COVER_W  1.5f   /* PS2 case is roughly 135 x 190 mm */
#define CF_COVER_H  2.1f
#define CF_VISIBLE  6.0f   /* covers further than this from the center aren't drawn */

typedef struct {
    float x;      /* horizontal position */
    float z;      /* depth; the centered cover sits closest to the camera */
    float angle;  /* rotation around Y in radians; positive turns a right-hand cover toward the center */
    float focus;  /* 1 for the centered cover, falling to 0 one slot away */
    float shade;  /* 1 = full brightness, side covers are dimmed */
    float alpha;  /* fades out the outermost covers */
} cf_pose;

/*
 * Pose of a cover whose distance from the scroll position is `offset`
 * (cover index minus the animated position). Continuous in `offset`, so
 * covers glide smoothly while scrolling.
 */
void coverflow_pose(float offset, cf_pose *out);

/* Moves the animated position toward the selected index; call once per frame. */
float coverflow_step(float pos, float target);

#endif
