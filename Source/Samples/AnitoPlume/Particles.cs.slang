// Particles.cs.slang
// GPU particle system for AnitoPlume / Falcor

// ─── Particle data layout ────────────────────────────────────────────────────

struct Particle
{
    float3 position;    // World-space position
    float  age;         // Seconds alive (0 = just spawned)
    float3 velocity;    // World-space velocity (units/sec)
    float  lifetime;    // Total lifespan in seconds
    float4 color;       // RGBA, alpha fades with age
    float  size;        // Billboard radius in world units
    uint   flags;       // bit 0 = alive
    float  rotation;    // radians
    uint   _pad;
};

// ─── Constant buffer ─────────────────────────────────────────────────────────

cbuffer PerFrameCB
{
    float3 gEmitterPos;     // World-space spawn origin
    float  gDeltaTime;      // Seconds since last frame

    float3 gEmitDirection;  // Base velocity direction (normalised)
    float  gEmitSpeed;      // Base emit speed (units/sec)

    float3 gGravity;        // Gravity vector, e.g. (0, -9.8, 0)
    float  gSpreadAngle;    // Half-angle cone spread in radians

    float4 gStartColor;
    float4 gEndColor;

    float  gMinLifetime;    // Seconds
    float  gMaxLifetime;
    float  gMinSize;        // World units
    float  gMaxSize;

    uint   gEmitCount;
    uint   gMaxParticles;
    uint   gFrameSeed;
    float  gSpawnRadius;   // radius of spawn disc at emitter
    float3 gWindVelocity;
    float  _pad1;
};

// ─── Resources ───────────────────────────────────────────────────────────────

RWStructuredBuffer<Particle> gParticles;
RWStructuredBuffer<uint>     gDeadList;     // Stack of free particle indices
RWStructuredBuffer<uint>     gAliveList;    // Compacted list built each frame
RWByteAddressBuffer          gCounters;     // uint[0]=deadCount  uint[1]=aliveCount

// ─── PCG hash RNG ────────────────────────────────────────────────────────────

uint pcgHash(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float randFloat(inout uint seed)
{
    seed = pcgHash(seed);
    return float(seed) / 4294967296.0;
}

float3 randConeDir(inout uint seed, float3 axis, float halfAngle)
{
    float cosA = cos(halfAngle);
    float z    = lerp(cosA, 1.0, randFloat(seed));
    float phi  = randFloat(seed) * 6.28318530718;
    float sinT = sqrt(saturate(1.0 - z * z));

    float3 up        = abs(axis.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent   = normalize(cross(up, axis));
    float3 bitangent = cross(axis, tangent);

    return normalize(tangent   * (sinT * cos(phi)) +
                     bitangent * (sinT * sin(phi)) +
                     axis      * z);
}

// ─── Reset pass ──────────────────────────────────────────────────────────────

[numthreads(1, 1, 1)]
void resetCounters(uint3 dispatchID : SV_DispatchThreadID)
{
    gCounters.Store(4, 0u);
}

// ─── Emit pass ───────────────────────────────────────────────────────────────

[numthreads(64, 1, 1)]
void emitParticles(uint3 dispatchID : SV_DispatchThreadID)
{
    uint emitIdx = dispatchID.x;
    if (emitIdx >= gEmitCount) return;

    // Truly atomic safe pop via InterlockedCompareExchange
    uint actual = gCounters.Load(0);
    uint slotIndex;

    [loop]
    while (true)
    {
        if (actual == 0) return;

        uint expected;
        gCounters.InterlockedCompareExchange(0, actual, actual - 1, expected);
        if (expected == actual) { slotIndex = actual - 1; break; }
        actual = expected;
    }

    if (slotIndex >= gMaxParticles) return;

    uint slot = gDeadList[slotIndex];
    uint seed = pcgHash(emitIdx + gFrameSeed * 1234567u);

    // ── Spawn within a random disc around the emitter ─────────────────────
    // Using sqrt() for uniform distribution across the disc area
    float spawnAngle  = randFloat(seed) * 6.28318530718;
    float spawnRadius = sqrt(randFloat(seed)) * gSpawnRadius;
    float3 spawnOffset = float3(
        cos(spawnAngle) * spawnRadius,
        0.0,
        sin(spawnAngle) * spawnRadius
    );

    Particle p;
    p.age      = 0.0;
    p.lifetime = lerp(gMinLifetime, gMaxLifetime, randFloat(seed));
    p.position = gEmitterPos + spawnOffset;  // spread across disc
    p.size     = lerp(gMinSize, gMaxSize, randFloat(seed));
    p.color    = gStartColor;
    p.flags    = 1u;
    p.rotation = randFloat(seed) * 6.28318530718;
    p._pad     = 0u;

    // Tight upward velocity with slight spread for natural smoke rise
    float3 dir = randConeDir(seed, normalize(gEmitDirection), gSpreadAngle);
    p.velocity = dir * lerp(gEmitSpeed * 0.9, gEmitSpeed * 1.1, randFloat(seed));

    gParticles[slot] = p;
}

// ─── Update pass ─────────────────────────────────────────────────────────────

[numthreads(64, 1, 1)]
void updateParticles(uint3 dispatchID : SV_DispatchThreadID)
{
    uint idx = dispatchID.x;
    if (idx >= gMaxParticles) return;

    Particle p = gParticles[idx];
    if ((p.flags & 1u) == 0u) return;

    p.age += gDeltaTime;

    // Expire
    if (p.age >= p.lifetime)
    {
        p.flags = 0u;
        gParticles[idx] = p;

        uint deadSlot;
        gCounters.InterlockedAdd(0, 1, deadSlot);
        if (deadSlot < gMaxParticles)
            gDeadList[deadSlot] = idx;

        return;
    }

    // Physics — very light gravity for smoke drift
    p.velocity += gGravity * gDeltaTime;
    p.velocity += gWindVelocity * gDeltaTime;
    p.position += p.velocity * gDeltaTime;

    // Slow rotation over time for billowing effect
    p.rotation += 0.3 * gDeltaTime;

    // Color / alpha fade
    float t    = saturate(p.age / p.lifetime);
    p.color    = lerp(gStartColor, gEndColor, t);
    p.color.a *= (1.0 - t);

    gParticles[idx] = p;

    // Append to alive list
    uint aliveSlot;
    gCounters.InterlockedAdd(4, 1, aliveSlot);
    if (aliveSlot < gMaxParticles)
        gAliveList[aliveSlot] = idx;
}
