/* The only floating point in the plugin: moving a W3D Matrix3D
 * (3 rows of rotation | translation) for an extra frame. Compiled with SSE
 * scalar code in its own unit, called only at ABI call boundaries, where the
 * x87 stack is empty and no SSE register is live: the frame limiter's Sleep
 * call and the entry of the particle manager's update. It never touches the
 * x87 unit and restores MXCSR before returning. */
#ifndef PF_FPSMATH_H
#define PF_FPSMATH_H

/* out = cur + (cur - prev) * t, t = t_q16 / 65536 in -1..1 (behind cur when
 * negative), with the rotation re-orthonormalized. out may not alias the
 * inputs. Returns 0 (out unset) if the translation moved more than max_step
 * world units between prev and cur on any axis (0 = no limit). */
int pf_mat_offset(const float *prev, const float *cur, int t_q16, unsigned max_step, float *out);
/* The same between the two: out = prev + (cur - prev) * f. */
int pf_mat_interpolate(const float *prev, const float *cur, unsigned f_q16, unsigned max_step,
                       float *out);
/* The translation's last two steps (pprev -> prev -> cur): -1 if it barely
 * moved, 1 if the step changed by more than a quarter of the larger one, 0
 * otherwise. */
int pf_step_uneven(const float *pprev, const float *prev, const float *cur);
/* 1 unless the translation moved more than max_step on an axis (0 = no limit). */
int pf_mat_step_ok(const float *prev, const float *cur, unsigned max_step);

#endif
