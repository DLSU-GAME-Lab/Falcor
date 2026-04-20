#include "Plume.hpp"

using namespace vcl;
//------------------------------------------------------------
//------------------------- ALGO -----------------------------
//------------------------------------------------------------ */

Plume::Plume(unsigned int id, std::string vent_name, vcl::vec3 vent_position, EruptionParams eruptParams)
{
    this->id = id;
    this->vent_name = vent_name;
    this->vent_position = vent_position;
    this->expansion_strength = 1;// for use in making the spread of pyroclastic plumes
    this->radius_multiplier = 2.f;// for use in the size of secondary columns
	erupt_params = eruptParams;
	reset_parameters();
    fMaxRadius = (float)erupt_params.maxRadius;

    subspheres_number = 0;
    subsubspheres_number = 0;

    // Parameters : constants
    g = 9.81; // (m.s-2)
    min_lifetime = 180.0;
    max_lifetime = 240.0;

    // coeff init
    air_incorporation_coeff = 5.;

    //transition
    max_smoke = 20;
    transition_speed = 5.0f;
    transition_delay = 0.2f;
    for (int i = 0; i < max_smoke; i++)
        transition_lifetime.push_back(transition_delay * i);

    reset();
}

void Plume::reset()
{
    free_sphere_id = 0;
    falling_sphere_id = 0;

    t_step = 0;
    new_layer_delay = 0;
    nb_of_iterations = 0;
    last_ppe_layer_idx = 0;
    stagnation_speed = 50;

    smoke_layers.clear();
    free_spheres.clear();
    s2_spheres.clear();
    s3_spheres.clear();
    falling_spheres.clear();
    stagnate_spheres.clear();
    falling_spheres_buffers.clear();

    transition_lifetime.clear();
    for (int i = 0; i < max_smoke; i++)
        transition_lifetime.push_back(transition_delay * i);
}

void Plume::reset_parameters()
{
    // Parameters : to be chosen by user
    T_0 = 1273.; // initial temp (K)
    theta_0 = 0.; // initial angle (rad)
    U_0 = erupt_params.U_0; // initial speed (m.s-1)
    n_0 = 0.03; // initial gas mass fraction
    z_0 = erupt_params.z_0; // initial altitude (m)
    r_0 = erupt_params.r_0; // initial radius (m)
    rho_0 = erupt_params.rho_0;
    vent_position.z = (float)z_0;
}

void Plume::set_t_step(float t_step)
{
    this->t_step = t_step;
    this->new_layer_delay += t_step;

    for (int i = 0; i < transition_lifetime.size(); i++)
    {
        transition_lifetime[i] += t_step;
    }
}

void Plume::add_smoke_layer(float v, float d, float r, vec3 position, bool secondary_plume)
{
    smoke_layer layer = smoke_layer({ 0,0,v }, d, r, position, secondary_plume);
    smoke_layers.push_back(layer);
}

//(K) cf https://fr.wikipedia.org/wiki/Atmosph%C3%A8re_normalis%C3%A9e English: https://en.wikipedia.org/wiki/International_Standard_Atmosphere
float Plume::compute_atm_temperature(float height)
{
    return 288.15 - 6.5 * height / 1000.0;
}

//(kg.m-3) cf https://www.deleze.name/marcel/sec2/applmaths/pression-altitude/masse_volumique.pdf
float Plume::compute_atm_density(float height)
{
    return 352.995 * pow(1 - 0.0000225577 * height, 5.25516) / (288.15 - 0.0065 * height);
}

float Plume::computeMER(float frag_factor, float scaling_coeff)
{
    double vent_area = PI * r_0 * r_0;
    return scaling_coeff * rho_0 * vent_area * U_0 * (1.0 + n_0) * frag_factor;
}

float Plume::computeVEI()
{
    double logMER = std::log10(computeMER(1.0f, 1.0f));
    return (logMER - 3.58) / 0.92;
}

void Plume::edit_smoke_layer_properties(unsigned int i, float& d_mass, vec3 wind)
{
    // preliminary computation
    float thk = t_step * smoke_layers[i].v.z;
    //thk = smoke_layers[i].v.z * 0.002;

    float total_smoke_thk = smoke_layers[i].thickness;
    float total_smoke_volume = total_smoke_thk * PI * smoke_layers[i].r * smoke_layers[i].r;
    float total_smoke_mass = smoke_layers[i].rho * total_smoke_volume;

    // wind velocity around for air incorporation
    float k_s = 0.09, k_w = 0.9;
    float U_e = k_s * abs(norm(smoke_layers[i].v) - norm(wind) * cos(smoke_layers[i].theta)) + k_w * abs(norm(wind) * sin(smoke_layers[i].theta));
    float r_atm = air_incorporation_coeff * U_e;
    //r_atm = U_e * t_step;

    // air quantity to put in the plume
    float atm_density = compute_atm_density(smoke_layers[i].center.z);
    float atm_volume = thk * PI * (2.0 * smoke_layers[i].r + r_atm) * r_atm; //air around the smoke layer
    float atm_mass = atm_density * atm_volume;

    // compute new temperature (we take all Cp equal)
    float new_temp = (total_smoke_mass * smoke_layers[i].temperature + atm_mass * compute_atm_temperature(smoke_layers[i].center.z)) / (total_smoke_mass + atm_mass);
    smoke_layers[i].temperature = new_temp;

    // new volume after heating
    float atm_new_volume = smoke_layers[i].temperature * atm_volume / compute_atm_temperature(smoke_layers[i].center.z); // after heating by hot smoke

    // new params
    d_mass = atm_mass;
    float mass_new = total_smoke_mass + atm_mass;
    float volume_new = total_smoke_volume + atm_new_volume;
    float rho_new = mass_new / volume_new;
    float r_new = cbrt(volume_new / PI);
    smoke_layers[i].thickness = r_new;

    // new speed due to conservation of energy (old)
    //float energy = 0.5 * total_smoke_mass * smoke_layers[i].v.z * smoke_layers[i].v.z;
    //float new_speed = sqrt(2.0 * energy / mass_new);
    //if (rho_new < atm_density) smoke_layers[i].v = {0,0,new_speed};

    if (smoke_layers[i].rho > atm_density && rho_new < atm_density) smoke_layers[i].plume = true;
    smoke_layers[i].r = r_new;
    smoke_layers[i].rho = rho_new;
}

void Plume::apply_forces_to_smoke_layer(unsigned int i, float d_mass, vec3 wind)
{
    // precomputation
    float atm_density = compute_atm_density(smoke_layers[i].center.z);
    float volume = smoke_layers[i].thickness * PI * smoke_layers[i].r * smoke_layers[i].r;
    float surface = 2 * PI * smoke_layers[i].r * smoke_layers[i].r;
    float surface_eff = 2 * smoke_layers[i].r * smoke_layers[i].r;
    float smoke_mass = smoke_layers[i].rho * volume;
    vec3 v_diff = wind - vec3(smoke_layers[i].v.x, smoke_layers[i].v.y, 0);

    vec3 weight = { 0,0, -smoke_mass * g }; // m*g
    vec3 archimede = { 0,0, atm_density * volume * g }; // rho*V*g
    vec3 friction = -0.5 * atm_density * 0.04 * surface * norm(smoke_layers[i].v) * smoke_layers[i].v; // axial friction
    //friction = - volume * 0.00005 * norm(smoke_layers[i].v) * smoke_layers[i].v; // old way to compute friction
    vec3 wind_force = 600. * norm(v_diff) * v_diff * surface_eff; // horizontal

    vec3 forces = weight + archimede + friction + wind_force;

    vec3 old_v = smoke_layers[i].v;
    float old_m = smoke_mass - d_mass;

    // equation of dynamics without mass conservation
    vec3 mv = old_m * old_v + forces * t_step;
    vec3 v = mv / smoke_mass;
    vec3 p = smoke_layers[i].center + v * t_step;

    // old with mass conservation
    //vec3 a = forces / (smoke_mass);
    //vec3 v = smoke_layers[i].v + a*t_step;
    //vec3 p = smoke_layers[i].center + v*t_step;


    // check if falls
    if (old_v.z > 0 && v.z < 0 && smoke_layers[i].plume == false)
    {
        smoke_layers[i].rising = false;
        smoke_layers[i].begin_falling = true;
    }

    // check if stagnates
    if (smoke_layers[i].plume && !smoke_layers[i].stagnates && smoke_layers[i].center.z > 5000 && (atm_density - smoke_layers[i].rho < 0.01))
    {
        smoke_layers[i].stagnates = true;
        stagnation_speed = smoke_layers[i].v.z;
    }

    // check if stagnates long
    if (smoke_layers[i].stagnates && v.z < 0 && !smoke_layers[i].stagnates_long)
    {
        smoke_layers[i].stagnates_long = true;
    }

    if (smoke_layers[i].stagnates && p.z < smoke_layers[i].center.z)
    {
        p.z = smoke_layers[i].center.z;
    }

    // update
    smoke_layers[i].a = v / t_step;
    smoke_layers[i].v = v;
    smoke_layers[i].center = p;
    if (p.z < -1000) smoke_layers[i].center.z = -1000; // prevent from going oob after falling (layers are not deleted but not used anymore)

    // compute new theta
    // WARNING: wind direction was constant in my tests, if the direction changes it may not work, it has to be tested
    //float dx = v.x*t_step, dy = v.y*t_step;
    float dz = v.z * t_step;
    smoke_layers[i].speed_along_axis = norm(v);
    smoke_layers[i].plume_axis = normalize(v);
    if (norm(wind) != 0 && t_step > 0)
    {
        smoke_layers[i].theta_axis = normalize(cross(wind, vec3(0, 0, 1)));
        float theta_totest = asin(dz / norm(v * t_step));
        if (dz / norm(v * t_step) < 1.000001 && dz / norm(v * t_step) > 0.999999) theta_totest = PI / 2.; // security to prevent nan values due to asin
        smoke_layers[i].theta = theta_totest;

        if (theta_totest < 0)
        {
            smoke_layers[i].theta = 0;
        }
    }
}

void Plume::sedimentation(unsigned int i, float& d_mass)
{
    // constant sedimentation
    float layer_volume = PI * smoke_layers[i].r * smoke_layers[i].r * smoke_layers[i].thickness;
    float diff_density = 0.00000005 * t_step;
    if (smoke_layers[i].stagnates_long) diff_density = 0.00005 * t_step;

    if (smoke_layers[i].rho > diff_density)
    {
        smoke_layers[i].rho -= diff_density;
        d_mass -= diff_density * layer_volume;
    }

}

void Plume::smoke_layer_update(unsigned int i, vcl::vec3 wind)
{
    float d_mass = 0; // to track mass change for equation of dynamics
    smoke_layers[i].lifetime = smoke_layers[i].lifetime + t_step;

    if (smoke_layers[i].plume == true && smoke_layers[i].center.z > 0.) sedimentation(i, d_mass); // sedimentation in altitude
    if (smoke_layers[i].rising && !smoke_layers[i].stagnates_long) edit_smoke_layer_properties(i, d_mass, wind); // convection if v_z > 0 (convection causes air entrainment)
    apply_forces_to_smoke_layer(i, d_mass, wind);
}

void Plume::update_smoke_layer_init()
{
    // add smoke layer each x seconds
    if (smoke_layers.size() == 0 || (new_layer_delay >= r_0 / (2 * U_0) && smoke_layers.size() < 1000000000000000))
    {
        add_smoke_layer(U_0, rho_0, r_0, vent_position, false);
        add_free_spheres_for_one_layer(smoke_layers.size() - 1);

        new_layer_delay = 0;
        std::cout << vent_name << " - LAYER ADDED OK" << std::endl;
        std::cout << vent_name << " - Total Layers: " << smoke_layers.size() << std::endl;
    }
}

void Plume::remove_colliding_smoke()
{
    if (!free_spheres.empty())
    {
        float ratio = 100.0f;
        float crater_r = 20.0f;
        float y_offset = -10.0f;
        for (int i = free_spheres.size() - 1; i >= 0; i--)
        {
            if (abs(free_spheres[i].center.x / ratio) > crater_r && free_spheres[i].center.y - (free_spheres[i].r / ratio) + y_offset < 0)
                free_spheres.erase(free_spheres.begin() + i);
        }
    }
}

void Plume::remove_smoke_layers()
{
    if (!smoke_layers.empty())
    {
        while (smoke_layers[0].lifetime > max_lifetime)
        {
            smoke_layers.erase(smoke_layers.begin());

            for (int i = 0; i < free_spheres.size(); i++)
            {
                free_spheres[i].closest_layer_idx--;
            }

            while (free_spheres[0].closest_layer_idx < 0)
            {
                free_spheres.erase(free_spheres.begin());
            }
        }
    }
}

vcl::vec3 Plume::getPosition()
{
    return this->vent_position;
}

double Plume::get_T_0()
{
    return this->T_0;
}

double Plume::get_theta_0()
{
    return this->theta_0;
}

double Plume::get_U_0()
{
    return this->U_0;
}

double Plume::get_n_0()
{
    return this->n_0;
}

double Plume::get_z_0()
{
    return this->z_0;
}

double Plume::get_r_0()
{
    return this->r_0;
}

double Plume::get_rho_0()
{
    return this->rho_0;
}

double Plume::getMaxRadius()
{
    return this->fMaxRadius;
}

unsigned int Plume::getVEI()
{
    float height = (U_0 * 50) / (rho_0 + r_0);
	//std::cout << "Height: " << height << std::endl;
    return vcl::clamp(unsigned int(height / 9), 1, 6);
}

void Plume::set_U_0(double U_0)
{
    this->U_0 = U_0;
}

void Plume::set_rho_0(double rho_0)
{
    this->rho_0 = rho_0;
}

void Plume::set_r_0(double r_0)
{
    this->r_0 = r_0;
}

void Plume::set_z_0(double z_0)
{
    this->z_0 = z_0;
}

void Plume::setVEI(unsigned int vei)
{
    this->rho_0 = 150.0;
    this->U_0 = vcl::clamp((rho_0 + r_0) * (vei * 9) / 50, 0.0, 200.0);
}

unsigned int Plume::getID()
{
    return this->id;
}

std::string Plume::getVentName()
{
    return this->vent_name;
}

int Plume::getMaxSmoke()
{
    return this->max_smoke;
}

float Plume::getVolume()
{
    float total_volume = 0;
    for (int i = 0; i < smoke_layers.size(); i++)
    {
        float total_smoke_volume = smoke_layers[i].thickness * PI * smoke_layers[i].r * smoke_layers[i].r;
        total_volume += total_smoke_volume;
    }
    
    return total_volume;
}

float Plume::getTransitionSpeed()
{
    return this->transition_speed;
}

float Plume::getTransitionDelay()
{
    return this->transition_delay;
}

std::vector<float>& Plume::getTransitionLifetime()
{
    return this->transition_lifetime;
}

void Plume::setMaxSmoke(int max_smoke)
{
    this->max_smoke = max_smoke;
}

void Plume::setTransitionSpeed(float transition_speed)
{
    this->transition_speed = transition_speed;
}

void Plume::setTranstionDelay(float transition_delay)
{
    this->transition_delay = transition_delay;
}

//------------------------------------------------------------
//--------------------- FALLING SPHERES ----------------------
//------------------------------------------------------------ */

void Plume::sphere_ground_collision(free_sphere_params& sphere, float terrain_z, vcl::vec3 terrain_normal, int idx, unsigned int frame_nb)
{
    // if sphere under ground mesh
    if (sphere.center.z < terrain_z)
    {
        vec3 terrain_pt(sphere.center.x, sphere.center.y, terrain_z);
        vec3 pt_diff = terrain_pt - sphere.center;

        // compute new position
        sphere.center += norm(pt_diff) * terrain_normal;

        // compute new speed
        vec3 v_normal = dot(sphere.speed, terrain_normal) * terrain_normal;
        vec3 v_tan = sphere.speed - v_normal;
        sphere.speed = 1.0f * v_tan - 0.0f * v_normal;

        // sedimentation
        if (sphere.falling_under_atm_rho == false) sphere.rho -= 0.001 * frame_nb * t_step * norm(sphere.speed);
        else sphere.rho -= 0.000005 * frame_nb * t_step * norm(sphere.speed);
        // find buffer for low density particles not in buffer:
        if (idx >= 0 && sphere.rho < compute_atm_density(sphere.center.z))
        {
            sphere.falling_under_atm_rho = true;

            // add particle in a buffer

            // test proximity with existing buffers
            bool is_in_buffer = false;
            for (unsigned int j = 0; j < falling_spheres_buffers.size(); j++)
            {
                if (norm(falling_spheres_buffers[j][0].center - sphere.center) < 5 * sphere.r)
                {
                    falling_spheres_buffers[j].push_back(sphere);
                    falling_spheres.erase(falling_spheres.begin() + idx);
                    is_in_buffer = true;
                    break;
                }
            }
            // if not cloase to any existing buffer, create a new one
            if (is_in_buffer == false)
            {
                std::vector<free_sphere_params> new_buffer;
                new_buffer.push_back(sphere);
                falling_spheres_buffers.push_back(new_buffer);
                falling_spheres.erase(falling_spheres.begin() + idx);
            }
        }
    }

}

void Plume::ground_falling_sphere_update(terrain_structure& terrain_struct, free_sphere_params& sphere, int idx, unsigned int frame_nb)
{
    // find closest layer for radial force
    unsigned int closest_layer_id = 0;
    float min_dist = norm(sphere.center - smoke_layers[0].center);
    for (unsigned int j = 0; j < smoke_layers.size(); j++)
    {
        float dist = norm(sphere.center - smoke_layers[j].center);
        if (dist < min_dist && smoke_layers[j].rising && smoke_layers[j].secondary_plume == false)
        {
            min_dist = dist;
            closest_layer_id = j;
        }
    }

    // precomputation
    float V = 4. / 3. * PI * sphere.r * sphere.r * sphere.r;
    float m = sphere.rho * V;

    float atm_rho = compute_atm_density(sphere.center.z);
    vec3 gravity = vec3(0, 0, -m * g);
    vec3 buoyancy = vec3(0, 0, atm_rho * V * g);
    vec3 friction = -0.1 * normalize(sphere.speed);
    //friction = vec3(0, 0, 0);
    vec3 radial_dir = normalize(sphere.center - smoke_layers[closest_layer_id].center);
    vec3 radial_force = radial_dir * expansion_strength * m;
 


    vec3 forces = gravity + buoyancy + friction + radial_force;
    vec3 a = forces / m;
    vec3 v = sphere.speed + a * frame_nb * t_step;
    vec3 p = sphere.center + v * frame_nb * t_step;

    sphere.speed = v;
    sphere.center = p;
    sphere.lifetime = sphere.lifetime + t_step;

    if (!sphere.falling_disappeared)
    {
        // find ground point and normal
        float terrain_z = terrain_struct.field_height_at(sphere.center.x, sphere.center.y);
        vec3 terrain_normal = terrain_struct.field_normal_at(sphere.center.x, sphere.center.y);
        sphere_ground_collision(sphere, terrain_z, terrain_normal, idx, frame_nb);
    }
}

void Plume::secondary_columns_creation()
{
    // merge close buffers
    for (unsigned int i = 0; i < falling_spheres_buffers.size(); i++)
    {
        for (unsigned int j = i + 1; j < falling_spheres_buffers.size(); j++)
        {
            if (norm(falling_spheres_buffers[i][0].center - falling_spheres_buffers[j][0].center) < 3 * falling_spheres_buffers[i][0].r)
            {
                for (unsigned int k = 0; k < falling_spheres_buffers[j].size(); k++)
                {
                    falling_spheres_buffers[i].push_back(falling_spheres_buffers[j][k]);
                }
                falling_spheres_buffers.erase(falling_spheres_buffers.begin() + j);
            }
        }
    }

    // find big enough buffers
    for (unsigned int i = 0; i < falling_spheres_buffers.size(); i++)
    {
        float wanted_ray = falling_spheres_buffers[i][0].r ;
        float wanted_volume = wanted_ray * PI * wanted_ray * wanted_ray;
        float sphere_volume = 4. / 3. * PI * falling_spheres_buffers[i][0].r * falling_spheres_buffers[i][0].r * falling_spheres_buffers[i][0].r;
        float nb_spheres_needed = wanted_volume / sphere_volume;
        //nb_spheres_needed = 6;

        //find closest layer
        vec3 center_i = falling_spheres_buffers[i][0].center;
        int closest_layer_id = smoke_layers.size() - 1;
        float min_dist = norm(center_i - smoke_layers[closest_layer_id].center);
        for (unsigned int j = 0; j < smoke_layers.size(); j++)
        {
            float dist = norm(center_i - smoke_layers[j].center);
            if (dist < min_dist)
            {
                min_dist = dist;
                closest_layer_id = j;
            }
        }

        if (falling_spheres_buffers[i].size() > nb_spheres_needed * 3 && min_dist > wanted_ray)
        {
            // emit layer
            add_smoke_layer(5, falling_spheres_buffers[i][0].rho, wanted_ray * this->radius_multiplier, falling_spheres_buffers[i][0].center, true);
            add_free_spheres_for_one_layer(smoke_layers.size() - 1);
            //if (debug_mode) std::cout << "SECONDARY LAYER ADDED OK" << std::endl;

            // remove corresponding particles
            for (unsigned int j = 0; j < nb_spheres_needed; j++)
            {
                falling_spheres.push_back(falling_spheres_buffers[i][0]);
                falling_spheres[falling_spheres.size() - 1].falling_disappeared = true;
                falling_spheres[falling_spheres.size() - 1].rho = 10.;
                falling_spheres_buffers[i].erase(falling_spheres_buffers[i].begin());
            }
        }
    }
}

void Plume::falling_spheres_update(terrain_structure& terrain_struct, unsigned int frame_nb)
{
    // edit spheres, attached or not to a buffer
    for (int i = falling_spheres.size() - 1; i >= 0; i--)
    {
        ground_falling_sphere_update(terrain_struct, falling_spheres[i], i, frame_nb);
    }
    for (unsigned int i = 0; i < falling_spheres_buffers.size(); i++)
    {
        for (unsigned int j = 0; j < falling_spheres_buffers[i].size(); j++)
        {
            ground_falling_sphere_update(terrain_struct, falling_spheres_buffers[i][j], -1, frame_nb);
        }
    }

    for (int i = falling_spheres.size() - 1; i >= 0; i--)
    {
        if (falling_spheres[i].falling_disappeared && falling_spheres[i].center.z < -2000.)
        {
            falling_spheres.erase(falling_spheres.begin() + i);
        }
    }

    // emit new layers from buffers
    secondary_columns_creation();
}

//------------------------------------------------------------
//---------------------- FREE SPHERES ------------------------
//------------------------------------------------------------ */

void Plume::add_free_sphere(unsigned int i, float angle, float size_fac)
{
    free_sphere_params sphere(free_sphere_id, smoke_layers[i].center, angle, size_fac * smoke_layers[i].r, smoke_layers[i].v.z);
    free_sphere_id++;
    sphere.size_factor = size_fac;
    sphere.rho = smoke_layers[i].rho;
    float volume = 4.0 / 3.0 * PI * sphere.r * sphere.r * sphere.r;
    sphere.mass = sphere.rho / volume;
    sphere.closest_layer_idx = i;

    if (smoke_layers[i].secondary_plume)
    {
        sphere.secondary_column = true;
        sphere.closest_layer_idx = i;
    }

    for (unsigned int i = 0; i < subspheres_number; i++)
    {
        // random angles
        float theta = PI * static_cast <float>(rand()) / static_cast <float>(RAND_MAX);
        float phi = 2 * PI * static_cast <float>(rand()) / static_cast <float>(RAND_MAX);

        subsphere_params subs = subsphere_params();
        subs.parent_id = free_spheres.size();
        subs.relative_position = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
        subs.center = sphere.center + sphere.r * subs.relative_position;
        //subs.size_ratio = 0.10 + 0.25 * static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
        subs.size_ratio = 0.2;
        subs.r = sphere.r * subs.size_ratio;

        for (unsigned int j = 0; j < subsubspheres_number; j++)
        {
            float theta2 = PI * static_cast <float>(rand()) / static_cast <float>(RAND_MAX);
            float phi2 = 2 * PI * static_cast <float>(rand()) / static_cast <float>(RAND_MAX);

            subsphere_params subsubs = subsphere_params();
            subsubs.parent_id = s2_spheres.size();
            subsubs.relative_position = vec3(sin(theta2) * cos(phi2), sin(theta2) * sin(phi2), cos(theta2));
            subsubs.center = subs.center + subs.r * subsubs.relative_position;
            subsubs.r = subs.r / 5.;
            s3_spheres.push_back(subsubs);
        }
        s2_spheres.push_back(subs);
    }
    free_spheres.push_back(sphere);
}

void Plume::add_free_spheres_for_one_layer(unsigned int i)
{
    float angle_offset = 2 * PI * static_cast <float> (rand()) / static_cast <float> (RAND_MAX);

    //determine size differences
    buffer<float> sizes;
    for (unsigned int k = 0; k < nb_spheres; k++)
    {
        sizes.push_back(0.5f + static_cast <float>(rand()) / static_cast <float>(RAND_MAX));
    }
    
    float total = 0;
    for (unsigned int k = 0; k < nb_spheres; k++)
    {
        total += sizes[k];
    }
    
    float factor = (float)nb_spheres / total;
    for (unsigned int k = 0; k < sizes.size(); k++)
    {
        sizes[k] *= factor;
    }

    // add spheres
    for (unsigned int j = 0; j < nb_spheres; j++)
    {
        float angle = (float)j * 2.0 * PI / 6.0;
        add_free_sphere(i, angle + angle_offset, sizes[j]);
    }
}

float Plume::compute_gaussian_speed_in_layer(float v_z, float max_r, float r)
{
    float A = max_r / (2. * sqrt(log(2)));
    return 2. * v_z * exp(-r * r / (A * A));
}

void Plume::subdivide_and_make_falling(unsigned int i)
{
    // update subspheres
    //update_subspheres_params();

    // define ray of falling spheres; density same as free sphere
    float falling_ray = free_spheres[i].r / 5.;
    float falling_volume = 4. / 3. * PI * falling_ray * falling_ray * falling_ray;
    float free_sphere_volume = 4. / 3. * PI * free_spheres[i].r * free_spheres[i].r * free_spheres[i].r;
    float n_float = free_sphere_volume / falling_volume;
    int n = (int)n_float;

    // start adding subspheres as falling spheres
    for (unsigned int j = 0; j < s2_spheres.size(); j++)
    {
        if (s2_spheres[j].parent_id == i)
        {
            free_sphere_params sphere = free_sphere_params(falling_sphere_id, s2_spheres[j].center, s2_spheres[j].r, free_spheres[i].rho);
            falling_sphere_id++;
            sphere.falling = true;
            sphere.stagnate = false;
            falling_spheres.push_back(sphere);
            if (n >= 0) n--;
        }
    }

    // add all falling spheres
    if (n > 0)
    {
        for (unsigned int j = 0; j < n; j++)
        {
            float rand_r = free_spheres[i].r * static_cast <float>(rand()) / static_cast <float>(RAND_MAX);
            float rand_phi = PI * static_cast <float>(rand()) / static_cast <float>(RAND_MAX);
            float rand_theta = 2 * PI * static_cast <float>(rand()) / static_cast <float>(RAND_MAX);
            vec3 rand_vec(sin(rand_theta) * cos(rand_phi), sin(rand_theta) * sin(rand_phi), cos(rand_theta));
            vec3 new_center = free_spheres[i].center + rand_r * rand_vec; //random position inside free sphere
            free_sphere_params sphere = free_sphere_params(falling_sphere_id, new_center, falling_ray, free_spheres[i].rho);
            falling_spheres.push_back(sphere);
            falling_sphere_id++;
        }
    }

    // make free sphere falling
    free_spheres[i].falling = true;
    //if (debug_mode) std::cout << i << " falls" << std::endl;
}

void Plume::update_free_spheres()
{
    for (int i = free_spheres.size() - 1; i >= 0; i--)
    {
        free_sphere_params& sphere_i = free_spheres[i];
        sphere_i.lifetime = sphere_i.lifetime + t_step;

        if (!sphere_i.stagnate_long && !sphere_i.falling)
        {
            // identify closest layer which is rising or begins falling (later : all layers in which the sphere is)
            int closest_layer_id = smoke_layers.size() - 1;
            float min_dist = norm(sphere_i.center - smoke_layers[closest_layer_id].center);
            for (unsigned int j = 0; j < smoke_layers.size(); j++)
            {
                //float dist = abs(free_spheres[i].center.z - smoke_layers[j].center.z);
                float dist = norm(sphere_i.center - smoke_layers[j].center);
                if (dist < min_dist && (smoke_layers[j].rising || smoke_layers[j].begin_falling) && !smoke_layers[j].stagnates_long)
                {
                    min_dist = dist;
                    closest_layer_id = j;
                }
            }

            if (sphere_i.secondary_column) closest_layer_id = sphere_i.closest_layer_idx;
            if (sphere_i.stagnate || sphere_i.stagnate_long) closest_layer_id = sphere_i.closest_layer_idx;
            closest_layer_id = sphere_i.closest_layer_idx;

            // check if closest layer begins falling (if so, make sphere falling)
            if (smoke_layers[closest_layer_id].begin_falling)
            {
                subdivide_and_make_falling(i);
            }
            else if (smoke_layers[closest_layer_id].stagnates && !smoke_layers[closest_layer_id].stagnates_long && !sphere_i.stagnate)
            {
                // check if densities have become equal: if so, keep altitude in memory
                sphere_i.stagnate = true;
                sphere_i.closest_layer_idx = closest_layer_id;
                sphere_i.stagnation_altitude = sphere_i.center.z;
            }
            else if (sphere_i.stagnate && !sphere_i.stagnate_long && smoke_layers[sphere_i.closest_layer_idx].stagnates_long)
            {
                // check if closest layer has reached max altitude: if so, keep sphere position in memory for stagnation spreading function
                sphere_i.stagnate_long = true;
                sphere_i.max_altitude = sphere_i.center.z;
                sphere_i.center_at_max_altitude = sphere_i.center;
                sphere_i.layer_center_at_max_altitude = smoke_layers[closest_layer_id].center;
                sphere_i.xy_at_max_altitude = sqrt(sphere_i.center.x * sphere_i.center.x + sphere_i.center.y * sphere_i.center.y);
            }
            else
            {
                // get axial and radial composants relative to layer center
                vec3 p_relative = sphere_i.center - smoke_layers[closest_layer_id].center;
                vec3 p_axial = dot(p_relative, smoke_layers[closest_layer_id].plume_axis) * smoke_layers[closest_layer_id].plume_axis;
                vec3 p_radial = p_relative - p_axial;

                // update rotation axis with new radial vector (can change upon time because axis changes)
                sphere_i.angle_vector = p_radial / norm(p_radial);
                //sphere_i.rotation_axis = normalize(cross(smoke_layers[closest_layer_id].plume_axis, p_radial));

                // update radial position if too close from plume axis
                //if (norm(p_radial) < smoke_layers[closest_layer_id].r)*/ free_spheres[i].center = smoke_layers[closest_layer_id].center + p_axial + smoke_layers[closest_layer_id].r * normalize(p_radial);

                // update size according to layer
                // size of layer + perturbation (some spheres should grow much more than others, to create diversity)
                float new_r = sphere_i.size_factor * smoke_layers[closest_layer_id].r;

                // update radial speed and position by adding perturbation (one part is random and one depends on radial position, so that spheres do not stay in the middle of the plume and do not go away)
                float random_f = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
                sphere_i.perturbation += new_r * 0.001 * 2.0 * (random_f - 0.5);
                if (sphere_i.perturbation > 100.) sphere_i.perturbation = 100.;
                if (sphere_i.perturbation < -100.) sphere_i.perturbation = -100.;
                float new_speed = compute_gaussian_speed_in_layer(smoke_layers[closest_layer_id].speed_along_axis, 2. * smoke_layers[closest_layer_id].r, norm(p_relative)); // axial speed
                if (smoke_layers[closest_layer_id].stagnates) new_speed = smoke_layers[closest_layer_id].speed_along_axis;
                float radial_speed = (smoke_layers[closest_layer_id].r - norm(p_radial)) / 0.5;
                if (smoke_layers[closest_layer_id].stagnates) radial_speed = 0;
                sphere_i.perturbation += radial_speed;
                //if (smoke_layers[closest_layer_id].stagnates) std::cout << sphere_i.perturbation << std::endl;
                if (smoke_layers[closest_layer_id].stagnates && sphere_i.perturbation < 0) sphere_i.perturbation = 0;
                radial_speed = 0;
                //std::cout << norm(p_relative) - smoke_layers[closest_layer_id].r << " " << free_spheres[i].perturbation << " " << radial_speed << std::endl;

                // update rotation speed with new speed and ray
                float new_angular_speed = new_speed / new_r;

                // update
                sphere_i.speed = new_speed * smoke_layers[closest_layer_id].plume_axis + (sphere_i.perturbation) * sphere_i.angle_vector;
                sphere_i.r = new_r;
                sphere_i.relative_distance = norm(sphere_i.center - smoke_layers[closest_layer_id].center);
                sphere_i.rho = smoke_layers[closest_layer_id].rho;

                if (!sphere_i.stagnate && smoke_layers[closest_layer_id].theta > 1)
                {
                    sphere_i.angular_speed = new_angular_speed;
                    float new_angle = sphere_i.current_angle + sphere_i.angular_speed * t_step;
                    sphere_i.current_angle = new_angle;
                }
                else sphere_i.angular_speed = 0;
            }
        }
    }

    // update positions
    for (unsigned int i = 0; i < free_spheres.size(); i++)
    {
        if (!free_spheres[i].stagnate_long && !free_spheres[i].falling)
        {
            free_spheres[i].center += t_step * free_spheres[i].speed;
        }
        else if (free_spheres[i].falling)
        {
            free_spheres[i].center.z = smoke_layers[free_spheres[i].closest_layer_idx].center.z;
        }
        //update_spheres_on_free_sphere(i);
    }

    // make begin_falling layers falling
    for (int i = smoke_layers.size() - 1; i >= 0; i--)
    {
        if (smoke_layers[i].begin_falling && smoke_layers.size() >= 1)
        {
            smoke_layers[i].falling = true;
            smoke_layers[i].begin_falling = false;
            smoke_layers[i].rising = false;
            smoke_layers[i].plume = false;
            //smoke_layers[i].center.z = -2000;
        }
    }
}


//------------------------------------------------------------
//---------------------- STAGNATION --------------------------
//------------------------------------------------------------ */

void Plume::update_stagnation_spheres(vec3 wind)
{
    for (int i = free_spheres.size() - 1; i >= 0; i--)
    {
        if (free_spheres[i].stagnate_long)
        {
            // closest layer
            unsigned int closest_layer_id = free_spheres[i].closest_layer_idx;

            // get radial composant relative to layer center
            vec3 p_radial = vec3(free_spheres[i].center.x, free_spheres[i].center.y, 0);

            // update rotation axis with new radial vector (can change upon time because axis changes)
            free_spheres[i].angle_vector = normalize(p_radial);

            // update radial speed and position by adding perturbation
            float speed_factor = norm(vec3(free_spheres[i].center.x, free_spheres[i].center.y, 0)) / 2000.;
            free_spheres[i].perturbation = stagnation_speed / speed_factor;
            if (norm(wind) != 0)
            {
                speed_factor = norm(p_radial) / 2000.;
                free_spheres[i].perturbation = stagnation_speed / speed_factor;
                if (dot(free_spheres[i].speed, p_radial) < 0) free_spheres[i].perturbation = 0;
            }

            //free_spheres[i].angle_vector = normalize(vec3(free_spheres[i].angle_vector.x, free_spheres[i].angle_vector.y, 0));

            // update
            free_spheres[i].speed = 0 * free_spheres[i].speed + 1 * (free_spheres[i].perturbation) * free_spheres[i].angle_vector;

            float dxy = norm(free_spheres[i].speed) * t_step;
            vec3 relative_pos_at_max_alt = free_spheres[i].center_at_max_altitude - free_spheres[i].layer_center_at_max_altitude;
            float relative_xy_at_max_alt = sqrt(relative_pos_at_max_alt.x * relative_pos_at_max_alt.x + relative_pos_at_max_alt.y * relative_pos_at_max_alt.y);
            float a = (free_spheres[i].max_altitude - free_spheres[i].stagnation_altitude) * relative_xy_at_max_alt;

            vec3 relative_pos = free_spheres[i].center - free_spheres[i].layer_center_at_max_altitude;
            float relative_xy = sqrt(relative_pos.x * relative_pos.x + relative_pos.y * relative_pos.y);
            float dz = -a * dxy / ((relative_xy) * (relative_xy));
            if (relative_xy < relative_xy_at_max_alt) dz = 0;
            float new_z = free_spheres[i].stagnation_altitude + a / (relative_xy);
            if (relative_xy <= relative_xy_at_max_alt) new_z = free_spheres[i].max_altitude;

            free_spheres[i].center.z = new_z;
            free_spheres[i].angular_speed = 0;
            free_spheres[i].rho = smoke_layers[closest_layer_id].rho;

            free_spheres[i].speed += wind;
        }
    }
}

void Plume::update_stagnation_spheres_position()
{
    // update positions
    for (unsigned int i = 0; i < free_spheres.size(); i++)
    {
        if (free_spheres[i].stagnate_long)
        {
            free_spheres[i].center += t_step * free_spheres[i].speed;
            //update_spheres_on_stagnation_sphere(i);
            if (free_spheres[i].closest_layer_idx != -1)
            {
                vec3 relative_position = free_spheres[i].center - smoke_layers[free_spheres[i].closest_layer_idx].center;
                free_spheres[i].relative_distance = norm(relative_position);
                free_spheres[i].angle_vector = normalize(relative_position);
            }
        }
    }
}

