/***************************************************************************
 # Copyright (c) 2015-24, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/


// ParticleBillboard.3d.slang
// Billboard vertex + pixel shader for the AnitoPlume particle system.
//
// Draw call: DrawInstanced(4 verts, aliveCount instances)
//   SV_VertexID   — selects one of the 4 quad corners (TriangleStrip)
//   SV_InstanceID — indexes into gAliveList to find the particle slot

struct Particle
{
    float3 position;
    float  age;
    float3 velocity;
    float  lifetime;
    float4 color;
    float  size;
    uint   flags;
    float  rotation;
    uint   _pad;
};

// ─── Resources ────────────────────────────────────────────────────────────────

StructuredBuffer<Particle> gParticles;
StructuredBuffer<uint>     gAliveList;

cbuffer BillboardCB
{
    float4x4 gViewProj;
    float3   gCameraRight;
    float    _pad0;
    float3   gCameraUp;
    float    _pad1;
};

// TriangleStrip order: TL, BL, TR, BR
// Triangle 1: TL, BL, TR
// Triangle 2: BL, BR, TR
// This winding tiles the quad flat with no crossing diagonals.
static const float2 kCorners[4] =
{
    float2(-0.5,  0.5),   // 0: TL
    float2(-0.5, -0.5),   // 1: BL
    float2( 0.5,  0.5),   // 2: TR
    float2( 0.5, -0.5),   // 3: BR
};

static const float2 kUVs[4] =
{
    float2(0.0, 0.0),   // TL
    float2(0.0, 1.0),   // BL
    float2(1.0, 0.0),   // TR
    float2(1.0, 1.0),   // BR
};

struct VSOut
{
    float4 posH  : SV_Position;
    float4 color : COLOR;
    float2 uv    : TEXCOORD;
};

VSOut vsMain(uint vertID : SV_VertexID, uint instID : SV_InstanceID)
{
    uint particleIdx = gAliveList[instID];
    Particle p       = gParticles[particleIdx];

    float2 corner = kCorners[vertID];

    // Apply per-particle rotation around billboard center
    float s = sin(p.rotation);
    float c = cos(p.rotation);
    float2 rotated = float2(
        corner.x * c - corner.y * s,
        corner.x * s + corner.y * c
    );

    float3 worldPos = p.position
                    + gCameraRight * rotated.x * p.size
                    + gCameraUp    * rotated.y * p.size;

    VSOut o;
    o.posH  = mul(gViewProj, float4(worldPos, 1.0));
    o.color = p.color;
    o.uv    = kUVs[vertID];
    return o;
}

float4 psMain(VSOut i) : SV_Target
{
    // Remap UVs from [0,1] to [-1,1] so the center is at (0,0)
    float2 uv   = i.uv * 2.0 - 1.0;

    // True radial distance from center — avoids the seam that dot(uv,uv)
    // produces at the triangle diagonal boundary
    float dist  = length(uv);

    // Smooth falloff: fully opaque at center, transparent at edges
    float alpha = saturate(1.0 - dist);
    alpha = alpha * alpha;  // squared for a softer, more natural edge

    float4 col = i.color;
    col.a *= alpha;

    if (col.a < 0.01) discard;
    return col;
}
