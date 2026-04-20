// Per-instance data (matches BillboardInstance in C++)
struct BillboardInstance
{
    float3   worldPosition;
    uint     textureIndex;
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
    float2 uv       : TEXCOORD;
    uint   texIndex : TEXINDEX;
    float4 color    : COLOR;
};

// ---------------------------------------------------------------
[shader("vertex")]
VsOut vsMain(VsIn vIn, uint instanceID : SV_InstanceID)
{
    BillboardInstance inst = gInstances[instanceID];

    // Expand the quad vertex in world space
    float3 worldPos = inst.worldPosition
        + gCameraRight * (vIn.localPos.x * inst.size.x)
        + gCameraUp * (vIn.localPos.y * inst.size.y);

    VsOut vOut;
    vOut.pos = mul(gViewProj, float4(worldPos, 1.0));
    vOut.uv = vIn.uv;
    vOut.texIndex = inst.textureIndex;
    vOut.color = inst.color;
    return vOut;
}

// ---------------------------------------------------------------
[shader("pixel")]
float4 psMain(VsOut pIn) : SV_Target
{
    float4 color = pIn.color;

    //if (gMinAlphaDistance != 0.0f)
    //{
    //    // Optionally fade out based on distance to camera
    //    // TODO: Change pIn.pos.xyz to world space position.
    //    float dist = distance(gCameraPosition, pIn.pos.xyz);
    //    if (dist < gMinAlphaDistance)
    //    {
    //        float alpha = saturate(dist / gMinAlphaDistance);
    //        color.a *= alpha;
    //    }
    //}

    //float4 color = gTexArray.Sample(
    //    gSampler,
    //    float3(pIn.uv, (float)pIn.texIndex));

    // Simple alpha cutout — swap for blending if needed
    clip(color.a - 0.01f);
    return color;
}
