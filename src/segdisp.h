/* segdisp.h — the turbo display's digits: three seven-segment LED digits
 * behind a smoked window on the band, next to the power button.  The 1993
 * case showed the clock in MHz; this one shows the frames per second.
 *
 * The chassis bakes the window and the UNLIT segments - the faint "8.8.8"
 * a dark display shows through its glass - and the composite shader lights
 * the ones that are on, so the digits change without touching the baked
 * case.  Both draw the same geometry, from these numbers, in one space:
 * the window with its height as the unit, x across, y UP.  The shader gets
 * them as uniforms, so this header is the one place they live. */
#ifndef DXM_SEGDISP_H
#define DXM_SEGDISP_H

/* the window, in mm: a 0.3" digit set, three wide */
#define SEG_WIN_W_MM 21.0f
/* digit height as a fraction of the window's; 0.62 of 12.5 mm is 0.3" */
#define SEG_DH 0.62f
/* digit width over its height */
#define SEG_WR 0.58f
/* pitch between digit centres, over the digit width */
#define SEG_PITCH 1.32f
/* segment thickness over the digit height */
#define SEG_T 0.15f
/* the italic lean every LED digit of the period had: x shift per unit y */
#define SEG_SLANT 0.10f
/* Each segment is a hexagon: a bar with 45-degree pointed ends, the tips
 * meeting at the digit's corners.  Neighbours would touch flank to flank
 * there, so every segment stands back from its outline by this much,
 * over the thickness - the hairline between segments a real display has. */
#define SEG_GAP 0.16f

/* Segment endpoints in the digit's own half-extents (x, y in -1..1, y up):
 * a top, b upper right, c lower right, d bottom, e lower left, f upper
 * left, g middle.  Bit s of a digit's mask lights segment s. */
#define SEG_ENDS                                                                                 \
    {                                                                                            \
        {-1, 1, 1, 1}, {1, 1, 1, 0}, {1, 0, 1, -1}, {-1, -1, 1, -1}, {-1, -1, -1, 0},           \
            {-1, 1, -1, 0}, {-1, 0, 1, 0},                                                       \
    }

/* the ten figures, a..g in bits 0..6 */
#define SEG_FIGURES                                                                              \
    { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F }

#endif
