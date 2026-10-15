/* WW3D's "rendering the shadow map" byte, raised by
 * W3DShadowMapManager::UpdateShadowMap around its scene render. Found by
 * signature, read (never written) by the instancing to tell the shadow pass
 * from the others. */
#ifndef PF_PASSFLAG_H
#define PF_PASSFLAG_H

#include "scan.h"

/* NULL on success (*flag set), else what wasn't found (*flag NULL). */
const char *pf_shadow_flag_find(const pf_image *img, const volatile unsigned char **flag);

#endif
