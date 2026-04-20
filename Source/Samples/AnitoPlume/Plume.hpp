#pragma once
#include "scenes/sources/smoke/smokeLayer.hpp"
#include "scenes/sources/smoke/terrain_structure.hpp"
#include "scenes/sources/smoke/wind_structure.hpp"
#include <vector>
struct EruptionParams
{
    double U_0; // initial speed
    double z_0; // initial altitude
    double r_0; // initial radius
    double rho_0; // initial density
    double maxRadius;
};

class Plume
{
private:
    unsigned int id;
    std::string vent_name;
    // User-defined parameters
    double T_0; // initial temp (unused)
    double theta_0; // initial angle (unused)
    double U_0; // initial speed
    double n_0; // initial gas mass fraction (unused)
    double z_0; // initial altitude
    double r_0; // initial radius
    double rho_0; // initial density
    double expansion_strength;
    double radius_multiplier;

    // Trackers
    float t_step;
    float new_layer_delay;
    float fMaxRadius;
    double air_incorporation_coeff;
    double stagnation_speed;

    unsigned int nb_of_iterations;
    unsigned int last_ppe_layer_idx;

    unsigned short free_sphere_id;
    unsigned short falling_sphere_id;

    // smoke transition animation
    int max_smoke;
    float transition_speed;
    float transition_delay;

    const unsigned int nb_spheres = 6;

public:
    // Constants
    float g;
    float min_lifetime;
    float max_lifetime;
    vcl::vec3 vent_position;
	EruptionParams erupt_params;

    // Data structures
    std::vector<smoke_layer> smoke_layers;
    std::vector<free_sphere_params> free_spheres;
    std::vector<subsphere_params> s2_spheres;
    std::vector<subsphere_params> s3_spheres;
    std::vector<free_sphere_params> stagnate_spheres;
    std::vector<free_sphere_params> falling_spheres;
    std::vector< std::vector<free_sphere_params>> falling_spheres_buffers;

    std::vector<float> sphere_lifetime;
    std::vector<float> transition_lifetime;

    unsigned int subspheres_number;
    unsigned int subsubspheres_number;

public:
    Plume(unsigned int id, std::string vent_name, vcl::vec3 vent_position, EruptionParams eruptParams);
    void reset();
	void reset_parameters();

    void set_t_step(float t_step);

    void remove_colliding_smoke();
    void remove_smoke_layers();

    vcl::vec3 getPosition();
    double get_T_0();
    double get_theta_0();
    double get_U_0();
    double get_n_0();
    double get_z_0();
    double get_r_0();
    double get_rho_0();
    double getMaxRadius();
    unsigned int getVEI();
    void set_U_0(double U_0);
    void set_rho_0(double rho_0);
    void set_r_0(double r_0);
    void set_z_0(double z_0);
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
    void add_smoke_layer(float v, float d, float r, vcl::vec3 position, bool secondary_plume);
    void edit_smoke_layer_properties(unsigned int i, float& d_mass, vcl::vec3 wind);
    void apply_forces_to_smoke_layer(unsigned int i, float d_mass, vcl::vec3 wind);
    void sedimentation(unsigned int i, float& d_mass);
    void pyroclastic_flow_computation_step(unsigned int i);
    void complete_plume_layer_properties_update(unsigned int i);

    float compute_gaussian_speed_in_layer(float v_z, float max_r, float r);
    float compute_atm_temperature(float height);
    float compute_atm_density(float height);

    // Pyroclastic flow : falling spheres
    void sphere_ground_collision(free_sphere_params& sphere, float terrain_z, vcl::vec3 terrain_normal, int idx, unsigned int frame_nb);
    void ground_falling_sphere_update(terrain_structure& terrain_struct, free_sphere_params& sphere, int idx, unsigned int frame_nb);
    void secondary_columns_creation();

    // Free spheres
    void add_free_sphere(unsigned int i, float angle, float size_fac);
    void add_free_spheres_for_one_layer(unsigned int i);
    void subdivide_and_make_falling(unsigned int i);

    public:
    // Pyroclastic flow : falling spheres
    void falling_spheres_update(terrain_structure& terrain_struct, unsigned int frame_nb);

    // Free spheres
    void update_free_spheres();

    // Stagnation
    void update_stagnation_spheres(vcl::vec3 wind);
    void update_stagnation_spheres_position();

    void smoke_layer_update(unsigned int i, vcl::vec3 wind);
    void update_smoke_layer_init();
};