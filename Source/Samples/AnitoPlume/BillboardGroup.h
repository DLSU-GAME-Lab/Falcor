#pragma once
#include "Falcor.h"

using namespace Falcor;

struct QuadVertex
{
    float2 position;
    float2 uv;
};

static const QuadVertex kQuadVerts[4] = {
    {{-0.5f, 0.5f}, {0.f, 0.f}},
    {{0.5f, 0.5f}, {1.f, 0.f}},
    {{0.5f, -0.5f}, {1.f, 1.f}},
    {{-0.5f, -0.5f}, {0.f, 1.f}},
};
static const uint16_t kQuadIndices[6] = {0, 1, 2, 0, 2, 3};

class BillboardGroup : public Object
{
public:
    FALCOR_OBJECT(BillboardGroup);

    // Creates a reference to a billboard group.
    static ref<BillboardGroup> create(RenderContext* pRenderContext, ref<Device> pDevice);

    // Call every frame to composite billboard instances onto pTargetFbo.
    void rasterize(RenderContext* pRenderContext, const ref<Fbo> pTargetFbo, const ref<Camera> pCamera);

    //void setCount(uint32_t count);
    //void setInstance(uint32_t index, float3 worldPos, uint32_t texIndex, float2 size);
    //
private:
    BillboardGroup(RenderContext* pRenderContext, ref<Device> pDevice);

    struct Billboard
    {
        float3 position;        // billboard center in world space
        float2 size;            // billboard half-extents in world space
        uint32_t textureIndex;  // index into the texture array
    };

    void createQuadMesh(ref<Device> pDevice);
    void setPerFrameVars(const ref<Fbo>& pTargetFbo, ref<Camera> pCamera);

    ref<Program> mpProgram;
    ref<ProgramVars> mpVars;
    ref<GraphicsState> mpState;

    ref<Buffer> mpVertexBuffer;
    ref<Buffer> mpIndexBuffer;
    ref<Vao> mpVao;

    uint32_t mActiveCount = 1; // number of active billboards to render
};
