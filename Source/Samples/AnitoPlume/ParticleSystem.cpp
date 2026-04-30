#include "ParticleSystem.h"

ref<ParticleSystem> ParticleSystem::create(ref<Device> pDevice)
{
    return ref<ParticleSystem>(new ParticleSystem(pDevice));
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
    float3 camRight = {view[0][0], view[1][0], view[2][0]};
    float3 camUp = {view[0][1], view[1][1], view[2][1]};

    // Bind buffers and constant data via the RasterPass root var
    ShaderVar vars = mpBillboardPass->getRootVar();
    vars["gParticles"] = mpParticleBuffer;
    vars["gAliveList"] = mpAliveList;

    vars["BillboardCB"]["gViewProj"] = pCamera->getViewProjMatrix();
    vars["BillboardCB"]["gCameraRight"] = camRight;
    vars["BillboardCB"]["gCameraUp"] = camUp;
    // vars["BillboardCB"]["gCameraPosition"] = pCamera->getPosition();

    mpBillboardPass->getState()->setFbo(pTargetFbo);

    pRenderContext->drawInstanced(mpBillboardPass->getState().get(), mpBillboardPass->getVars().get(), 4, aliveCount, 0, 0);
}

ParticleSystem::ParticleSystem(ref<Device> pDevice) : mpDevice(pDevice)
{
    initBuffers();
    initComputePasses();
    initBillboardPass();
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
        ResourceBindFlags::None, MemoryType::ReadBack
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

    mpBillboardPass = RasterPass::create(mpDevice, billboardDesc);

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
    dsDesc.setDepthWriteMask(false);

    // ── Rasterizer: no backface culling ───────────────────────────────────
    RasterizerState::Desc rsDesc;
    rsDesc.setCullMode(RasterizerState::CullMode::None);

    // ── Topology: triangle strip, positions built in the vertex shader ─────
    // RasterPass exposes its internal GraphicsState directly for cases like
    // this where the topology or blend state need to differ from the default.
    mpBillboardPass->getState()->setBlendState(BlendState::create(blendDesc));
    mpBillboardPass->getState()->setDepthStencilState(DepthStencilState::create(dsDesc));
    mpBillboardPass->getState()->setRasterizerState(RasterizerState::create(rsDesc));
    mpBillboardPass->getState()->setVao(Vao::create(Vao::Topology::TriangleStrip));
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
}

// Reads the alive count back to the CPU via a staging buffer.
// Causes a GPU flush — replace with drawIndirect to eliminate the stall.
uint32_t ParticleSystem::readAliveCount(RenderContext* pRenderContext)
{
    pRenderContext->resourceBarrier(mpCounters.get(), Resource::State::CopySource);

    pRenderContext->copyBufferRegion(mpStagingBuffer.get(), 0, mpCounters.get(), 0, sizeof(uint32_t) * 2);

    pRenderContext->resourceBarrier(mpCounters.get(), Resource::State::UnorderedAccess);
    pRenderContext->submit(true);

    const uint32_t* counters = static_cast<const uint32_t*>(mpStagingBuffer->map());
    uint32_t deadCount = counters[0];
    uint32_t aliveCount = counters[1];
    mpStagingBuffer->unmap();

    logInfo("ParticleSystem: dead={} alive={}", deadCount, aliveCount);

    return aliveCount;
}
