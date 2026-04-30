// Particles.cs.slang
// Simple GPU particle system for Falcor (HelloDXR base)
// Two entry points: emitParticles (spawn) + updateParticles (simulate)

// ─── Particle data layout ────────────────────────────────────────────────────

struct Particle
{
    float3 position;    // World-space position
    float  age;         // Seconds alive (0 = just spawned)
    float3 velocity;    // World-space velocity (units/sec)
    float  lifetime;    // Total lifespan in seconds
    float4 color;       // RGBA, alpha fades with age
    float  size;        // Billboard radius in world units
    uint   flags;       // Bit 0 = alive, reserved for future use
    uint   _pad;        // keep struct 16-byte aligned
};

// ─── Constant buffer ─────────────────────────────────────────────────────────

cbuffer PerFrameCB
{
    float3 gEmitterPos;         // World-space spawn origin
    float  gDeltaTime;          // Seconds since last frame

    float3 gEmitDirection;      // Base velocity direction (normalised)
    float  gEmitSpeed;          // Base emit speed (units/sec)

    float3 gGravity;            // Gravity vector, e.g. (0, -9.8, 0)
    float  gSpreadAngle;        // Half-angle cone spread in radians

    float4 gStartColor;         // Color at birth
    float4 gEndColor;           // Color at death (linearly interpolated)

    float  gMinLifetime;        // Seconds
    float  gMaxLifetime;
    float  gMinSize;            // World units
    float  gMaxSize;

    uint   gEmitCount;          // Particles to spawn this dispatch
    uint   gMaxParticles;       // Size of the particle buffer
    uint   gFrameSeed;          // Changes every frame for RNG variety
    float  _pad0;
};

// ─── Resources ───────────────────────────────────────────────────────────────

RWStructuredBuffer<Particle> gParticles;
RWStructuredBuffer<uint>     gDeadList;     // Stack of free particle indices
RWStructuredBuffer<uint>     gAliveList;    // Compacted list built each frame
RWByteAddressBuffer          gCounters;     // uint[0]=deadCount  uint[1]=aliveCount

// ─── Utility: PCG hash-based RNG ─────────────────────────────────────────────
//  Cheap, GPU-friendly random float in [0, 1).

uint pcgHash(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float randFloat(inout uint seed)
{
    seed = pcgHash(seed);
    return float(seed) / 4294967296.0;  // 2^32
}

// Uniform random direction inside a cone around `axis`.
float3 randConeDir(inout uint seed, float3 axis, float halfAngle)
{
    float cosA = cos(halfAngle);
    float z    = lerp(cosA, 1.0, randFloat(seed));  // cos(theta) in [cosA, 1]
    float phi  = randFloat(seed) * 6.28318530718;   // azimuth in [0, 2π)
    float sinT = sqrt(saturate(1.0 - z * z));

    // Build a perpendicular frame around `axis`
    float3 up        = abs(axis.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent   = normalize(cross(up, axis));
    float3 bitangent = cross(axis, tangent);

    return normalize(tangent   * (sinT * cos(phi)) +
                     bitangent * (sinT * sin(phi)) +
                     axis      * z);
}

// ─── Emit pass ───────────────────────────────────────────────────────────────
// Spawns up to gEmitCount new particles per frame.
// Each thread pops one free slot from the dead stack and writes a particle.
// Does NOT touch gAliveList — updateParticles handles that next frame.

[numthreads(64, 1, 1)]
void emitParticles(uint3 dispatchID : SV_DispatchThreadID)
{
    uint emitIdx = dispatchID.x;
    if (emitIdx >= gEmitCount) return;

    // ── Truly atomic safe pop via InterlockedCompareExchange ──────────
    // Load + InterlockedAdd is NOT safe — two threads can both pass a
    // Load()==0 check before either decrements, causing underflow to 4B.
    // InterlockedCompareExchange only writes (actual-1) if the value is
    // still `actual` at the moment of the swap. If another thread changed
    // it first, we get the new value back in `expected` and retry.
    uint actual = gCounters.Load(0);
    uint slotIndex;

    [loop]
    while (true)
    {
        if (actual == 0) return;  // stack empty — nothing to emit

        uint expected;
        gCounters.InterlockedCompareExchange(0, actual, actual - 1, expected);

        if (expected == actual)
        {
            // Atomically decremented — top of stack is at actual-1
            slotIndex = actual - 1;
            break;
        }

        // Another thread changed the value before — retry with latest
        actual = expected;
    }

    if (slotIndex >= gMaxParticles) return;

    uint slot = gDeadList[slotIndex];

    // Seed unique to this particle + frame
    uint seed = pcgHash(emitIdx + gFrameSeed * 1234567u);

    Particle p;
    p.age      = 0.0;
    p.lifetime = lerp(gMinLifetime, gMaxLifetime, randFloat(seed));
    p.position = gEmitterPos;
    p.size     = lerp(gMinSize, gMaxSize, randFloat(seed));
    p.color    = gStartColor;
    p.flags    = 1u;    // alive
    p._pad     = 0u;

    float3 dir = randConeDir(seed, normalize(gEmitDirection), gSpreadAngle);
    p.velocity = dir * lerp(gEmitSpeed * 0.5, gEmitSpeed * 1.5, randFloat(seed));

    // Write particle data into the buffer.
    // Without this the slot is popped but never initialised — updateParticles
    // would see flags==0 and skip it immediately.
    gParticles[slot] = p;
}

// ─── Update pass ─────────────────────────────────────────────────────────────
// Runs over ALL kMaxParticles slots each frame.
// Live particles: age, apply physics, fade color, append to gAliveList.
// Expired particles: clear flags, push slot back onto dead stack.

[numthreads(64, 1, 1)]
void updateParticles(uint3 dispatchID : SV_DispatchThreadID)
{
    uint idx = dispatchID.x;
    if (idx >= gMaxParticles) return;

    Particle p = gParticles[idx];
    if ((p.flags & 1u) == 0u) return;  // slot is empty

    p.age += gDeltaTime;

    // ── Expire ────────────────────────────────────────────────────────
    if (p.age >= p.lifetime)
    {
        // Kill: push slot back onto the dead list
        p.flags = 0u;
        gParticles[idx] = p;

        // Push freed slot back onto the dead stack.
        // InterlockedAdd returns the value BEFORE increment — that is the
        // correct write index for the top of the stack.
        uint deadSlot;
        gCounters.InterlockedAdd(0, 1, deadSlot);
        if (deadSlot < gMaxParticles)
            gDeadList[deadSlot] = idx;

        return;
    }

    // ── Physics integration (symplectic Euler) ──
    p.velocity += gGravity * gDeltaTime;
    p.position += p.velocity * gDeltaTime;

    // ── Colour & size fade ──────────────────────
    float t    = saturate(p.age / p.lifetime);      // normalised age [0, 1]
    p.color    = lerp(gStartColor, gEndColor, t);
    p.color.a *= (1.0 - t);                         // linear alpha fade

    gParticles[idx] = p;

    // ── Append to alive list for the renderer ──
    // InterlockedAdd(4, ...) targets byte offset 4 = aliveCount (counter[1]).
    uint aliveSlot;
    gCounters.InterlockedAdd(4, 1, aliveSlot);      // counter at byte offset 4
    if (aliveSlot < gMaxParticles)
        gAliveList[aliveSlot] = idx;
}

// ─── Reset pass ──────────────────────────────────────────────────────────────
// Zeroes aliveCount (counter[1]) only. Runs once per frame before emit.
// Avoids CPU-side updateBuffer offset ambiguity entirely — runs on GPU.

[numthreads(1, 1, 1)]
void resetCounters(uint3 dispatchID : SV_DispatchThreadID)
{
    gCounters.Store(4, 0u);  // byte offset 4 = aliveCount only, deadCount untouched
}
