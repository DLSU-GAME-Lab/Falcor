// Particles.cs.slang
// Simple GPU particle system for Falcor (HelloDXR base)
// Two entry points: emitParticles (spawn) + updateParticles (simulate)

// ─── Particle data layout ─────────────────────────────────────────────────────

struct Particle
{
    float3 position;   // World-space position
    float  age;        // Seconds alive (0 = just spawned)
    float3 velocity;   // World-space velocity (units/sec)
    float  lifetime;   // Total lifespan in seconds
    float4 color;      // RGBA, alpha fades with age
    float  size;       // Billboard radius in world units
    uint   flags;      // Bit 0 = alive, reserved for future use
};

// ─── Constant buffer ──────────────────────────────────────────────────────────

cbuffer PerFrameCB
{
    float3 gEmitterPos;       // World-space spawn origin
    float  gDeltaTime;        // Seconds since last frame

    float3 gEmitDirection;    // Base velocity direction (normalised)
    float  gEmitSpeed;        // Base emit speed (units/sec)

    float3 gGravity;          // Gravity vector, e.g. (0, -9.8, 0)
    float  gSpreadAngle;      // Half-angle cone spread in radians

    float4 gStartColor;       // Color at birth
    float4 gEndColor;         // Color at death (linearly interpolated)

    float  gMinLifetime;      // Seconds
    float  gMaxLifetime;
    float  gMinSize;          // World units
    float  gMaxSize;

    uint   gEmitCount;        // Particles to spawn this dispatch
    uint   gMaxParticles;     // Size of the particle buffer
    uint   gFrameSeed;        // Changes every frame for RNG variety
    float  _pad0;
};

// ─── Resources ────────────────────────────────────────────────────────────────

RWStructuredBuffer<Particle> gParticles;
RWStructuredBuffer<uint>     gDeadList;      // Stack of free particle indices
RWStructuredBuffer<uint>     gAliveList;     // Compacted list built each frame
RWByteAddressBuffer          gCounters;      // [0]=deadCount, [1]=aliveCount

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
    return float(seed) / 4294967296.0; // 2^32
}

// Uniform random direction inside a cone around `axis`.
float3 randConeDir(inout uint seed, float3 axis, float halfAngle)
{
    float cosA = cos(halfAngle);
    float z    = lerp(cosA, 1.0, randFloat(seed));  // cos(theta) in [cosA, 1]
    float phi  = randFloat(seed) * 6.28318530718;    // azimuth in [0, 2π)
    float sinT = sqrt(saturate(1.0 - z * z));

    // Build a perpendicular frame around `axis`
    float3 up   = abs(axis.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent   = normalize(cross(up, axis));
    float3 bitangent = cross(axis, tangent);

    return normalize(tangent * (sinT * cos(phi)) +
                     bitangent * (sinT * sin(phi)) +
                     axis * z);
}

// ─── Emit pass ────────────────────────────────────────────────────────────────
//  Dispatched once per frame: 1-D with ceil(gEmitCount / 64) thread groups.
//  Each thread pops a free slot from gDeadList and initialises a particle.

[numthreads(64, 1, 1)]
void emitParticles(uint3 dispatchID : SV_DispatchThreadID)
{
    uint emitIdx = dispatchID.x;
    if (emitIdx >= gEmitCount) return;

    // Atomically consume one slot from the dead list
    uint deadCount;
    gCounters.InterlockedAdd(0, uint(-1), deadCount); // decrement counter
    if (deadCount == 0)
    {
        // Nothing free — put the counter back
        gCounters.InterlockedAdd(0, 1, deadCount);
        return;
    }

    uint slot = gDeadList[deadCount - 1]; // top of stack

    // Seed unique to this particle + frame
    uint seed = pcgHash(emitIdx + gFrameSeed * 1234567u);

    Particle p;
    p.age      = 0.0;
    p.lifetime = lerp(gMinLifetime, gMaxLifetime, randFloat(seed));
    p.position = gEmitterPos;
    p.size     = lerp(gMinSize, gMaxSize, randFloat(seed));
    p.color    = gStartColor;
    p.flags    = 1u; // alive

    float3 dir = randConeDir(seed, normalize(gEmitDirection), gSpreadAngle);
    p.velocity = dir * lerp(gEmitSpeed * 0.5, gEmitSpeed * 1.5, randFloat(seed));

    gParticles[slot] = p;
}

// ─── Update pass ──────────────────────────────────────────────────────────────
//  Dispatched once per frame: 1-D with ceil(gMaxParticles / 64) groups.
//  Simulates live particles; recycles dead ones; compacts the alive list.

[numthreads(64, 1, 1)]
void updateParticles(uint3 dispatchID : SV_DispatchThreadID)
{
    uint idx = dispatchID.x;
    if (idx >= gMaxParticles) return;

    Particle p = gParticles[idx];
    if ((p.flags & 1u) == 0u) return; // slot is empty

    p.age += gDeltaTime;

    if (p.age >= p.lifetime)
    {
        // Kill: push slot back onto the dead list
        p.flags = 0u;
        gParticles[idx] = p;

        uint deadCount;
        gCounters.InterlockedAdd(0, 1, deadCount);
        gDeadList[deadCount] = idx;
        return;
    }

    // ── Physics integration (symplectic Euler) ──
    p.velocity += gGravity * gDeltaTime;
    p.position += p.velocity * gDeltaTime;

    // ── Colour & size fade ──────────────────────
    float t   = p.age / p.lifetime;           // normalised age [0, 1]
    p.color   = lerp(gStartColor, gEndColor, t);
    p.color.a *= (1.0 - t);                   // linear alpha fade

    gParticles[idx] = p;

    // ── Append to alive list for the renderer ──
    uint aliveSlot;
    gCounters.InterlockedAdd(4, 1, aliveSlot); // counter at byte offset 4
    gAliveList[aliveSlot] = idx;
}
