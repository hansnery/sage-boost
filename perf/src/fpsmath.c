#include "fpsmath.h"

/* Matrix3D: row r, column c at [r * 4 + c]; columns 0..2 are the axes. */
#define M(m, r, c) ((m)[(r) * 4 + (c)])

static inline void normalize_column(float *m, int c)
{
    float x = M(m, 0, c), y = M(m, 1, c), z = M(m, 2, c);
    float len2 = x * x + y * y + z * z;
    float inv;
    if (!(len2 > 1e-12f))
        return;
    inv = 1.0f / __builtin_sqrtf(len2);
    M(m, 0, c) = x * inv;
    M(m, 1, c) = y * inv;
    M(m, 2, c) = z * inv;
}

static int step_ok(const float *prev, const float *cur, unsigned max_step)
{
    float lim = (float)(int)max_step, d;
    int i;
    if (!max_step)
        return 1;
    for (i = 3; i < 12; i += 4) {
        d = cur[i] - prev[i];
        if (!(d <= lim && d >= -lim))
            return 0;
    }
    return 1;
}

int pf_mat_step_ok(const float *prev, const float *cur, unsigned max_step)
{
    unsigned csr;
    int ok;
    __asm__ volatile("stmxcsr %0" : "=m"(csr));
    ok = step_ok(prev, cur, max_step);
    __asm__ volatile("ldmxcsr %0" : : "m"(csr));
    return ok;
}

/* out = from + (cur - prev) * t, t = t_q16 / 65536 (-1..1), re-orthonormalized. */
static int blend(const float *from, const float *prev, const float *cur, int t_q16,
                 unsigned max_step, float *out)
{
    unsigned csr;
    float f, d;
    int i;

    __asm__ volatile("stmxcsr %0" : "=m"(csr));
    if (!step_ok(prev, cur, max_step)) {
        __asm__ volatile("ldmxcsr %0" : : "m"(csr));
        return 0;
    }
    if (t_q16 > 65536)
        t_q16 = 65536;
    if (t_q16 < -65536)
        t_q16 = -65536;
    f = (float)t_q16 * (1.0f / 65536.0f);
    for (i = 0; i < 12; i++)
        out[i] = from[i] + (cur[i] - prev[i]) * f;
    /* Gram-Schmidt on the axes; the third is the cross product, so the
     * result stays a right-handed rotation like the camera's. */
    normalize_column(out, 0);
    d = M(out, 0, 0) * M(out, 0, 1) + M(out, 1, 0) * M(out, 1, 1) + M(out, 2, 0) * M(out, 2, 1);
    for (i = 0; i < 3; i++)
        M(out, i, 1) -= d * M(out, i, 0);
    normalize_column(out, 1);
    M(out, 0, 2) = M(out, 1, 0) * M(out, 2, 1) - M(out, 2, 0) * M(out, 1, 1);
    M(out, 1, 2) = M(out, 2, 0) * M(out, 0, 1) - M(out, 0, 0) * M(out, 2, 1);
    M(out, 2, 2) = M(out, 0, 0) * M(out, 1, 1) - M(out, 1, 0) * M(out, 0, 1);
    __asm__ volatile("ldmxcsr %0" : : "m"(csr));
    return 1;
}

int pf_mat_offset(const float *prev, const float *cur, int t_q16, unsigned max_step, float *out)
{
    return blend(cur, prev, cur, t_q16, max_step, out);
}

int pf_step_uneven(const float *pprev, const float *prev, const float *cur)
{
    unsigned csr;
    float d1, d2, a = 0, b = 0, e = 0;
    int i, r;
    __asm__ volatile("stmxcsr %0" : "=m"(csr));
    for (i = 3; i < 12; i += 4) {
        d1 = prev[i] - pprev[i];
        d2 = cur[i] - prev[i];
        a += d1 * d1;
        b += d2 * d2;
        e += (d2 - d1) * (d2 - d1);
    }
    /* Not moving (under 0.01 units a frame either time). */
    if (!(a > 1e-4f) && !(b > 1e-4f))
        r = -1;
    else
        r = e * 16.0f > (a > b ? a : b) ? 1 : 0;
    __asm__ volatile("ldmxcsr %0" : : "m"(csr));
    return r;
}

int pf_mat_interpolate(const float *prev, const float *cur, unsigned f_q16, unsigned max_step,
                       float *out)
{
    return blend(prev, prev, cur, f_q16 > 65536u ? 65536 : (int)f_q16, max_step, out);
}
