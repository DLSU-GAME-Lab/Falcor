#include "BillboardGroup.h"
#include "Scene/TriangleMesh.h"
#include "Core/AssetResolver.h"

static const uint16_t kQuadIndices[6] = {0, 1, 2, 0, 2, 3}; // CCW

ref<BillboardGroup> BillboardGroup::create(RenderContext* pRenderContext, ref<Device> pDevice, Desc desc)
{
    return ref<BillboardGroup>(new BillboardGroup(pRenderContext, pDevice, desc));
}

void BillboardGroup::rasterize(RenderContext* pRenderContext, const ref<Fbo> pTargetFbo, const ref<Camera> pCamera)
{
    FALCOR_PROFILE(pRenderContext, "BillboardGroup::rasterize");

    mpState->setFbo(pTargetFbo);
    mpState->setVao(mpVao);
    
    setPerFrameVars(pTargetFbo, pCamera);
    updateInstances(pRenderContext);

    pRenderContext->drawIndexedInstanced(mpState.get(), mpVars.get(), 6, mpInstanceBuffer->getElementCount(), 0, 0, 0);
}

BillboardGroup::BillboardGroup(RenderContext* pRenderContext, ref<Device> pDevice, Desc desc) : mDesc(desc)
{
    ProgramDesc programDesc;
    programDesc.addShaderLibrary("Samples/AnitoPlume/BillboardGroup.3d.slang").vsEntry("vsMain").psEntry("psMain");
    mpProgram = Program::create(pDevice, programDesc);
    mpVars = ProgramVars::create(pDevice, mpProgram->getReflector());

    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(TextureFilteringMode::Linear, TextureFilteringMode::Linear, TextureFilteringMode::Linear);
    samplerDesc.setAddressingMode(TextureAddressingMode::Clamp, TextureAddressingMode::Clamp, TextureAddressingMode::Clamp);
    mpSampler = pDevice->createSampler(samplerDesc);

    RasterizerState::Desc rsDesc;
    rsDesc.setCullMode(RasterizerState::CullMode::None);

    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthEnabled(true);
    dsDesc.setDepthWriteMask(false); // Disable so transparent billboards don't write to depth buffer

    BlendState::Desc blendDesc;
    blendDesc.setRtBlend(0, true);
    //blendDesc.setAlphaToCoverage(true); //MSAA must be enabled for this to work
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

    mInstances.resize(mDesc.maxCount);
    for (uint32_t i = 0; i < mDesc.maxCount; i++)
        mInstances[i] = {{0.f, 0.f, 0.f}, i, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}};

    // Allocate GPU instance buffer (StructuredBuffer<BillboardInstance>)
    mpInstanceBuffer = pDevice->createStructuredBuffer(
        sizeof(BillboardInstance),
        mInstances.size(),
        ResourceBindFlags::ShaderResource,
        MemoryType::DeviceLocal,
        mInstances.data(),
        false
    );

    createQuadMesh(pDevice);
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
        sizeof(mDesc.kQuadVerts),
        ResourceBindFlags::Vertex,
        MemoryType::DeviceLocal,
        mDesc.kQuadVerts
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

void BillboardGroup::loadTextures(RenderContext* pRenderContext, ref<Device> pDevice, const std::vector<std::string>& paths)
{
    // Load each slice as a plain Texture2D first
    std::vector<ref<Texture>> staging;
    AssetResolver resolver = AssetResolver::getDefaultResolver();
    for (auto& p : paths)
    {
        std::filesystem::path resolvedPath = resolver.resolvePath(p);
        staging.push_back(Texture::createFromFile(pDevice, resolvedPath, true, true));
    }

    uint32_t w = staging[0]->getWidth();
    uint32_t h = staging[0]->getHeight();
    uint32_t mips = staging[0]->getMipCount();
    uint32_t slices = (uint32_t)staging.size();
    ResourceFormat fmt = staging[0]->getFormat();

    mpTexArray = pDevice->createTexture2D(w, h, fmt, mips, slices, nullptr, ResourceBindFlags::ShaderResource);

    // Blit each loaded Texture2D into the correct array slice
    for (uint32_t slice = 0; slice < slices; slice++)
    {
        for (uint32_t mip = 0; mip < mips; mip++)
        {
            pRenderContext->copySubresource(
                mpTexArray.get(),
                mpTexArray->getSubresourceIndex(slice, mip),
                staging[slice].get(),
                staging[slice]->getSubresourceIndex(0, mip)
            );
        }
    }
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
    var["gTexArray"] = mpTexArray;
    var["gSampler"] = mpSampler;
    var["BillboardCB"]["gCameraPosition"] = pCamera->getPosition();
    var["BillboardCB"]["gCameraRight"] = cameraRight;
    var["BillboardCB"]["gCameraUp"] = cameraUp;
    var["BillboardCB"]["gViewProj"] = pCamera->getViewProjMatrix();
    var["BillboardCB"]["gInstanceCount"] = (uint32_t)mInstances.size();
    var["BillboardCB"]["gMinAlphaDistance"] = mDesc.minAlphaDistance;
    var["BillboardCB"]["gMaxAlphaDistance"] = mDesc.maxAlphaDistance;
}

void BillboardGroup::setInstance(uint32_t index, float3 worldPos, uint32_t texId, float2 size, float4 color)
{
    FALCOR_ASSERT(index < (uint32_t)mInstances.size());
    mInstances[index] = {worldPos, texId, size, color};
    mUpdateInstances = true;
}

void BillboardGroup::updateInstances(RenderContext* pRenderContext)
{
    if (!mUpdateInstances || mInstances.size() == 0) return;
    mpInstanceBuffer->setBlob(mInstances.data(), 0, mInstances.size() * sizeof(BillboardInstance));
    mUpdateInstances = false;
}
