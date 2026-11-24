/* Shared key vocabulary for the core views.
 *
 * Kept free of dependencies on purpose: the machine definitions use these
 * constants without pulling in SDL.
 */
#ifndef RG808_VIEW_KEYS_H
#define RG808_VIEW_KEYS_H

/* Shoulder order shared by both shells (identical to their SH_* enum). */
enum { RGSH_L1 = 0, RGSH_L2, RGSH_R1, RGSH_R2, RGSH_COUNT };

/* Action vocabulary shared by the core views. The values mirror the shells'
 * ACT_* enum so a plain cast is enough; the shells assert that mapping at
 * compile time (see the _Static_assert in each shell). */
enum {
    RGV_NONE = 0,
    RGV_UP, RGV_DOWN, RGV_LEFT, RGV_RIGHT,
    RGV_OK, RGV_BACK
};

#endif
