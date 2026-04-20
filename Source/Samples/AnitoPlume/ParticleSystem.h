// ParticleSystem.h
// Drop-in particle system for a Falcor HelloDXR-based sample.
// Owns GPU buffers, compiles the compute passes, and drives each frame.

#pragma once
#include "Falcor.h"
#include "Core/Pass/RasterPass.h"

using namespace Falcor;

// ─── Constants ────────────────────────────────────────────────────────────────

static constexpr uint32_t kMaxParticles = 65536;
static constexpr uint32_t kCounterBytes = 8; // [0] = deadCount, [1] = aliveCount

// ─────────────────────────────────────────────────────────────────────────────

class ParticleSystem : Object
{
public:
    FALCOR_OBJECT(ParticleSystem);

    static ref<ParticleSystem> create(ref<Device> pDevice);

    // =========================================================================
    // Lifecycle
    // =========================================================================

    // Call every frame — dispatches emit + update compute passes.
    void simulate(RenderContext* pCtx, float deltaTime);
    // Call every frame after simulate() — composites billboards onto pTargetFbo.
    void render(RenderContext* pCtx, const ref<Fbo> pTargetFbo, const ref<Camera> pCamera);

private:
    ParticleSystem(ref<Device> pDevice);

    // =========================================================================
    // GPU-side particle layout — must mirror Particles.cs.slang exactly
    // =========================================================================

    struct Particle
    {
        float3 position;
        float age;
        float3 velocity;
        float lifetime;
        float4 color;
        float size;
        uint32_t flags;
        float _pad[2]; // 16-byte alignment -> 64 bytes total
    };
    static_assert(sizeof(Particle) == 64, "Particle layout mismatch with shader");

    // =========================================================================
    // Init helpers — each called exactly once from init()
    // =========================================================================

    void initBuffers();
    void initComputePasses();
    void initBillboardPass();

    // =========================================================================
    // Per-frame helpers
    // =========================================================================

    // Binds all buffers and the constant buffer onto a compute pass root var.
    void bindComputeResources(ShaderVar vars, float deltaTime);

    // Reads the alive count back to the CPU via a staging buffer.
    // Causes a GPU flush — replace with drawIndirect to eliminate the stall.
    uint32_t readAliveCount(RenderContext* pCtx);

    // =========================================================================
    // Public tuning knobs — set any time before simulate()
    // =========================================================================

    float3 mEmitterPos = {0.f, 100.f, 0.f};
    float3 mEmitDirection = {0.f, 1.f, 0.f}; // normalised emit axis
    float3 mGravity = {0.f, -9.8f, 0.f};
    float4 mStartColor = {1.f, 0.6f, 0.1f, 1.f}; // orange, fully opaque
    float4 mEndColor = {0.3f, 0.3f, 0.3f, 0.f};  // grey, fully transparent
    float mEmitSpeed = 20.f;
    float mSpreadAngle = 0.3f; // half-angle cone in radians (~17 deg)
    float mMinLifetime = 1.5f; // seconds
    float mMaxLifetime = 3.5f;
    float mMinSize = 50.0f; // world units
    float mMaxSize = 100.0f;
    uint32_t mEmitPerFrame = 128;

    // =========================================================================
    // Members
    // =========================================================================

    ref<Device> mpDevice;

    // Compute passes
    ref<ComputePass> mpEmitPass;
    ref<ComputePass> mpUpdatePass;

    // GPU buffers
    ref<Buffer> mpParticleBuffer;
    ref<Buffer> mpDeadList;
    ref<Buffer> mpAliveList;
    ref<Buffer> mpCounters;

    // Billboard raster pass
    ref<RasterPass> mpBillboardPass;

    uint32_t mFrameSeed = 0u;

    static uint32_t divUp(uint32_t n, uint32_t d) { return (n + d - 1) / d; }
};
