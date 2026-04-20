#include "BillboardGroup.h"
#include "Scene/TriangleMesh.h"

struct QuadVertex
{
    float2 position;
    float2 uv;
};

static const QuadVertex kQuadVerts[4] = {
    {{-0.5f,  0.5f}, {0.f, 0.f}},
    {{ 0.5f,  0.5f}, {1.f, 0.f}},
    {{ 0.5f, -0.5f}, {1.f, 1.f}},
    {{-0.5f, -0.5f}, {0.f, 1.f}},
};
static const uint16_t kQuadIndices[6] = {0, 1, 2, 0, 2, 3};

ref<BillboardGroup> BillboardGroup::create(RenderContext* pRenderContext, ref<Device> pDevice, uint32_t maxCount)
{
    return ref<BillboardGroup>(new BillboardGroup(pRenderContext, pDevice, maxCount));
}

void BillboardGroup::rasterize(RenderContext* pRenderContext, const ref<Fbo> pTargetFbo, const ref<Camera> pCamera)
{
    mpState->setFbo(pTargetFbo);
    mpState->setVao(mpVao);
    
    setPerFrameVars(pTargetFbo, pCamera);
    updateInstances(pRenderContext);

    pRenderContext->drawIndexedInstanced(mpState.get(), mpVars.get(), 6, mpMaxCount, 0, 0, 0);
}

BillboardGroup::BillboardGroup(RenderContext* pRenderContext, ref<Device> pDevice, uint32_t maxCount) : mpMaxCount(maxCount)
{
    ProgramDesc desc;
    desc.addShaderLibrary("Samples/AnitoPlume/BillboardGroup.3d.slang").vsEntry("vsMain").psEntry("psMain");
    mpProgram = Program::create(pDevice, desc);
    mpVars = ProgramVars::create(pDevice, mpProgram->getReflector());

    RasterizerState::Desc rsDesc;
    rsDesc.setCullMode(RasterizerState::CullMode::None); // quads are single-sided from GS

    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthEnabled(true);
    dsDesc.setDepthWriteMask(true);

    BlendState::Desc blendDesc;
    blendDesc.setRtBlend(0, true);
    blendDesc.setRtParams(
        0,
        BlendState::BlendOp::Add,
        BlendState::BlendOp::Add,
        BlendState::BlendFunc::SrcAlpha,
        BlendState::BlendFunc::OneMinusSrcAlpha,
        BlendState::BlendFunc::One,
        BlendState::BlendFunc::Zero
    );

    mpState = GraphicsState::create(pDevice);
    mpState->setProgram(mpProgram);
    mpState->setRasterizerState(RasterizerState::create(rsDesc));
    mpState->setDepthStencilState(DepthStencilState::create(dsDesc));
    mpState->setBlendState(BlendState::create(blendDesc));

    createQuadMesh(pDevice);

    mInstances.resize(maxCount);
    for (uint32_t i = 0; i < maxCount; i++)
        mInstances[i] = {{0.f, 0.f, 0.f}, 0, {100.f, 100.f}, {1.f, 1.f, 1.f, 1.f}};

    // Allocate GPU instance buffer (StructuredBuffer<BillboardInstance>)
    mpInstanceBuffer = pDevice->createStructuredBuffer(
        sizeof(BillboardInstance),
        mInstances.size(),
        ResourceBindFlags::ShaderResource,
        MemoryType::DeviceLocal,
        mInstances.data(),
        false
    );
}

void BillboardGroup::createQuadMesh(ref<Device> pDevice)
{
    // Vertex layout: float2 localPos, float2 uv
    ref<VertexLayout> pLayout = VertexLayout::create();
    ref<VertexBufferLayout> pBufLayout = VertexBufferLayout::create();
    pBufLayout->addElement("POSITION", 0, ResourceFormat::RG32Float, 1, 0);
    pBufLayout->addElement("TEXCOORD", 8, ResourceFormat::RG32Float, 1, 1);
    pLayout->addBufferLayout(0, pBufLayout);

    mpVertexBuffer = pDevice->createBuffer(
        sizeof(kQuadVerts),
        ResourceBindFlags::Vertex,
        MemoryType::DeviceLocal,
        kQuadVerts
    );

    mpIndexBuffer = pDevice->createBuffer(
        sizeof(kQuadIndices),
        ResourceBindFlags::Index,
        MemoryType::DeviceLocal,
        kQuadIndices
    );

    Vao::BufferVec vbufs = {mpVertexBuffer};
    mpVao = Vao::create(
        Vao::Topology::TriangleList,
        pLayout,
        vbufs,
        mpIndexBuffer,
        ResourceFormat::R16Uint
    );
}

void BillboardGroup::setPerFrameVars(const ref<Fbo>& pTargetFbo, ref<Camera> pCamera)
{
    // Camera right/up vectors for CPU-side billboard orientation
    // (passed as uniforms; the shader uses them directly)
    const float4x4& view = pCamera->getViewMatrix();
    float3 cameraRight = float3(view[0][0], view[0][1], view[0][2]);
    float3 cameraUp = float3(view[1][0], view[1][1], view[1][2]);

    auto var = mpVars->getRootVar();
    var["gInstances"] = mpInstanceBuffer;
    //var["gTexArray"] = mpTextureArray;
    //var["gSampler"] = Sampler::create(pDevice, Sampler::Desc{});
    var["BillboardCB"]["gCameraPosition"] = pCamera->getPosition();
    var["BillboardCB"]["gCameraRight"] = cameraRight;
    var["BillboardCB"]["gCameraUp"] = cameraUp;
    var["BillboardCB"]["gViewProj"] = pCamera->getViewProjMatrix();
    var["BillboardCB"]["gInstanceCount"] = mpMaxCount;
    var["BillboardCB"]["gMinAlphaDistance"] = 20.0f;
}
//
//void BillboardGroup::setCount(uint32_t count)
//{
//    FALCOR_ASSERT(count <= (uint32_t)mInstances.size());
//    mActiveCount = count;
//    mUpdateInstances = true;
//}

void BillboardGroup::setInstance(uint32_t index, float3 worldPos, uint32_t texIndex, float2 size, float4 color)
{
    FALCOR_ASSERT(index < (uint32_t)mInstances.size());
    mInstances[index] = {worldPos, texIndex, size, color};
    mUpdateInstances = true;
}

void BillboardGroup::updateInstances(RenderContext* pRenderContext)
{
    if (!mUpdateInstances || mInstances.size() == 0)
        return;
    mpInstanceBuffer->setBlob(mInstances.data(), 0, mInstances.size() * sizeof(BillboardInstance));
    mUpdateInstances = false;
}
