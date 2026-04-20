#pragma once

#include "Falcor.h"
using namespace Falcor::math;


struct subsphere_params
{
    size_t parent_id;
    float3 relative_position;

    float3 center;
    float r;
    float size_ratio;
    float lifetime;

    subsphere_params()
    {
        lifetime = 0;
    }
};

struct free_sphere_params
{
    // characteristics
    unsigned short id;
    float3 center;
    float r;

    float3 speed;
    float mass;
    float rho;
    float relative_distance;
    float lifetime;

    // rotation informations
    float angular_speed;
    float3 rotation_axis; // cst
    float current_angle; // to keep track of self rotation
    float3 angle_vector;

    // for diversity
    float perturbation;
    float size_factor;

    // identify state of spheres
    bool falling_under_atm_rho;
    bool stagnate;
    bool stagnate_long;
    bool falling;
    bool falling_disappeared; // to make their disappearance more discrete
    bool secondary_column;

    // informations for stagnation
    int closest_layer_idx;
    float stagnation_altitude;
    float max_altitude;
    float xy_at_max_altitude;
    float3 center_at_max_altitude;
    float3 layer_center_at_max_altitude;


    free_sphere_params() {}
    // constructor used for spheres attached to newly emitted slice
    free_sphere_params(unsigned short sphere_id, float3 ring_center, float angle_on_ring, float r, float speed, bool sec_column=false) : id(sphere_id), r(r), speed({0,0,speed}), secondary_column(sec_column)
    {
        float3 angle_normal = {cos(angle_on_ring), sin(angle_on_ring),0};
        angle_vector = angle_normal;
        center = ring_center + r*angle_normal;
        rotation_axis = {-sin(angle_on_ring), cos(angle_on_ring),0};
        lifetime = 0;
        angular_speed = speed/r;
        current_angle = 0;
        perturbation = 0;
        stagnate = false;
        stagnate_long = false;
        falling = false;
        falling_disappeared = false;
    }
    // constructor used for falling spheres
    free_sphere_params(unsigned short sphere_id, float3 center, float r, float rho)
        : id(sphere_id)
        , center(center)
        , r(r)
        , speed({0, 0, 0})
        , mass(rho * 4. / 3. * 3.14 * r * r * r)
        , rho(rho)
        , angular_speed(0)
        , rotation_axis({1, 0, 0})
        , current_angle(0)
        , angle_vector({ 0,1,0 })
        , size_factor(1.)
        , falling_under_atm_rho(false)
        , stagnate(false)
        , stagnate_long(false)
        , falling(true)
        , falling_disappeared(false)
    {
        lifetime = 0;
    }
};

struct smoke_layer
{
    float3 center; // Position (x,y,z) (m)
    float3 v; // Speed (m.s-1)
    float3 a; // Acceleration (m.s-2)

    float3 plume_axis; // actually normalized speed
    float speed_along_axis; // (m.s-1)
    float theta; // angle of plume axis (rad)
    float3 theta_axis; // axis to compute plume_axis rotation with theta

    float r; // ray (m)
    float temperature; // (Kelvin)
    float rho; // density (kg.m-3)
    float thickness; // (m)
    float lifetime;

    // identify state of layers
    bool rising;
    bool begin_falling; // true for first frame of falling to allow closest spheres to fall
    bool falling;
    bool plume; // becomes true if rho becomes lower than atm
    bool stagnates; // becomes true if densities of layer and air become equal
    bool stagnates_long; // becomes true if stagnates and stops rising
    bool secondary_plume;


    smoke_layer()
        : center{0, 0, 0}
        , v{0, 0, 200}
        , a{0, 0, 0}
        , plume_axis{0, 0, 1}
        , speed_along_axis(200)
        , theta(asin(1.0))
        , theta_axis{1, 0, 0}
        ,r(100)
        , temperature(1273)
        , rho(1.5)
        , thickness(100)
        , rising(true)
        , begin_falling(false)
        , falling(false)
        , plume(false)
        , stagnates(false)
        , stagnates_long(false) {}
    // constructor for principal plume layers
    smoke_layer(float3 v)
        : center{0, 0, 0}
        , v(v)
        , a{0, 0, 0}
        , plume_axis(normalize(v))
        , speed_along_axis(length(v))
        , theta(asin(1.0))
        , r(100)
        , temperature(1273)
        , rho(1.5)
        , thickness(100)
        ,rising(true)
        , begin_falling(false)
        , falling(false)
        , plume(false)
        , stagnates(false)
        , stagnates_long(false)
    {
        if (length(v) == v.z)
            theta_axis = float3(1, 0, 0);
        else theta_axis = normalize(cross(v,float3(0,0,1)));
    }
    // constructor for secondary plume layers
    smoke_layer(float3 v, float rho, float r, float3 position, bool secondary_plume)
        : center(position)
        , v(v)
        , a{0, 0, 0}
        , plume_axis(normalize(v)) 
        , speed_along_axis(length(v))
        , theta(asin(1.0))         
        , r(r)
        , temperature(1273)
        , rho(rho)
        , thickness(r)
        , rising(true)
        , begin_falling(false)
        , falling(false)
        , plume(false)
        , stagnates(false)
        , stagnates_long(false)
        , secondary_plume(secondary_plume)
    {
        if (length(v) == v.z)
            theta_axis = float3(1, 0, 0);
        else
            theta_axis = normalize(cross(v, float3(0, 0, 1)));

        lifetime = 0;
    }
};



