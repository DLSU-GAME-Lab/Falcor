// Per-instance data (matches BillboardInstance in C++)
struct BillboardInstance
{
    float3   worldPos;
    uint     texId;
    float2   size;
    float4   color;
};

// Constant buffer
cbuffer BillboardCB
{
    float3   gCameraPosition;
    float3   gCameraRight;
    float3   gCameraUp;
    float4x4 gViewProj;
    uint gInstanceCount;
    float gMinAlphaDistance;
    float gMaxAlphaDistance;
};

StructuredBuffer<BillboardInstance> gInstances;
Texture2DArray                      gTexArray;
SamplerState                        gSampler;

// VS input comes from the quad VBO
struct VsIn
{
    float2 localPos : POSITION;
    float2 uv       : TEXCOORD;
};

struct VsOut
{
    float4 pos      : SV_Position;
    float3 worldPos : WORLDPOS;
    float2 uv       : TEXCOORD;
    uint texId      : TEXINDEX;
    float4 color    : COLOR;
};

// ---------------------------------------------------------------
[shader("vertex")]
VsOut vsMain(VsIn vIn, uint instanceID : SV_InstanceID)
{
    BillboardInstance inst = gInstances[instanceID];

    // Expand the quad vertex in world space
    float3 worldPos = inst.worldPos
        + gCameraRight * (vIn.localPos.x * inst.size.x)
        + gCameraUp * (vIn.localPos.y * inst.size.y);

    VsOut vOut;
    vOut.pos = mul(gViewProj, float4(worldPos, 1.0));
    vOut.worldPos = worldPos;
    vOut.uv = vIn.uv;
    vOut.texId = inst.texId;
    vOut.color = inst.color;
    return vOut;
}

// ---------------------------------------------------------------
[shader("pixel")]
float4 psMain(VsOut pIn) : SV_Target
{
    float4 color = gTexArray.Sample(
        gSampler,
        float3(pIn.uv, float(pIn.texId))
    );

    float dist = distance(gCameraPosition, pIn.worldPos);
    if (gMaxAlphaDistance > gMinAlphaDistance && dist < gMaxAlphaDistance)
    {
        discard; // Too close to camera, discard
    }
    else if (gMinAlphaDistance != 0.0f && dist < gMinAlphaDistance)
    {
        // Optionally fade out based on distance to camera
        float ratio = max(((dist - gMaxAlphaDistance) / (gMinAlphaDistance - gMaxAlphaDistance)), 0.0);
        float alpha = saturate(ratio);
        color.a *= alpha;
    }

    if (color.a < 0.1) discard;
    else color *= pIn.color;

    // Simple alpha cutout — swap for blending if needed
    clip(color.a - 0.01f);
    return color;
}
