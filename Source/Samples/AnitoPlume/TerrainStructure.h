#pragma once
#include "Falcor.h"
using namespace Falcor;
using namespace Falcor::math;
// Terrain grid for acceleration collision computation
struct TerrainStructure
{
    
    std::vector<float3> positions;
    std::vector<float3> normals;

   std::vector<std::vector<unsigned int> > grid;
    size_t grid_size;
   // use texture when switching to GPU
    std::vector<float> height_field;
    //ref<Vao> height_field_mesh;
    std::vector<float3> normal_field;
    float cell_size;
    size_t field_size;
    float min_xyz;
    float max_xyz;

    // Fill structures
    void fill_height_field(std::vector<float3>& position, std::vector<float3>& normal,
        Vao terrain, float4x4 transform);

    float field_height_at(float x, float y);
    float3 field_normal_at(float x, float y);
};
