#pragma once
#include "Falcor.h"

using namespace Falcor;

// Per-instance data uploaded to the GPU
struct BillboardInstance
{
    float3 worldPosition;
    uint32_t textureIndex; // index into the texture array
    float2 size;           // billboard half-extents in world space
    float4 color;          // optional per-instance color
};

class BillboardGroup : public Object
{
public:
    FALCOR_OBJECT(BillboardGroup);

    // Creates a reference to a billboard group.
    static ref<BillboardGroup> create(RenderContext* pRenderContext, ref<Device> pDevice, uint32_t maxCount);

    // Call every frame to composite billboard instances onto pTargetFbo.
    void rasterize(RenderContext* pRenderContext, const ref<Fbo> pTargetFbo, const ref<Camera> pCamera);

    //void setCount(uint32_t count);
    void setInstance(uint32_t index, float3 worldPos, uint32_t texIndex, float2 size, float4 color);
    
private:
    BillboardGroup(RenderContext* pRenderContext, ref<Device> pDevice, uint32_t maxCount);

    void createQuadMesh(ref<Device> pDevice);

    void updateInstances(RenderContext* pRenderContext);
    void setPerFrameVars(const ref<Fbo>& pTargetFbo, ref<Camera> pCamera);

    ref<Program> mpProgram;
    ref<ProgramVars> mpVars;
    ref<GraphicsState> mpState;
    
    ref<Buffer> mpInstanceBuffer; // Per-instance structured buffer (GPU-side)
    std::vector<BillboardInstance> mInstances; // Per-instance data (CPU-side)
    uint32_t mpMaxCount = 1; // number of active billboards to render
    bool mUpdateInstances = true; // whether to update the instance buffer on the next rasterize() call

    ref<Buffer> mpVertexBuffer;
    ref<Buffer> mpIndexBuffer;
    ref<Vao> mpVao;

};
