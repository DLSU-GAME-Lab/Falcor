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
// Billboard shader for AnitoPlume particle system.
// Mirrors BillboardGroup.3d.slang — uses real quad VAO + drawIndexedInstanced.

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
Texture2DArray             gTexArray;
SamplerState               gSampler;

cbuffer BillboardCB
{
    float3   gCameraRight;
    float    _pad0;
    float3   gCameraUp;
    float    _pad1;
    float4x4 gViewProj;
};

struct VsIn
{
    float2 localPos : POSITION;
    float2 uv       : TEXCOORD;
};

struct VsOut
{
    float4 posH  : SV_Position;
    float2 uv    : TEXCOORD;
    float4 color : COLOR;
};

[shader("vertex")]
VsOut vsMain(VsIn vIn, uint instanceID : SV_InstanceID)
{
    uint particleIdx = gAliveList[instanceID];
    Particle p       = gParticles[particleIdx];

    // ── Size scaling by age ───────────────────────────────────────────────
    // Grow from 0 to full size over first 30% of lifetime so newly spawned
    // particles are invisible and expand into the smoke cloud naturally.
    // Shrink back to 0 over the last 10% so death is smooth.
    float t         = saturate(p.age / p.lifetime);
    float growFade  = saturate(t / 0.3f);                    // 0→1 in first 30%
    float shrink    = 1.0 - saturate((t - 0.9f) / 0.1f);    // 1→0 in last 10%
    float scaledSize = p.size * growFade * shrink;

    // ── Per-particle rotation ─────────────────────────────────────────────
    float s = sin(p.rotation);
    float c = cos(p.rotation);
    float2 rotated = float2(
        vIn.localPos.x * c - vIn.localPos.y * s,
        vIn.localPos.x * s + vIn.localPos.y * c
    );

    // ── Expand into world space ───────────────────────────────────────────
    float3 worldPos = p.position
                    + gCameraRight * (rotated.x * scaledSize)
                    + gCameraUp    * (rotated.y * scaledSize);

    VsOut o;
    o.posH  = mul(gViewProj, float4(worldPos, 1.0));
    o.uv    = vIn.uv;
    o.color = p.color;
    return o;
}

[shader("pixel")]
float4 psMain(VsOut i) : SV_Target
{
    float4 texColor = gTexArray.Sample(gSampler, float3(i.uv, 0.0));
    float4 color    = texColor * i.color;

    // Soft radial falloff — smooth puff shape
    float2 uv    = i.uv * 2.0 - 1.0;
    float  dist  = length(uv);
    float  alpha = saturate(1.0 - dist);
    alpha = alpha * alpha * alpha;  // cubic for softer, more smoke-like edge
    color.a *= alpha;

    clip(color.a - 0.01f);
    return color;
}
