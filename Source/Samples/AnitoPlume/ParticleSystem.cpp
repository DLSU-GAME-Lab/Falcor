#include "ParticleSystem.h"
#include "Core/AssetResolver.h" 

ref<ParticleSystem> ParticleSystem::create(ref<Device> pDevice)
{
    return ref<ParticleSystem>(new ParticleSystem(pDevice));
}

ParticleSystem::ParticleSystem(ref<Device> pDevice) : mpDevice(pDevice)
{
    initBuffers();
    initComputePasses();
    initBillboardPass();
}


// Call every frame — dispatches emit + update compute passes.
void ParticleSystem::simulate(RenderContext* pRenderContext, float deltaTime)
{
    if (deltaTime <= 0.f)
        return;

    mFrameSeed++;

    // Reset aliveCount on GPU only — deadCount is GPU-managed
    mpResetPass->getRootVar()["gCounters"] = mpCounters;
    mpResetPass->execute(pRenderContext, 1, 1, 1);
    pRenderContext->uavBarrier(mpCounters.get());

    bindComputeResources(mpEmitPass->getRootVar(), deltaTime);
    bindComputeResources(mpUpdatePass->getRootVar(), deltaTime);

    // ── Emit ──────────────────────────────────────────────────────────────
    mpEmitPass->execute(pRenderContext, divUp(mEmitPerFrame, 64u), 1, 1);
    pRenderContext->uavBarrier(mpParticleBuffer.get());
    pRenderContext->uavBarrier(mpDeadList.get());
    pRenderContext->uavBarrier(mpCounters.get());

    // ── Update ────────────────────────────────────────────────────────────
    mpUpdatePass->execute(pRenderContext, divUp(kMaxParticles, 64u), 1, 1);
    pRenderContext->uavBarrier(mpAliveList.get());
    pRenderContext->uavBarrier(mpParticleBuffer.get());
    pRenderContext->uavBarrier(mpCounters.get());
}

// Call every frame after simulate() — composites billboards onto pTargetFbo.
void ParticleSystem::render(RenderContext* pRenderContext, const ref<Fbo> pTargetFbo, const ref<Camera> pCamera)
{
    // CPU readback of alive count.
    // Replace with drawIndirect to avoid the GPU flush once stable.
    uint32_t aliveCount = readAliveCount(pRenderContext);
    if (aliveCount == 0)
        return;

    // Camera basis vectors for axis-aligned billboarding
    float4x4 view = pCamera->getViewMatrix();
    //float3 camRight = {view[0][0], view[1][0], view[2][0]};
    //float3 camUp    = {view[0][1], view[1][1], view[2][1]};
    float3 camRight = float3(view[0][0], view[0][1], view[0][2]);
    float3 camUp = float3(view[1][0], view[1][1], view[1][2]);

    // Bind buffers and constant data via the RasterPass root var
   // ShaderVar vars = mpBillboardPass->getRootVar();
    auto var = mpBillboardVars->getRootVar();
    var["gParticles"] = mpParticleBuffer;
    var["gAliveList"] = mpAliveList;
    var["gTexArray"] = mpParticleTexture;
    var["gSampler"] = mpSampler;

    var["BillboardCB"]["gViewProj"] = pCamera->getViewProjMatrix();
    var["BillboardCB"]["gCameraRight"] = camRight;
    var["BillboardCB"]["gCameraUp"] = camUp;
    // vars["BillboardCB"]["gCameraPosition"] = pCamera->getPosition();

   // mpBillboardPass->getState()->setFbo(pTargetFbo);
    mpBillboardState->setFbo(pTargetFbo);
    mpBillboardState->setVao(mpQuadVao);

   // pRenderContext->drawInstanced(mpBillboardPass->getState().get(), mpBillboardPass->getVars().get(), 4, aliveCount, 0, 0);
    pRenderContext->drawIndexedInstanced(
        mpBillboardState.get(),
        mpBillboardVars.get(),
        6, aliveCount, 0, 0, 0
    );
}


void ParticleSystem::loadTexture(RenderContext* pRenderContext, const std::string& path)
{
    AssetResolver resolver = AssetResolver::getDefaultResolver();
    std::filesystem::path resolved = resolver.resolvePath(path);
    ref<Texture> src = Texture::createFromFile(mpDevice, resolved, true, true);
    if (!src)
    {
        logWarning("ParticleSystem::loadTexture — failed to load '{}'", path);
        return;
    }

    uint32_t w = src->getWidth();
    uint32_t h = src->getHeight();
    uint32_t mips = src->getMipCount();

    mpParticleTexture = mpDevice->createTexture2D(w, h, src->getFormat(), mips, 1, nullptr, ResourceBindFlags::ShaderResource);

    for (uint32_t mip = 0; mip < mips; mip++)
    {
        pRenderContext->copySubresource(
            mpParticleTexture.get(), mpParticleTexture->getSubresourceIndex(0, mip), src.get(), src->getSubresourceIndex(0, mip)
        );
    }

    logInfo("ParticleSystem::loadTexture — loaded '{}' ({}x{})", path, w, h);
}


void ParticleSystem::initBuffers()
{
    // Particle data store
    mpParticleBuffer = mpDevice->createStructuredBuffer(
        sizeof(Particle),
        kMaxParticles,
        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
        MemoryType::DeviceLocal,
        nullptr,
        false
    );

    // Dead list — pre-fill with all indices (every slot free at start)
    std::vector<uint32_t> deadIndices(kMaxParticles);
    std::iota(deadIndices.begin(), deadIndices.end(), 0);
    mpDeadList = mpDevice->createStructuredBuffer(
        sizeof(uint32_t),
        kMaxParticles,
        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
        MemoryType::DeviceLocal,
        deadIndices.data(),
        false
    );

    // Alive list — compacted each frame by the update pass
    mpAliveList = mpDevice->createStructuredBuffer(
        sizeof(uint32_t),
        kMaxParticles,
        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
        MemoryType::DeviceLocal,
        nullptr,
        false
    );

    // Counter buffer: [0] = deadCount (full), [1] = aliveCount (zero)
    uint32_t initCounters[2] = {kMaxParticles, 0};
    mpCounters = mpDevice->createBuffer(
        kCounterBytes,
        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
        MemoryType::DeviceLocal,
        initCounters
    );

    // 8 bytes to read both deadCount and aliveCount at once
    mpStagingBuffer = mpDevice->createBuffer(
        sizeof(uint32_t) * 2,
        ResourceBindFlags::None,
        MemoryType::ReadBack
    );
}

void ParticleSystem::initComputePasses()
{
    mpResetPass = ComputePass::create(mpDevice, "Samples/AnitoPlume/Particles.cs.slang", "resetCounters");
    mpEmitPass = ComputePass::create(mpDevice, "Samples/AnitoPlume/Particles.cs.slang", "emitParticles");
    mpUpdatePass = ComputePass::create(mpDevice, "Samples/AnitoPlume/Particles.cs.slang", "updateParticles");
}

void ParticleSystem::initBillboardPass()
{
    // RasterPass takes a Program::Desc the same way HelloDXR constructs its
    // own raster passes — no manual GraphicsState or GraphicsVars needed.
    ProgramDesc billboardDesc;
    // billboardDesc.addShaderModules(shaderModules);
    billboardDesc.addShaderLibrary("Samples/AnitoPlume/ParticleBillboard.3d.slang").vsEntry("vsMain").psEntry("psMain");
    // billboardDesc.addTypeConformances(typeConformances);

    mpBillboardProgram = Program::create(mpDevice, billboardDesc);
    mpBillboardVars = ProgramVars::create(mpDevice, mpBillboardProgram->getReflector());
    //mpBillboardPass = RasterPass::create(mpDevice, billboardDesc);

    // Sampler — same as BillboardGroup
    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(
        TextureFilteringMode::Linear,
        TextureFilteringMode::Linear,
        TextureFilteringMode::Linear
    );
    samplerDesc.setAddressingMode(
        TextureAddressingMode::Clamp,
        TextureAddressingMode::Clamp,
        TextureAddressingMode::Clamp
    );
    mpSampler = mpDevice->createSampler(samplerDesc);

    // ── Blend: standard src-alpha over ────────────────────────────────────
    BlendState::Desc blendDesc;
    blendDesc.setRtBlend(0, true).setRtParams(
        0,
        BlendState::BlendOp::Add,
        BlendState::BlendOp::Add,
        BlendState::BlendFunc::SrcAlpha,
        BlendState::BlendFunc::OneMinusSrcAlpha,
        BlendState::BlendFunc::One,
        BlendState::BlendFunc::OneMinusSrcAlpha
    );

    // ── Depth: test against scene depth, do not write ─────────────────────
    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthEnabled(false);
    dsDesc.setDepthWriteMask(false);

    // ── Rasterizer: no backface culling ───────────────────────────────────
    RasterizerState::Desc rsDesc;
    rsDesc.setCullMode(RasterizerState::CullMode::None);

    // GraphicsState
    mpBillboardState = GraphicsState::create(mpDevice);
    mpBillboardState->setProgram(mpBillboardProgram);
    mpBillboardState->setBlendState(BlendState::create(blendDesc));
    mpBillboardState->setDepthStencilState(DepthStencilState::create(dsDesc));
    mpBillboardState->setRasterizerState(RasterizerState::create(rsDesc));

    // Quad VAO — identical to BillboardGroup::createQuadMesh()
    struct QuadVertex
    {
        float2 position;
        float2 uv;
    };
    QuadVertex verts[4] = {
        {{-0.5f, 0.5f}, {0.f, 0.f}},  // TL
        {{0.5f, 0.5f}, {1.f, 0.f}},   // TR
        {{0.5f, -0.5f}, {1.f, 1.f}},  // BR
        {{-0.5f, -0.5f}, {0.f, 1.f}}, // BL
    };
    uint16_t indices[6] = {0, 1, 2, 0, 2, 3}; // CCW two triangles

    ref<VertexLayout> pLayout = VertexLayout::create();
    ref<VertexBufferLayout> pBufLayout = VertexBufferLayout::create();
    pBufLayout->addElement("POSITION", 0, ResourceFormat::RG32Float, 1, 0);
    pBufLayout->addElement("TEXCOORD", sizeof(float2), ResourceFormat::RG32Float, 1, 1);
    pLayout->addBufferLayout(0, pBufLayout);

    mpQuadVB = mpDevice->createBuffer(sizeof(verts), ResourceBindFlags::Vertex, MemoryType::DeviceLocal, verts);
    mpQuadIB = mpDevice->createBuffer(sizeof(indices), ResourceBindFlags::Index, MemoryType::DeviceLocal, indices);

    Vao::BufferVec vbufs = {mpQuadVB};
    mpQuadVao = Vao::create(Vao::Topology::TriangleList, pLayout, vbufs, mpQuadIB, ResourceFormat::R16Uint);
    mpBillboardState->setVao(mpQuadVao);

    // Default 1x1 white texture — replace with loadTexture() for smoke look
    uint32_t white = 0xFFFFFFFF;
    mpParticleTexture = mpDevice->createTexture2D(1, 1, ResourceFormat::RGBA8UnormSrgb, 1, 1, &white, ResourceBindFlags::ShaderResource);
}

// =========================================================================
// Per-frame helpers
// =========================================================================

// Binds all buffers and the constant buffer onto a compute pass root var.
void ParticleSystem::bindComputeResources(ShaderVar vars, float deltaTime)
{
    vars["gParticles"] = mpParticleBuffer;
    vars["gDeadList"] = mpDeadList;
    vars["gAliveList"] = mpAliveList;
    vars["gCounters"] = mpCounters;

    vars["PerFrameCB"]["gEmitterPos"] = mEmitterPos;
    vars["PerFrameCB"]["gDeltaTime"] = deltaTime;
    vars["PerFrameCB"]["gEmitDirection"] = mEmitDirection;
    vars["PerFrameCB"]["gEmitSpeed"] = mEmitSpeed;
    vars["PerFrameCB"]["gGravity"] = mGravity;
    vars["PerFrameCB"]["gSpreadAngle"] = mSpreadAngle;
    vars["PerFrameCB"]["gStartColor"] = mStartColor;
    vars["PerFrameCB"]["gEndColor"] = mEndColor;
    vars["PerFrameCB"]["gMinLifetime"] = mMinLifetime;
    vars["PerFrameCB"]["gMaxLifetime"] = mMaxLifetime;
    vars["PerFrameCB"]["gMinSize"] = mMinSize;
    vars["PerFrameCB"]["gMaxSize"] = mMaxSize;
    vars["PerFrameCB"]["gEmitCount"] = mEmitPerFrame;
    vars["PerFrameCB"]["gMaxParticles"] = kMaxParticles;
    vars["PerFrameCB"]["gFrameSeed"] = mFrameSeed;
    vars["PerFrameCB"]["gSpawnRadius"] = mSpawnRadius;
    vars["PerFrameCB"]["gWindVelocity"] = mWindVelocity;
}

// Reads the alive count back to the CPU via a staging buffer.
// Causes a GPU flush — replace with drawIndirect to eliminate the stall.
//uint32_t ParticleSystem::readAliveCount(RenderContext* pRenderContext)
//{
//    pRenderContext->resourceBarrier(mpCounters.get(), Resource::State::CopySource);
//
//    pRenderContext->copyBufferRegion(
//        mpStagingBuffer.get(), 0,
//        mpCounters.get(), 0,
//        sizeof(uint32_t) * 2
//    );
//
//    pRenderContext->resourceBarrier(mpCounters.get(), Resource::State::UnorderedAccess);
//    pRenderContext->submit(true);
//
//    const uint32_t* counters = static_cast<const uint32_t*>(mpStagingBuffer->map());
//    uint32_t deadCount = counters[0];
//    uint32_t aliveCount = counters[1];
//    mpStagingBuffer->unmap();
//
//    logInfo("ParticleSystem: dead={} alive={}", deadCount, aliveCount);
//
//    return aliveCount;
//}


// readAliveCount() — one-frame-behind read, no GPU stall
uint32_t ParticleSystem::readAliveCount(RenderContext* pRenderContext)
{
    // read last frame's result
    const uint32_t* counters = static_cast<const uint32_t*>(mpStagingBuffer->map());
    mCachedAliveCount = counters[1]; // aliveCount at byte offset 4
    mpStagingBuffer->unmap();

    pRenderContext->resourceBarrier(mpCounters.get(), Resource::State::CopySource);
    pRenderContext->copyBufferRegion(mpStagingBuffer.get(), 0, mpCounters.get(), 0, sizeof(uint32_t) * 2);
    pRenderContext->resourceBarrier(mpCounters.get(), Resource::State::UnorderedAccess);
    // No submit(true)

    return mCachedAliveCount;
}
