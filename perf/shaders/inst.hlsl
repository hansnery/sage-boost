// Instanced vertex shaders for the unit meshes of BFME2's FX path.
//
// Written for bfme2perf. tools/inst_shaders.py compiles this file once per
// game shader variant, with defines for the family, the skinning, the
// texture-coordinate mappings and the register of every constant, so each
// output reads the constants the game's own variant left on the device.
// Only each instance's bones (or world matrix) come from IR, starting at the
// per-instance base register in TEXCOORD7.
//
// FAMILY 1: lit, receives the shadow map. 2: lit, no shadow map.
//        3: writes the shadow map (depth).
//        4: lit, low shader quality (defaultw3d's vs_1_1): 2 without fog.
//        5: normal-mapped material at low shader quality (normalmapped's
//           vs_1_1): diffuse lighting only, no vertex colour, the opacity
//           override as alpha, vertex fog in oFog.
// JOINTS 0: rigid (3 world rows per instance). 1, 2: skinned, bones as
//        { rotation quaternion, translation } pairs of registers.
// MAP0   0: uv0. 1: uv0 * scale + offset. 2: uv0 + animation offset.
// MAP1   0: uv1. 1: uv1 * scale + offset.

float4 IR[IR_COUNT] : register(IR_REG);

#if FAMILY == 3
float4 OPAC : register(R_OPAC);
float4 TT0OFF : register(R_TT0OFF);
float4 TAOFF : register(R_TAOFF);
float4 BMFLAG : register(R_BMFLAG);
float4 VIEWM[4] : register(R_VIEW);
float4 PROJ[4] : register(R_PROJ);
float4 TT0 : register(R_TT0);
#elif FAMILY == 5
float4 VP[4] : register(R_VP);
float4 RCPRANGE : register(R_RCPRANGE);
float4 AMB : register(R_AMB);
float4 FOGRCP : register(R_FOGRCP);
float4 DL[6] : register(R_DL);
float4 FOG[3] : register(R_FOG);
float4 VIEWI[3] : register(R_VIEWI);
float4 PL[2] : register(R_PL);
float4 DIFF : register(R_DIFF);
float4 SHROUD : register(R_SHROUD);
float4 OPACOV : register(R_OPACOV);
#else
float4 VP[4] : register(R_VP);
float4 RCPRANGE : register(R_RCPRANGE);
float4 AMB : register(R_AMB);
float4 OPAC : register(R_OPAC);
#if FAMILY != 4
float4 FOGRCP : register(R_FOGRCP);
float4 FOG[3] : register(R_FOG);
#endif
float4 DL[6] : register(R_DL);
float4 VIEWI[3] : register(R_VIEWI);
float4 PL[2] : register(R_PL);
float4 CD : register(R_CD);
float4 CS : register(R_CS);
float4 SHIN : register(R_SHIN);
float4 SHROUD : register(R_SHROUD);
float4 HCE : register(R_HCE);
#if MAP0 != 0
float4 MAP0OFF : register(R_MAP0OFF);
#endif
#if MAP0 == 1
float4 TT0 : register(R_TT0);
#endif
#if MAP1 == 1
float4 MAP1OFF : register(R_MAP1OFF);
float4 TT1 : register(R_TT1);
#endif
#if FAMILY == 1
float4 SI[4] : register(R_SI);
float4 CLOUD : register(R_CLOUD);
float4 CLOUDOFF : register(R_CLOUDOFF);
#endif
#endif

struct VSIn {
    float4 pos0 : POSITION0;
#if JOINTS == 2
    float4 pos1 : POSITION1;
#endif
#if FAMILY != 3
    float3 nrm0 : NORMAL0;
#if JOINTS == 2
    float3 nrm1 : NORMAL1;
#endif
#endif
#if JOINTS > 0
    float4 bidx : BLENDINDICES0;
#endif
#if JOINTS == 2
    float4 bw : BLENDWEIGHT0;
#endif
    float2 uv0 : TEXCOORD0;
#if FAMILY != 3 && FAMILY != 5
    float2 uv1 : TEXCOORD1;
#endif
#if FAMILY != 5
    float4 col : COLOR0;
#endif
    float inst : TEXCOORD7;
};

struct VSOut {
    float4 pos : POSITION;
    float4 d0 : COLOR0;
#if FAMILY == 3
    float2 t0 : TEXCOORD0;
    float t1 : TEXCOORD1;
#elif FAMILY == 5
    float2 t0 : TEXCOORD0;
    float2 t1 : TEXCOORD1;
    float fog : FOG;
#else
    float4 d1 : COLOR1;
    float2 t0 : TEXCOORD0;
    float2 t1 : TEXCOORD1;
    float2 t2 : TEXCOORD2;
#if FAMILY == 1
    float2 t3 : TEXCOORD3;
    float4 t4 : TEXCOORD4;
    float3 t5 : TEXCOORD5;
#endif
#endif
};

// p rotated by the unit quaternion q: q * p * conjugate(q).
float3 qrot(float4 q, float3 p)
{
    float4 r;
    r.xyz = q.w * p + cross(q.xyz, p);
    r.w = -dot(q.xyz, p);
    return q.w * r.xyz - r.w * q.xyz + cross(q.xyz, r.xyz);
}

float4 rows4(float4 v, float4 m0, float4 m1, float4 m2, float4 m3)
{
    return float4(dot(v, m0), dot(v, m1), dot(v, m2), dot(v, m3));
}

VSOut main(VSIn v)
{
    VSOut o;
    float3 P;
#if FAMILY != 3
    float3 Nw;
#endif

#if JOINTS == 0
    float4 w0 = IR[v.inst], w1 = IR[v.inst + 1], w2 = IR[v.inst + 2];
    float4 pin = float4(v.pos0.xyz, 1);
    P = float3(dot(pin, w0), dot(pin, w1), dot(pin, w2));
#if FAMILY != 3
    Nw = float3(dot(v.nrm0, w0.xyz), dot(v.nrm0, w1.xyz), dot(v.nrm0, w2.xyz));
#endif
#else
    // D3DCOLOR blend indices: bone b reads as b / 255; two registers per bone.
    float4 bi = v.bidx * 510.003906;
    float ia = v.inst + bi.z;
    float4 qa = IR[ia];
    P = qrot(qa, v.pos0.xyz) + IR[ia + 1].xyz;
#if FAMILY != 3
    Nw = qrot(qa, v.nrm0);
#endif
#if JOINTS == 2
    float ib = v.inst + bi.y;
    float4 qb = IR[ib];
    float3 Pb = qrot(qb, v.pos1.xyz) + IR[ib + 1].xyz;
    P = P * v.bw.x + Pb * v.bw.y;
#if FAMILY != 3
    Nw = Nw * v.bw.x + qrot(qb, v.nrm1) * v.bw.y;
#endif
#endif
#endif

    float4 P1 = float4(P, 1);

#if FAMILY == 3
    float4 pv = rows4(P1, VIEWM[0], VIEWM[1], VIEWM[2], VIEWM[3]);
    float4 pc = rows4(pv, PROJ[0], PROJ[1], PROJ[2], PROJ[3]);
    o.d0 = float4(v.col.w * OPAC.x, 0, 0, 0);
    o.t0 = v.uv0 * TT0.xy + TT0OFF.xy + TAOFF.xy;
    o.t1 = pc.z * (1 / pc.w);
    o.pos = BMFLAG.x * (float4(1, 1, 1, 0) - pc) + pc;
#elif FAMILY == 5
    float3 eye = float3(VIEWI[0].w, VIEWI[1].w, VIEWI[2].w);
    float3 N = normalize(Nw);
    // The game's order: light 1, then 0, then 2, then the point light.
    float3 D = DL[2].xyz * max(dot(N, DL[3].xyz), 0);
    D = DL[0].xyz * max(dot(N, DL[1].xyz), 0) + D;
    D = DL[4].xyz * max(dot(N, DL[5].xyz), 0) + D;
    float3 toPt = PL[1].xyz - P;
    float invPt = rsqrt(dot(toPt, toPt));
    float att = max(1 - (1 / invPt) * RCPRANGE.x, 0);
    att *= att;
    D = att * PL[0].xyz * max(dot(N, toPt * invPt), 0) + D;
    o.d0.xyz = (D * DIFF.xyz + AMB.xyz) * 0.5;
    o.d0.w = OPACOV.x;

    float3 fromEye = P - eye;
    float dist = 1 / rsqrt(dot(fromEye, fromEye));
    o.fog = 1 - FOG[0].x * saturate((dist - FOG[2].x) * FOGRCP.x);
    o.t0 = v.uv0;
    o.t1 = (P.xy + SHROUD.zw) * SHROUD.xy;
    o.pos = rows4(P1, VP[0], VP[1], VP[2], VP[3]);
#else
    float3 eye = float3(VIEWI[0].w, VIEWI[1].w, VIEWI[2].w);
    float3 toEye = eye - P;
    float3 V = toEye * rsqrt(dot(toEye, toEye));
    float3 toPt = PL[1].xyz - P;
    float invPt = rsqrt(dot(toPt, toPt));
    float3 Lp = toPt * invPt;
    float3 N = normalize(Nw);

    float3 spec = 0, diff = 0;
#if FAMILY == 1
    float3 first = 0;
#endif
    [unroll] for (int i = 0; i < 3; i++) {
        float3 Ld = DL[2 * i + 1].xyz;
        float4 l = lit(dot(N, Ld), dot(N, normalize(V + Ld)), SHIN.x);
        spec += DL[2 * i].xyz * l.z;
        diff += DL[2 * i].xyz * l.y;
#if FAMILY == 1
        if (i == 0)
            first = (diff * CD.xyz * v.col.xyz + spec * CS.xyz) * 0.5;
#endif
    }

    float att = max(1 - (1 / invPt) * RCPRANGE.x, 0);
    att *= att;
    float3 pcol = att * PL[0].xyz;
#if FAMILY == 1
    float4 lp = lit(dot(N, Lp), dot(N, normalize(V + Lp)), SHIN.x);
    float3 S = (pcol * CS.xyz * lp.z + spec) * CS.xyz;
    float3 D = pcol * lp.y + diff;
#else
    float3 S = spec * CS.xyz;
    float3 D = pcol * max(dot(N, Lp), 0) + diff;
#endif
    o.d1.xyz = S * 0.5;
    o.d0.xyz = (D * CD.xyz + AMB.xyz) * v.col.xyz * 0.5;

#if FAMILY == 4
    o.d1.w = 1;
#else
    float3 fromEye = P - eye;
    float dist = 1 / rsqrt(dot(fromEye, fromEye));
    o.d1.w = 1 - FOG[0].x * saturate((dist - FOG[2].x) * FOGRCP.x);
#endif
    o.d0.w = v.col.w * OPAC.x;

    float2 T0 = v.uv0;
#if MAP0 == 1
    T0 = v.uv0 * TT0.xy + MAP0OFF.xy;
#elif MAP0 == 2
    T0 = v.uv0 + MAP0OFF.xy;
#endif
    float2 T1 = v.uv1;
#if MAP1 == 1
    T1 = v.uv1 * TT1.xy + MAP1OFF.xy;
#endif
    o.t0 = T0;
    o.t1 = HCE.x * (T0 - T1) + T1;

#if FAMILY == 1
    o.t2 = P.xy * CLOUD.xy + CLOUDOFF.xy;
    o.t3 = (P.xy + SHROUD.zw) * SHROUD.xy;
    o.t4 = rows4(P1, SI[0], SI[1], SI[2], SI[3]);
    o.t5 = first;
#else
    o.t2 = (P.xy + SHROUD.zw) * SHROUD.xy;
#endif
    o.pos = rows4(P1, VP[0], VP[1], VP[2], VP[3]);
#endif
    return o;
}
