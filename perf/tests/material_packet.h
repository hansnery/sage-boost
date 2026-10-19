#ifndef PF_MATERIAL_PACKET_H
#define PF_MATERIAL_PACKET_H
/* Numeric values for the controlled fixture only; texture is a stable
 * fixture-local index, never a borrowed interface or engine pointer. */
typedef struct {
    float opacity,diffuse[3],clock;
    unsigned texture;
} fixture_material_values;
void material_fixture_capture(unsigned group,unsigned epoch,unsigned change_percent,
                              unsigned milliseconds,fixture_material_values *out);
void material_fixture_captured(const fixture_material_values *values,unsigned groups);
#endif
