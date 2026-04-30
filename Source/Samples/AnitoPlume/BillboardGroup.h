#pragma once
#include "Falcor.h"

using namespace Falcor;

class BillboardGroup : public Object
{
private:
    // Per-instance data uploaded to the GPU
    struct BillboardInstance
    {
        float3 worldPos; // world-space center of the billboard
        uint32_t texId;  // index into the texture array
        float2 size;     // billboard half-extents in world space
        float4 color;    // optional per-instance color
    };

    struct QuadVertex
    {
        float2 position;
        float2 uv;
    };

public:
    class Desc
    {
    private:
        friend class BillboardGroup;

        uint32_t maxCount = 1;
        float minAlphaDistance = 0.f; // distance at which the billboard begins to fade out
        float maxAlphaDistance = 0.f; // distance at which the billboard is fully transparent
        QuadVertex kQuadVerts[4] = {
            {{-0.5f, 0.5f}, {0.f, 0.f}},  // top left
            {{0.5f, 0.5f}, {1.f, 0.f}},   // top right
            {{0.5f, -0.5f}, {1.f, 1.f}},  // bottom right
            {{-0.5f, -0.5f}, {0.f, 1.f}}, // bottom left
        };

    public:
        Desc& setMaxCount(uint32_t count)
        {
            maxCount = count;
            return *this;
        }

        Desc& setMinAlphaDistance(float distance)
        {
            minAlphaDistance = distance;
            return *this;
        }

        Desc& setMaxAlphaDistance(float distance)
        {
            maxAlphaDistance = distance;
            return *this;
        }

        Desc& setQuadOffset(float2 offset)
        {
            for (auto& v : kQuadVerts)
                v.position += offset;
            return *this;
        }
    };

public:
    FALCOR_OBJECT(BillboardGroup);

    // Creates a reference to a billboard group.
    static ref<BillboardGroup> create(RenderContext* pRenderContext, ref<Device> pDevice, Desc desc);

    // Call every frame to composite billboard instances onto pTargetFbo.
    void rasterize(RenderContext* pRenderContext, const ref<Fbo> pTargetFbo, const ref<Camera> pCamera);

    void loadTextures(RenderContext* pRenderContext, ref<Device> pDevice, const std::vector<std::string>& paths);
    void setInstance(uint32_t index, float3 worldPos, uint32_t texIndex, float2 size, float4 color);
    
private:
    BillboardGroup(RenderContext* pRenderContext, ref<Device> pDevice, Desc desc);

    void createQuadMesh(ref<Device> pDevice);
    void updateInstances(RenderContext* pRenderContext, const float3 cameraPos);
    void setPerFrameVars(const ref<Fbo>& pTargetFbo, ref<Camera> pCamera);

    static float lengthSquared(const float3& v) { return v.x * v.x + v.y * v.y + v.z * v.z; };

    ref<Program> mpProgram;
    ref<ProgramVars> mpVars;
    ref<GraphicsState> mpState;

    ref<Sampler> mpSampler;
    ref<Texture> mpTexArray; // Texture array containing all billboard textures
    
    ref<Buffer> mpInstanceBuffer; // Per-instance structured buffer (GPU-side)
    std::vector<BillboardInstance> mInstances; // Per-instance data (CPU-side)
    bool mUpdateInstances = true; // whether to update the instance buffer on the next rasterize() call
    float3 mLastCamPos = { 0, 0, 0 };

    ref<Buffer> mpVertexBuffer;
    ref<Buffer> mpIndexBuffer;
    ref<Vao> mpVao;

    Desc mDesc;

};
