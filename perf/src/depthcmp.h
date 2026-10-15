/* Shadow-map texels (D3DFMT_R32F, the depth z/w the shadow pass writes)
 * compared without floating point: each is turned into a signed fixed-point
 * number with 24 fractional bits, read from its IEEE bits. Shared by the
 * verify mode and the GPU test. */
#ifndef PF_DEPTHCMP_H
#define PF_DEPTHCMP_H

/* What the verify clears to: 1.0, as the game clears the shadow map. */
#define PF_DEPTH_CLEAR 0x3F800000u
/* A texel is bad when the two depths differ by more than this many 2^-24:
 * 2^-16 of the depth range (an 8-bit step is 2^-8). */
#define PF_DEPTH_TOLERANCE 256u

/* bits as value * 2^24, saturated at about +-128 (NaN and infinities too). */
static inline int pf_depth_fixed(unsigned bits)
{
    unsigned e = bits >> 23 & 0xFF, m = (bits & 0x7FFFFFu) | 0x800000u, v;
    if (e < 126 - 23)
        return 0;
    if (e >= 126 + 8)
        v = 0x7FFFFFFFu;
    else if (e >= 126)
        v = m << (e - 126);
    else
        v = m >> (126 - e);
    return bits >> 31 ? -(int)v : (int)v;
}

/* |a - b| in 2^-24 steps, saturated. */
static inline unsigned pf_depth_diff(unsigned a, unsigned b)
{
    long long d = (long long)pf_depth_fixed(a) - pf_depth_fixed(b);
    if (d < 0)
        d = -d;
    return d > 0x7FFFFFFF ? 0x7FFFFFFFu : (unsigned)d;
}

#endif
