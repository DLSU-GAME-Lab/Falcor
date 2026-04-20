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
// Billboard raster pass for the particle system.
// The vertex shader expands each alive particle index into a screen-facing
// triangle strip quad. The pixel shader renders a soft anti-aliased disc.

// ─── Shared particle layout (must mirror Particles.cs.slang exactly) ─────────

struct Particle
{
    float3 position;
    float age;
    float3 velocity;
    float lifetime;
    float4 color;
    float size;
    uint flags;
    float _pad[2];
};

// ─── Resources ────────────────────────────────────────────────────────────────

StructuredBuffer<Particle> gParticles;
StructuredBuffer<uint> gAliveList;

cbuffer BillboardCB
{
    float4x4 gViewProj;
    float3 gCameraRight; // world-space camera right vector
    float3 gCameraUp;    // world-space camera up vector
};

// ─── Vertex → pixel payload ───────────────────────────────────────────────────

struct V2P
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0; // [0,1] over the quad face
    float4 color : COLOR0;
};

// ─── Vertex shader ────────────────────────────────────────────────────────────
// No vertex buffer is bound. SV_VertexID drives everything:
//   bits [1..n] = index into gAliveList  (which particle)
//   bits [0..1] = corner index 0-3       (which quad corner)
//
// Corner layout (triangle strip order):
//   2 ── 3
//   │  / │
//   0 ── 1

[shader("vertex")]
V2P vsMain(uint vertexID: SV_VertexID)
{
    uint particleIdx = gAliveList[vertexID / 4];
    uint corner = vertexID % 4;
    Particle p = gParticles[particleIdx];

    // Signed offsets in [-1, 1] for each corner (matches strip order above)
    static const float2 kOffsets[4] = {
        float2(-1.f, -1.f), // 0  bottom-left
        float2(1.f, -1.f),  // 1  bottom-right
        float2(-1.f, 1.f),  // 2  top-left
        float2(1.f, 1.f),   // 3  top-right
    };

    float2 off = kOffsets[corner] * p.size;
    float3 worldPos = p.position
                    + gCameraRight * off.x
                    + gCameraUp * off.y;

    V2P out;
    out.posH = mul(gViewProj, float4(worldPos, 1.f));
    out.uv = kOffsets[corner] * 0.5f + 0.5f; // remap to [0, 1]
    out.color = p.color;
    return out;
}

// ─── Pixel shader ─────────────────────────────────────────────────────────────
// Renders a smooth circular disc. Fragments outside the unit circle are
// discarded; the edge is feathered with smoothstep for anti-aliasing.

[shader("pixel")]
float4 psMain(V2P i) : SV_Target
{
    // Remap uv from [0,1] back to [-1,1] to get distance from disc centre
    float2 centered = i.uv * 2.f - 1.f;
    float dist = length(centered);

    if (dist > 1.f) discard;

    // Feather the outer 40% of the radius for a soft edge
    float discAlpha = smoothstep(1.f, 0.6f, dist);
    float4 color = i.color;
    color.a *= discAlpha;

    return color;
}
