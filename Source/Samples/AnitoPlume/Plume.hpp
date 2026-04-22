#pragma once
#include "SmokeLayer.h"
#include "TerrainStructure.h"
#include "WindStructure.h"
#include <vector>
#include "Falcor.h"
using namespace Falcor::math;
#define PI 3.14159265358979323846f
struct EruptionParams
{
    float U_0; // initial speed
    float z_0; // initial altitude
    float r_0; // initial radius
    float rho_0; // initial density
    float maxRadius;
};

class Plume
{
private:
    unsigned int mId;
    std::string mVentName;
    // User-defined parameters
    float mT0; // initial temp (unused)
    float mTheta0; // initial angle (unused)
    float mU0; // initial speed
    float mN0; // initial gas mass fraction (unused)
    float mZ0; // initial altitude
    float mR0; // initial radius
    float mRho0; // initial density
    float mExpansionStrength;
    float mRadiusMultiplier;

    // Trackers
    float mTStep;
    float mNewLayerDelay;
    float mMaxRadius;
    float mAirIncorporationCoeff;
    float mStagnationSpeed;

    unsigned int mNbOfIterations;
    unsigned int mLastPpeLayerIdx;

    unsigned short mFreeSphereID;
    unsigned short mFallingSphereID;

    // smoke transition animation
    int mMaxSmoke;
    float mTransitionSpeed;
    float mTransitionDelay;

    const unsigned int mNbSpheres = 6;

public:
    // Constants
    float mG;
    float mMinLifetime;
    float mMaxLifetime;
    float3 mVentPosition;
	EruptionParams mEruptParams;

    // Data structures
    std::vector<smoke_layer> mSmokeLayers;
    std::vector<free_sphere_params> mFreeSpheres;
    std::vector<subsphere_params> mS2Spheres;
    std::vector<subsphere_params> mS3Spheres;
    std::vector<free_sphere_params> mStagnateSpheres;
    std::vector<free_sphere_params> mFallingSpheres;
    std::vector< std::vector<free_sphere_params>> mFallingSpheresBuffers;

    std::vector<float> mSphereLifetime;
    std::vector<float> mTransitionLifetime;

    unsigned int mSubspheresNumber;
    unsigned int mSubSubspheresNumber;

public:
    Plume(unsigned int id, std::string vent_name, float3 vent_position, EruptionParams eruptParams);
    void reset();
	void reset_parameters();

    void set_t_step(float t_step);

    void remove_colliding_smoke();
    void remove_smoke_layers();

    float3 getPosition();
    float get_T_0();
    float get_theta_0();
    float get_U_0();
    float get_n_0();
    float get_z_0();
    float get_r_0();
    float get_rho_0();
    float getMaxRadius();
    unsigned int getVEI();
    void set_U_0(float U_0);
    void set_rho_0(float rho_0);
    void set_r_0(float r_0);
    void set_z_0(float z_0);
    void setVEI(unsigned int vei);

    unsigned int getID();
    std::string getVentName();
    int getMaxSmoke();
    float getVolume();
    float getTransitionSpeed();
    float getTransitionDelay();
    std::vector<float>& getTransitionLifetime();
    void setMaxSmoke(int max_smoke);
    void setTransitionSpeed(float transition_speed);
    void setTranstionDelay(float transition_delay);

    // VEI computations
    float computeMER(float frag_factor, float scaling_coeff);
    float computeVEI();

private:
    // Smoke layer computation
    void add_smoke_layer(float v, float d, float r, float3 position, bool secondary_plume);
    void edit_smoke_layer_properties(unsigned int i, float& d_mass, float3 wind);
    void apply_forces_to_smoke_layer(unsigned int i, float d_mass, float3 wind);
    void sedimentation(unsigned int i, float& d_mass);
    void pyroclastic_flow_computation_step(unsigned int i);
    void complete_plume_layer_properties_update(unsigned int i);

    float compute_gaussian_speed_in_layer(float v_z, float max_r, float r);
    float compute_atm_temperature(float height);
    float compute_atm_density(float height);

    // Pyroclastic flow : falling spheres
    void sphere_ground_collision(free_sphere_params& sphere, float terrain_z, float3 terrain_normal, int idx, unsigned int frame_nb);
    void ground_falling_sphere_update(TerrainStructure& terrain_struct, free_sphere_params& sphere, int idx, unsigned int frame_nb);
    void secondary_columns_creation();

    // Free spheres
    void add_free_sphere(unsigned int i, float angle, float size_fac);
    void add_free_spheres_for_one_layer(unsigned int i);
    void subdivide_and_make_falling(unsigned int i);

    public:
    // Pyroclastic flow : falling spheres
    void falling_spheres_update(TerrainStructure& terrain_struct, unsigned int frame_nb);

    // Free spheres
    void update_free_spheres();

    // Stagnation
    void update_stagnation_spheres(float3 wind);
    void update_stagnation_spheres_position();

    void smoke_layer_update(unsigned int i, float3 wind);
    void update_smoke_layer_init();
};
