#include "TerrainStructure.h"


void TerrainStructure::fill_height_field(std::vector<float3>& position, std::vector<float3>& normal, Vao terrain, float4x4 transform)
{
    // prepare field with parameters
    min_xyz = -15000.0f;
    max_xyz = 15000.0f;
    float interval_size = max_xyz - min_xyz;
    cell_size = 200.f;
    field_size = (size_t)(max_xyz / cell_size);
    height_field.resize(field_size * field_size);
    normal_field.resize(field_size * field_size);
    float3 translate = {0,0,0};
    float3 rotation = {0, 0, 0};
    float3 scale = {1, 1, 1};
    // transform like for mesh_drawable
    for (unsigned int i = 0; i < position.size(); i++)
    {
           
        position[i] = scale * ( rotation * position[i] + translate);
        normal[i] = rotation * normal[i];
    }
    float4x4 a = float4x4::identity();
    float4 b = float4(1);
    float4 trans = mul(a, b);
    float3 result(trans.x, trans.y, trans.z);
    positions = position;
    normals = normal;

    // fill height field for collisions
    for (unsigned int i = 0; i < position.size(); i++)
    {
        if (position[i].x < max_xyz && position[i].x > min_xyz &&
            position[i].y < max_xyz && position[i].y > min_xyz)
        {
            int idx_x = (int)(field_size * (position[i].x - min_xyz) / interval_size);
            int idx_y = (int)(field_size * (position[i].y - min_xyz) / interval_size);
            if (idx_x == field_size)
                idx_x = field_size - 1;
            if (idx_y == field_size)
                idx_y = field_size - 1;
            size_t idx = idx_y * field_size + idx_x; // flatten 2D → 1D
            height_field[idx] = position[i].z;
            normal_field[idx] = normal[i];
        }
    }
}

float TerrainStructure::field_height_at(float x, float y)
{
    float interval_size = max_xyz - min_xyz;
    int idx_x = (int)(field_size * (x - min_xyz) / interval_size);
    int idx_y = (int)(field_size * (y - min_xyz) / interval_size);
    return height_field[idx_y * field_size + idx_x];
}

float3 TerrainStructure::field_normal_at(float x, float y)
{
    float interval_size = max_xyz - min_xyz;
    int idx_x = (int)(field_size * (x - min_xyz) / interval_size);
    int idx_y = (int)(field_size * (y - min_xyz) / interval_size);
    return normal_field[idx_y * field_size + idx_x];
}
