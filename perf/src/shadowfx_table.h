/* The effect files ShadowBatchIgnoreLight is proven exact for: every
 * shaders\compiled\*.fxo in ROTWK 2.01's Shaders.big (Age of the Ring and
 * Realms in Exile ship no other). `make shadow-fx` checks this table against
 * the local Shaders.big and proves, for each file with a _CreateShadowMap
 * technique, that the technique reads no light, ambient or fog parameter.
 * fxguard.c switches the merge off when the game could load any other. */
#ifndef PF_SHADOWFX_TABLE_H
#define PF_SHADOWFX_TABLE_H

typedef struct {
    const char *name;  /* under shaders\compiled\ */
    unsigned fnv, size; /* FNV-1a 32 of the file, its length */
    int shadow;        /* has a _CreateShadowMap technique (drawn into the shadow map) */
} pf_shadowfx_entry;

#define PF_SHADOWFX_COUNT 13

static const pf_shadowfx_entry pf_shadowfx_table[PF_SHADOWFX_COUNT] = {
    { "debug.fxo", 0x696826B6u, 1872u, 0 },
    { "defaultw3d.fxo", 0x8DF72F18u, 749196u, 1 },
    { "gpuparticle.fxo", 0x462A16B0u, 41564u, 1 },
    { "normalmapped.fxo", 0x4388ABC1u, 112176u, 1 },
    { "phong.fxo", 0x77E70521u, 3652u, 0 },
    { "postfx_lookuptable.fxo", 0xF5CC376Eu, 2148u, 0 },
    { "riverwater.fxo", 0x9AAE8031u, 12380u, 0 },
    { "sampleshader.fxo", 0x28364187u, 6152u, 0 },
    { "shadow.fxo", 0xA142D595u, 5512u, 0 },
    { "simple.fxo", 0x29E357A4u, 5896u, 0 },
    { "terrain.fxo", 0x99C78085u, 121368u, 1 },
    { "vegetation.fxo", 0xF49DB21Bu, 20160u, 1 },
    { "watershader.fxo", 0xA2DC759Bu, 110616u, 0 },
};

#endif
