#include "Plume.hpp"

//------------------------------------------------------------
//------------------------- ALGO -----------------------------
//------------------------------------------------------------ */

Plume::Plume(unsigned int id, std::string ventName, float3 ventPosition, EruptionParams eruptParams)
{
    this->mId = id;
    this->mVentName = ventName;
    this->mVentPosition = ventPosition;
    this->mExpansionStrength = 1;// for use in making the spread of pyroclastic plumes
    this->mRadiusMultiplier = 2.f;// for use in the size of secondary columns
	mEruptParams = eruptParams;
	reset_parameters();
    mMaxRadius = (float)mEruptParams.maxRadius;

    mSubspheresNumber = 0;
    mSubSubspheresNumber = 0;

    // Parameters : constants
    mG = 9.81f; // (m.s-2)
    mMinLifetime = 180.0;
    mMaxLifetime = 240.0;

    // coeff init
    mAirIncorporationCoeff = 5.;

    //transition
    mMaxSmoke = 20;
    mTransitionSpeed = 5.0f;
    mTransitionDelay = 0.2f;
    for (int i = 0; i < mMaxSmoke; i++)
        mTransitionLifetime.push_back(mTransitionDelay * i);

    reset();
}

void Plume::reset()
{
    mFreeSphereID = 0;
    mFallingSphereID = 0;

    mTStep = 0;
    mNewLayerDelay = 0;
    mNbOfIterations = 0;
    mLastPpeLayerIdx = 0;
    mStagnationSpeed = 50;

    mSmokeLayers.clear();
    mFreeSpheres.clear();
    mS2Spheres.clear();
    mS3Spheres.clear();
    mFallingSpheres.clear();
    mStagnateSpheres.clear();
    mFallingSpheresBuffers.clear();

    mTransitionLifetime.clear();
    for (int i = 0; i < mMaxSmoke; i++)
        mTransitionLifetime.push_back(mTransitionDelay * i);
}

void Plume::reset_parameters()
{
    // Parameters : to be chosen by user
    mT0 = 1273.; // initial temp (K)
    mTheta0 = 0.; // initial angle (rad)
    mU0 = mEruptParams.U_0; // initial speed (m.s-1)
    mN0 = 0.03f; // initial gas mass fraction
    mZ0 = mEruptParams.z_0; // initial altitude (m)
    mR0 = mEruptParams.r_0; // initial radius (m)
    mRho0 = mEruptParams.rho_0;
    mVentPosition.z = (float)mZ0;
}

void Plume::set_t_step(float dt)
{
    this->mTStep = dt;
    this->mNewLayerDelay += dt;

    for (int i = 0; i < mTransitionLifetime.size(); i++)
    {
        mTransitionLifetime[i] += dt;
    }
}

void Plume::add_smoke_layer(float v, float d, float r, float3 position, bool secondary_plume)
{
    smoke_layer layer = smoke_layer({ 0,0,v }, d, r, position, secondary_plume);
    mSmokeLayers.push_back(layer);
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
    float vent_area = PI * mR0 * mR0;
    return scaling_coeff * mRho0 * vent_area * mU0 * (1.0 + mN0) * frag_factor;
}

float Plume::computeVEI()
{
    float logMER = std::log10(computeMER(1.0f, 1.0f));
    return (logMER - 3.58) / 0.92;
}

void Plume::edit_smoke_layer_properties(unsigned int i, float& d_mass, float3 wind)
{
    // preliminary computation
    float thk = mTStep * mSmokeLayers[i].v.z;
    //thk = mSmokeLayers[i].v.z * 0.002;

    float total_smoke_thk = mSmokeLayers[i].thickness;
    float total_smoke_volume = total_smoke_thk * PI * mSmokeLayers[i].r * mSmokeLayers[i].r;
    float total_smoke_mass = mSmokeLayers[i].rho * total_smoke_volume;

    // wind velocity around for air incorporation
    float k_s = 0.09f, k_w = 0.9f;
    float U_e = k_s * abs(length(mSmokeLayers[i].v) - length(wind) * cos(mSmokeLayers[i].theta)) + k_w * abs(length(wind) * sin(mSmokeLayers[i].theta));
    float r_atm = mAirIncorporationCoeff * U_e;
    //r_atm = U_e * mTStep;

    // air quantity to put in the plume
    float atm_density = compute_atm_density(mSmokeLayers[i].center.z);
    float atm_volume = thk * PI * (2.0 * mSmokeLayers[i].r + r_atm) * r_atm; //air around the smoke layer
    float atm_mass = atm_density * atm_volume;

    // compute new temperature (we take all Cp equal)
    float new_temp = (total_smoke_mass * mSmokeLayers[i].temperature + atm_mass * compute_atm_temperature(mSmokeLayers[i].center.z)) / (total_smoke_mass + atm_mass);
    mSmokeLayers[i].temperature = new_temp;

    // new volume after heating
    float atm_new_volume = mSmokeLayers[i].temperature * atm_volume / compute_atm_temperature(mSmokeLayers[i].center.z); // after heating by hot smoke

    // new params
    d_mass = atm_mass;
    float mass_new = total_smoke_mass + atm_mass;
    float volume_new = total_smoke_volume + atm_new_volume;
    float rho_new = mass_new / volume_new;
    float r_new = cbrt(volume_new / PI);
    mSmokeLayers[i].thickness = r_new;

    // new speed due to conservation of energy (old)
    //float energy = 0.5 * total_smoke_mass * mSmokeLayers[i].v.z * mSmokeLayers[i].v.z;
    //float new_speed = sqrt(2.0 * energy / mass_new);
    //if (rho_new < atm_density) mSmokeLayers[i].v = {0,0,new_speed};

    if (mSmokeLayers[i].rho > atm_density && rho_new < atm_density) mSmokeLayers[i].plume = true;
    mSmokeLayers[i].r = r_new;
    mSmokeLayers[i].rho = rho_new;
}

void Plume::apply_forces_to_smoke_layer(unsigned int i, float d_mass, float3 wind)
{
    // precomputation
    float atm_density = compute_atm_density(mSmokeLayers[i].center.z);
    float volume = mSmokeLayers[i].thickness * PI * mSmokeLayers[i].r * mSmokeLayers[i].r;
    float surface = 2 * PI * mSmokeLayers[i].r * mSmokeLayers[i].r;
    float surface_eff = 2 * mSmokeLayers[i].r * mSmokeLayers[i].r;
    float smoke_mass = mSmokeLayers[i].rho * volume;
    float3 v_diff = wind - float3(mSmokeLayers[i].v.x, mSmokeLayers[i].v.y, 0);

    float3 weight = { 0,0, -smoke_mass * mG }; // m*mG
    float3 archimede = { 0,0, atm_density * volume * mG }; // rho*V*mG
    float3 friction = -0.5f * atm_density * 0.04f * surface * length(mSmokeLayers[i].v) * mSmokeLayers[i].v; // axial friction
    //friction = - volume * 0.00005 * norm(mSmokeLayers[i].v) * mSmokeLayers[i].v; // old way to compute friction
    float3 wind_force = 600.f * length(v_diff) * v_diff * surface_eff; // horizontal

    float3 forces = weight + archimede + friction + wind_force;

    float3 old_v = mSmokeLayers[i].v;
    float old_m = smoke_mass - d_mass;

    // equation of dynamics without mass conservation
    float3 mv = old_m * old_v + forces * mTStep;
    float3 v = mv / smoke_mass;
    float3 p = mSmokeLayers[i].center + v * mTStep;

    // old with mass conservation
    //float3 a = forces / (smoke_mass);
    //float3 v = mSmokeLayers[i].v + a*mTStep;
    //float3 p = mSmokeLayers[i].center + v*mTStep;


    // check if falls
    if (old_v.z > 0 && v.z < 0 && mSmokeLayers[i].plume == false)
    {
        mSmokeLayers[i].rising = false;
        mSmokeLayers[i].begin_falling = true;
    }

    // check if stagnates
    if (mSmokeLayers[i].plume && !mSmokeLayers[i].stagnates && mSmokeLayers[i].center.z > 5000 && (atm_density - mSmokeLayers[i].rho < 0.01))
    {
        mSmokeLayers[i].stagnates = true;
        mStagnationSpeed = mSmokeLayers[i].v.z;
    }

    // check if stagnates long
    if (mSmokeLayers[i].stagnates && v.z < 0 && !mSmokeLayers[i].stagnates_long)
    {
        mSmokeLayers[i].stagnates_long = true;
    }

    if (mSmokeLayers[i].stagnates && p.z < mSmokeLayers[i].center.z)
    {
        p.z = mSmokeLayers[i].center.z;
    }

    // update
    mSmokeLayers[i].a = v / mTStep;
    mSmokeLayers[i].v = v;
    mSmokeLayers[i].center = p;
    if (p.z < -1000) mSmokeLayers[i].center.z = -1000; // prevent from going oob after falling (layers are not deleted but not used anymore)

    // compute new theta
    // WARNING: wind direction was constant in my tests, if the direction changes it may not work, it has to be tested
    //float dx = v.x*mTStep, dy = v.y*mTStep;
    float dz = v.z * mTStep;
    mSmokeLayers[i].speed_along_axis = length(v);
    mSmokeLayers[i].plume_axis = normalize(v);
    if (length(wind) != 0 && mTStep > 0)
    {
        mSmokeLayers[i].theta_axis = normalize(cross(wind, float3(0, 0, 1)));
        float theta_totest = asin(dz / length(v * mTStep));
        if (dz / length(v * mTStep) < 1.000001 && dz / length(v * mTStep) > 0.999999) theta_totest = PI / 2.f; // security to prevent nan values due to asin
        mSmokeLayers[i].theta = theta_totest;

        if (theta_totest < 0)
        {
            mSmokeLayers[i].theta = 0;
        }
    }
}

void Plume::sedimentation(unsigned int i, float& d_mass)
{
    // constant sedimentation
    float layer_volume = PI * mSmokeLayers[i].r * mSmokeLayers[i].r * mSmokeLayers[i].thickness;
    float diff_density = 0.00000005 * mTStep;
    if (mSmokeLayers[i].stagnates_long) diff_density = 0.00005 * mTStep;

    if (mSmokeLayers[i].rho > diff_density)
    {
        mSmokeLayers[i].rho -= diff_density;
        d_mass -= diff_density * layer_volume;
    }

}

void Plume::smoke_layer_update(unsigned int i, float3 wind)
{
    float d_mass = 0; // to track mass change for equation of dynamics
    mSmokeLayers[i].lifetime = mSmokeLayers[i].lifetime + mTStep;

    if (mSmokeLayers[i].plume == true && mSmokeLayers[i].center.z > 0.) sedimentation(i, d_mass); // sedimentation in altitude
    if (mSmokeLayers[i].rising && !mSmokeLayers[i].stagnates_long) edit_smoke_layer_properties(i, d_mass, wind); // convection if v_z > 0 (convection causes air entrainment)
    apply_forces_to_smoke_layer(i, d_mass, wind);
}

void Plume::update_smoke_layer_init()
{
    // add smoke layer each x seconds
    if (mSmokeLayers.size() == 0 || (mNewLayerDelay >= mR0 / (2 * mU0) && mSmokeLayers.size() < 1000000000000000))
    {
        add_smoke_layer(mU0, mRho0, mR0, mVentPosition, false);
        add_free_spheres_for_one_layer(mSmokeLayers.size() - 1);

        mNewLayerDelay = 0;
        std::cout << mVentName << " - LAYER ADDED OK" << std::endl;
        std::cout << mVentName << " - Total Layers: " << mSmokeLayers.size() << std::endl;
    }
}

void Plume::remove_colliding_smoke()
{
    if (!mFreeSpheres.empty())
    {
        float ratio = 100.0f;
        float crater_r = 20.0f;
        float y_offset = -10.0f;
        for (int i = mFreeSpheres.size() - 1; i >= 0; i--)
        {
            if (abs(mFreeSpheres[i].center.x / ratio) > crater_r && mFreeSpheres[i].center.y - (mFreeSpheres[i].r / ratio) + y_offset < 0)
                mFreeSpheres.erase(mFreeSpheres.begin() + i);
        }
    }
}

void Plume::remove_smoke_layers()
{
    if (!mSmokeLayers.empty())
    {
        while (mSmokeLayers[0].lifetime > mMaxLifetime)
        {
            mSmokeLayers.erase(mSmokeLayers.begin());

            for (int i = 0; i < mFreeSpheres.size(); i++)
            {
                mFreeSpheres[i].closest_layer_idx--;
            }

            while (mFreeSpheres[0].closest_layer_idx < 0)
            {
                mFreeSpheres.erase(mFreeSpheres.begin());
            }
        }
    }
}

float3 Plume::getPosition()
{
    return this->mVentPosition;
}

float Plume::get_T_0()
{
    return this->mT0;
}

float Plume::get_theta_0()
{
    return this->mTheta0;
}

float Plume::get_U_0()
{
    return this->mU0;
}

float Plume::get_n_0()
{
    return this->mN0;
}

float Plume::get_z_0()
{
    return this->mZ0;
}

float Plume::get_r_0()
{
    return this->mR0;
}

float Plume::get_rho_0()
{
    return this->mRho0;
}

float Plume::getMaxRadius()
{
    return this->mMaxRadius;
}

unsigned int Plume::getVEI()
{
    float height = (mU0 * 50) / (mRho0 + mR0);
	//std::cout << "Height: " << height << std::endl;
    return clamp(unsigned int(height / 9), 1u, 6u);
}

void Plume::set_U_0(float U0)
{
    this->mU0 = mU0;
}

void Plume::set_rho_0(float Rho0)
{
    this->mRho0 = Rho0;
}

void Plume::set_r_0(float R0)
{
    this->mR0 = R0;
}

void Plume::set_z_0(float Z0)
{
    this->mZ0 = Z0;
}

void Plume::setVEI(unsigned int vei)
{
    this->mRho0 = 150.0;
    this->mU0 = clamp((mRho0 + mR0) * (vei * 9) / 50, 0.0f, 200.0f);
}

unsigned int Plume::getID()
{
    return this->mId;
}

std::string Plume::getVentName()
{
    return this->mVentName;
}

int Plume::getMaxSmoke()
{
    return this->mMaxSmoke;
}

float Plume::getVolume()
{
    float total_volume = 0;
    for (int i = 0; i < mSmokeLayers.size(); i++)
    {
        float total_smoke_volume = mSmokeLayers[i].thickness * PI * mSmokeLayers[i].r * mSmokeLayers[i].r;
        total_volume += total_smoke_volume;
    }
    
    return total_volume;
}

float Plume::getTransitionSpeed()
{
    return this->mTransitionSpeed;
}

float Plume::getTransitionDelay()
{
    return this->mTransitionDelay;
}

std::vector<float>& Plume::getTransitionLifetime()
{
    return this->mTransitionLifetime;
}

void Plume::setMaxSmoke(int max)
{
    this->mMaxSmoke = max;
}

void Plume::setTransitionSpeed(float speed)
{
    this->mTransitionSpeed = speed;
}

void Plume::setTranstionDelay(float delay)
{
    this->mTransitionDelay = delay;
}

//------------------------------------------------------------
//--------------------- FALLING SPHERES ----------------------
//------------------------------------------------------------ */

void Plume::sphere_ground_collision(free_sphere_params& sphere, float terrain_z, float3 terrain_normal, int idx, unsigned int frame_nb)
{
    // if sphere under ground mesh
    if (sphere.center.z < terrain_z)
    {
        float3 terrain_pt(sphere.center.x, sphere.center.y, terrain_z);
        float3 pt_diff = terrain_pt - sphere.center;

        // compute new position
        sphere.center += length(pt_diff) * terrain_normal;

        // compute new speed
        float3 v_normal = dot(sphere.speed, terrain_normal) * terrain_normal;
        float3 v_tan = sphere.speed - v_normal;
        sphere.speed = 1.0f * v_tan - 0.0f * v_normal;

        // sedimentation
        if (sphere.falling_under_atm_rho == false) sphere.rho -= 0.001 * frame_nb * mTStep * length(sphere.speed);
        else sphere.rho -= 0.000005 * frame_nb * mTStep * length(sphere.speed);
        // find buffer for low density particles not in buffer:
        if (idx >= 0 && sphere.rho < compute_atm_density(sphere.center.z))
        {
            sphere.falling_under_atm_rho = true;

            // add particle in a buffer

            // test proximity with existing buffers
            bool is_in_buffer = false;
            for (unsigned int j = 0; j < mFallingSpheresBuffers.size(); j++)
            {
                if (length(mFallingSpheresBuffers[j][0].center - sphere.center) < 5 * sphere.r)
                {
                    mFallingSpheresBuffers[j].push_back(sphere);
                    mFallingSpheres.erase(mFallingSpheres.begin() + idx);
                    is_in_buffer = true;
                    break;
                }
            }
            // if not cloase to any existing buffer, create a new one
            if (is_in_buffer == false)
            {
                std::vector<free_sphere_params> new_buffer;
                new_buffer.push_back(sphere);
                mFallingSpheresBuffers.push_back(new_buffer);
                mFallingSpheres.erase(mFallingSpheres.begin() + idx);
            }
        }
    }

}

void Plume::ground_falling_sphere_update(TerrainStructure& terrain_struct, free_sphere_params& sphere, int idx, unsigned int frame_nb)
{
    // find closest layer for radial force
    unsigned int closest_layer_id = 0;
    float min_dist = length(sphere.center - mSmokeLayers[0].center);
    for (unsigned int j = 0; j < mSmokeLayers.size(); j++)
    {
        float dist = length(sphere.center - mSmokeLayers[j].center);
        if (dist < min_dist && mSmokeLayers[j].rising && mSmokeLayers[j].secondary_plume == false)
        {
            min_dist = dist;
            closest_layer_id = j;
        }
    }

    // precomputation
    float V = 4. / 3. * PI * sphere.r * sphere.r * sphere.r;
    float m = sphere.rho * V;

    float atm_rho = compute_atm_density(sphere.center.z);
    float3 gravity = float3(0, 0, -m * mG);
    float3 buoyancy = float3(0, 0, atm_rho * V * mG);
    float3 friction = -0.1f * normalize(sphere.speed);
    //friction = float3(0, 0, 0);
    float3 radial_dir = normalize(sphere.center - mSmokeLayers[closest_layer_id].center);
    float3 radial_force = radial_dir * mExpansionStrength * m;
 


    float3 forces = gravity + buoyancy + friction + radial_force;
    float3 a = forces / m;
    float3 v = sphere.speed + a * (float)frame_nb * mTStep;
    float3 p = sphere.center + v * (float)frame_nb * mTStep;

    sphere.speed = v;
    sphere.center = p;
    sphere.lifetime = sphere.lifetime + mTStep;

    if (!sphere.falling_disappeared)
    {
        // find ground point and normal
        float terrain_z = terrain_struct.field_height_at(sphere.center.x, sphere.center.y);
        float3 terrain_normal = terrain_struct.field_normal_at(sphere.center.x, sphere.center.y);
        sphere_ground_collision(sphere, terrain_z, terrain_normal, idx, frame_nb);
    }
}

void Plume::secondary_columns_creation()
{
    // merge close buffers
    for (unsigned int i = 0; i < mFallingSpheresBuffers.size(); i++)
    {
        for (unsigned int j = i + 1; j < mFallingSpheresBuffers.size(); j++)
        {
            if (length(mFallingSpheresBuffers[i][0].center - mFallingSpheresBuffers[j][0].center) < 3 * mFallingSpheresBuffers[i][0].r)
            {
                for (unsigned int k = 0; k < mFallingSpheresBuffers[j].size(); k++)
                {
                    mFallingSpheresBuffers[i].push_back(mFallingSpheresBuffers[j][k]);
                }
                mFallingSpheresBuffers.erase(mFallingSpheresBuffers.begin() + j);
            }
        }
    }

    // find big enough buffers
    for (unsigned int i = 0; i < mFallingSpheresBuffers.size(); i++)
    {
        float wanted_ray = mFallingSpheresBuffers[i][0].r ;
        float wanted_volume = wanted_ray * PI * wanted_ray * wanted_ray;
        float sphere_volume = 4. / 3. * PI * mFallingSpheresBuffers[i][0].r * mFallingSpheresBuffers[i][0].r * mFallingSpheresBuffers[i][0].r;
        float nb_spheres_needed = wanted_volume / sphere_volume;
        //nb_spheres_needed = 6;

        //find closest layer
        float3 center_i = mFallingSpheresBuffers[i][0].center;
        int closest_layer_id = mSmokeLayers.size() - 1;
        float min_dist = length(center_i - mSmokeLayers[closest_layer_id].center);
        for (unsigned int j = 0; j < mSmokeLayers.size(); j++)
        {
            float dist = length(center_i - mSmokeLayers[j].center);
            if (dist < min_dist)
            {
                min_dist = dist;
                closest_layer_id = j;
            }
        }

        if (mFallingSpheresBuffers[i].size() > nb_spheres_needed * 3 && min_dist > wanted_ray)
        {
            // emit layer
            add_smoke_layer(5, mFallingSpheresBuffers[i][0].rho, wanted_ray * this->mRadiusMultiplier, mFallingSpheresBuffers[i][0].center, true);
            add_free_spheres_for_one_layer(mSmokeLayers.size() - 1);
            //if (debug_mode) std::cout << "SECONDARY LAYER ADDED OK" << std::endl;

            // remove corresponding particles
            for (unsigned int j = 0; j < nb_spheres_needed; j++)
            {
                mFallingSpheres.push_back(mFallingSpheresBuffers[i][0]);
                mFallingSpheres[mFallingSpheres.size() - 1].falling_disappeared = true;
                mFallingSpheres[mFallingSpheres.size() - 1].rho = 10.;
                mFallingSpheresBuffers[i].erase(mFallingSpheresBuffers[i].begin());
            }
        }
    }
}

void Plume::falling_spheres_update(TerrainStructure& terrain_struct, unsigned int frame_nb)
{
    // edit spheres, attached or not to a buffer
    for (int i = mFallingSpheres.size() - 1; i >= 0; i--)
    {
        ground_falling_sphere_update(terrain_struct, mFallingSpheres[i], i, frame_nb);
    }
    for (unsigned int i = 0; i < mFallingSpheresBuffers.size(); i++)
    {
        for (unsigned int j = 0; j < mFallingSpheresBuffers[i].size(); j++)
        {
            ground_falling_sphere_update(terrain_struct, mFallingSpheresBuffers[i][j], -1, frame_nb);
        }
    }

    for (int i = mFallingSpheres.size() - 1; i >= 0; i--)
    {
        if (mFallingSpheres[i].falling_disappeared && mFallingSpheres[i].center.z < -2000.)
        {
            mFallingSpheres.erase(mFallingSpheres.begin() + i);
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
        free_sphere_params sphere(mFreeSphereID, mSmokeLayers[i].center, angle, size_fac * mSmokeLayers[i].r, mSmokeLayers[i].v.z);
        mFreeSphereID++;
        sphere.size_factor = size_fac;
        sphere.rho = mSmokeLayers[i].rho;
        float volume = 4.0 / 3.0 * PI * sphere.r * sphere.r * sphere.r;
        sphere.mass = sphere.rho / volume;
        sphere.closest_layer_idx = i;

        if (mSmokeLayers[i].secondary_plume)
        {
            sphere.secondary_column = true;
            sphere.closest_layer_idx = i;
        }

        for (unsigned int j = 0; j < mSubspheresNumber; j++)
        {
            // random angles
            float theta = PI * static_cast <float>(rand()) / static_cast <float>(RAND_MAX);
            float phi = 2.0f * PI * static_cast <float>(rand()) / static_cast <float>(RAND_MAX);

            subsphere_params subs = subsphere_params();
            subs.parent_id = mFreeSpheres.size();
            subs.relative_position = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            subs.center = sphere.center + sphere.r * subs.relative_position;
            //subs.size_ratio = 0.10 + 0.25 * static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
            subs.size_ratio = 0.2f;
            subs.r = sphere.r * subs.size_ratio;

            for (unsigned int k = 0; k < mSubSubspheresNumber; k++)
            {
                float theta2 = PI * static_cast <float>(rand()) / static_cast <float>(RAND_MAX);
                float phi2 = 2.f * PI * static_cast <float>(rand()) / static_cast <float>(RAND_MAX);

                subsphere_params subsubs = subsphere_params();
                subsubs.parent_id = mS2Spheres.size();
                subsubs.relative_position = float3(sin(theta2) * cos(phi2), sin(theta2) * sin(phi2), cos(theta2));
                subsubs.center = subs.center + subs.r * subsubs.relative_position;
                subsubs.r = subs.r / 5.f;
                mS3Spheres.push_back(subsubs);
            }
            mS2Spheres.push_back(subs);
        }
        mFreeSpheres.push_back(sphere);
    }

void Plume::add_free_spheres_for_one_layer(unsigned int i)
{
    float angle_offset = 2 * PI * static_cast <float> (rand()) / static_cast <float> (RAND_MAX);

    //determine size differences
    std::vector<float> sizes;
    for (unsigned int k = 0; k < mNbSpheres; k++)
    {
        sizes.push_back(0.5f + static_cast <float>(rand()) / static_cast <float>(RAND_MAX));
    }
    
    float total = 0;
    for (unsigned int k = 0; k < mNbSpheres; k++)
    {
        total += sizes[k];
    }
    
    float factor = (float)mNbSpheres / total;
    for (unsigned int k = 0; k < sizes.size(); k++)
    {
        sizes[k] *= factor;
    }

    // add spheres
    for (unsigned int j = 0; j < mNbSpheres; j++)
    {
        float angle = (float)j * 2.0 * PI / 6.0;
        add_free_sphere(i, angle + angle_offset, sizes[j]);
    }
}

float Plume::compute_gaussian_speed_in_layer(float v_z, float max_r, float r)
{
    float A = max_r / (2. * sqrt(logf(2)));
    return 2. * v_z * exp(-r * r / (A * A));
}

void Plume::subdivide_and_make_falling(unsigned int i)
{
    // update subspheres
    //update_subspheres_params();

    // define ray of falling spheres; density same as free sphere
    float falling_ray = mFreeSpheres[i].r / 5.;
    float falling_volume = 4. / 3. * PI * falling_ray * falling_ray * falling_ray;
    float free_sphere_volume = 4. / 3. * PI * mFreeSpheres[i].r * mFreeSpheres[i].r * mFreeSpheres[i].r;
    float n_float = free_sphere_volume / falling_volume;
    int n = (int)n_float;

    // start adding subspheres as falling spheres
    for (unsigned int j = 0; j < mS2Spheres.size(); j++)
    {
        if (mS2Spheres[j].parent_id == i)
        {
            free_sphere_params sphere = free_sphere_params(mFallingSphereID, mS2Spheres[j].center, mS2Spheres[j].r, mFreeSpheres[i].rho);
            mFallingSphereID++;
            sphere.falling = true;
            sphere.stagnate = false;
            mFallingSpheres.push_back(sphere);
            if (n >= 0) n--;
        }
    }

    // add all falling spheres
    if (n > 0)
    {
        for (unsigned int k = 0; k < (unsigned int)n; k++)
        {
            float rand_r = mFreeSpheres[i].r * static_cast <float>(rand()) / static_cast <float>(RAND_MAX);
            float rand_phi = PI * static_cast <float>(rand()) / static_cast <float>(RAND_MAX);
            float rand_theta = 2.f * PI * static_cast <float>(rand()) / static_cast <float>(RAND_MAX);
            float3 rand_vec(sin(rand_theta) * cos(rand_phi), sin(rand_theta) * sin(rand_phi), cos(rand_theta));
            float3 new_center = mFreeSpheres[i].center + rand_r * rand_vec; //random position inside free sphere
            free_sphere_params sphere = free_sphere_params(mFallingSphereID, new_center, falling_ray, mFreeSpheres[i].rho);
            mFallingSpheres.push_back(sphere);
            mFallingSphereID++;
        }
    }

    // make free sphere falling
    mFreeSpheres[i].falling = true;
    //if (debug_mode) std::cout << i << " falls" << std::endl;
}

void Plume::update_free_spheres()
{
    for (int i = mFreeSpheres.size() - 1; i >= 0; i--)
    {
        free_sphere_params& sphere_i = mFreeSpheres[i];
        sphere_i.lifetime = sphere_i.lifetime + mTStep;

        if (!sphere_i.stagnate_long && !sphere_i.falling)
        {
            // identify closest layer which is rising or begins falling (later : all layers in which the sphere is)
            int closest_layer_id = mSmokeLayers.size() - 1;
            float min_dist = length(sphere_i.center - mSmokeLayers[closest_layer_id].center);
            for (unsigned int j = 0; j < mSmokeLayers.size(); j++)
            {
                //float dist = abs(mFreeSpheres[i].center.z - mSmokeLayers[j].center.z);
                float dist = length(sphere_i.center - mSmokeLayers[j].center);
                if (dist < min_dist && (mSmokeLayers[j].rising || mSmokeLayers[j].begin_falling) && !mSmokeLayers[j].stagnates_long)
                {
                    min_dist = dist;
                    closest_layer_id = j;
                }
            }

            if (sphere_i.secondary_column) closest_layer_id = sphere_i.closest_layer_idx;
            if (sphere_i.stagnate || sphere_i.stagnate_long) closest_layer_id = sphere_i.closest_layer_idx;
            closest_layer_id = sphere_i.closest_layer_idx;

            // check if closest layer begins falling (if so, make sphere falling)
            if (mSmokeLayers[closest_layer_id].begin_falling)
            {
                subdivide_and_make_falling(i);
            }
            else if (mSmokeLayers[closest_layer_id].stagnates && !mSmokeLayers[closest_layer_id].stagnates_long && !sphere_i.stagnate)
            {
                // check if densities have become equal: if so, keep altitude in memory
                sphere_i.stagnate = true;
                sphere_i.closest_layer_idx = closest_layer_id;
                sphere_i.stagnation_altitude = sphere_i.center.z;
            }
            else if (sphere_i.stagnate && !sphere_i.stagnate_long && mSmokeLayers[sphere_i.closest_layer_idx].stagnates_long)
            {
                // check if closest layer has reached max altitude: if so, keep sphere position in memory for stagnation spreading function
                sphere_i.stagnate_long = true;
                sphere_i.max_altitude = sphere_i.center.z;
                sphere_i.center_at_max_altitude = sphere_i.center;
                sphere_i.layer_center_at_max_altitude = mSmokeLayers[closest_layer_id].center;
                sphere_i.xy_at_max_altitude = sqrt(sphere_i.center.x * sphere_i.center.x + sphere_i.center.y * sphere_i.center.y);
            }
            else
            {
                // get axial and radial composants relative to layer center
                float3 p_relative = sphere_i.center - mSmokeLayers[closest_layer_id].center;
                float3 p_axial = dot(p_relative, mSmokeLayers[closest_layer_id].plume_axis) * mSmokeLayers[closest_layer_id].plume_axis;
                float3 p_radial = p_relative - p_axial;

                // update rotation axis with new radial vector (can change upon time because axis changes)
                sphere_i.angle_vector = p_radial / length(p_radial);
                //sphere_i.rotation_axis = normalize(cross(mSmokeLayers[closest_layer_id].plume_axis, p_radial));

                // update radial position if too close from plume axis
                //if (norm(p_radial) < mSmokeLayers[closest_layer_id].r)*/ mFreeSpheres[i].center = mSmokeLayers[closest_layer_id].center + p_axial + mSmokeLayers[closest_layer_id].r * normalize(p_radial);

                // update size according to layer
                // size of layer + perturbation (some spheres should grow much more than others, to create diversity)
                float new_r = sphere_i.size_factor * mSmokeLayers[closest_layer_id].r;

                // update radial speed and position by adding perturbation (one part is random and one depends on radial position, so that spheres do not stay in the middle of the plume and do not go away)
                float random_f = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
                sphere_i.perturbation += new_r * 0.001 * 2.0 * (random_f - 0.5);
                if (sphere_i.perturbation > 100.) sphere_i.perturbation = 100.;
                if (sphere_i.perturbation < -100.) sphere_i.perturbation = -100.;
                float new_speed = compute_gaussian_speed_in_layer(mSmokeLayers[closest_layer_id].speed_along_axis, 2. * mSmokeLayers[closest_layer_id].r, length(p_relative)); // axial speed
                if (mSmokeLayers[closest_layer_id].stagnates) new_speed = mSmokeLayers[closest_layer_id].speed_along_axis;
                float radial_speed = (mSmokeLayers[closest_layer_id].r - length(p_radial)) / 0.5;
                if (mSmokeLayers[closest_layer_id].stagnates) radial_speed = 0;
                sphere_i.perturbation += radial_speed;
                //if (mSmokeLayers[closest_layer_id].stagnates) std::cout << sphere_i.perturbation << std::endl;
                if (mSmokeLayers[closest_layer_id].stagnates && sphere_i.perturbation < 0) sphere_i.perturbation = 0;
                radial_speed = 0;
                //std::cout << norm(p_relative) - mSmokeLayers[closest_layer_id].r << " " << mFreeSpheres[i].perturbation << " " << radial_speed << std::endl;

                // update rotation speed with new speed and ray
                float new_angular_speed = new_speed / new_r;

                // update
                sphere_i.speed = new_speed * mSmokeLayers[closest_layer_id].plume_axis + (sphere_i.perturbation) * sphere_i.angle_vector;
                sphere_i.r = new_r;
                sphere_i.relative_distance = length(sphere_i.center - mSmokeLayers[closest_layer_id].center);
                sphere_i.rho = mSmokeLayers[closest_layer_id].rho;

                if (!sphere_i.stagnate && mSmokeLayers[closest_layer_id].theta > 1)
                {
                    sphere_i.angular_speed = new_angular_speed;
                    float new_angle = sphere_i.current_angle + sphere_i.angular_speed * mTStep;
                    sphere_i.current_angle = new_angle;
                }
                else sphere_i.angular_speed = 0;
            }
        }
    }

    // update positions
    for (unsigned int i = 0; i < mFreeSpheres.size(); i++)
    {
        if (!mFreeSpheres[i].stagnate_long && !mFreeSpheres[i].falling)
        {
            mFreeSpheres[i].center += mTStep * mFreeSpheres[i].speed;
        }
        else if (mFreeSpheres[i].falling)
        {
            mFreeSpheres[i].center.z = mSmokeLayers[mFreeSpheres[i].closest_layer_idx].center.z;
        }
        //update_spheres_on_free_sphere(i);
    }

    // make begin_falling layers falling
    for (int i = mSmokeLayers.size() - 1; i >= 0; i--)
    {
        if (mSmokeLayers[i].begin_falling && mSmokeLayers.size() >= 1)
        {
            mSmokeLayers[i].falling = true;
            mSmokeLayers[i].begin_falling = false;
            mSmokeLayers[i].rising = false;
            mSmokeLayers[i].plume = false;
            //mSmokeLayers[i].center.z = -2000;
        }
    }
}


//------------------------------------------------------------
//---------------------- STAGNATION --------------------------
//------------------------------------------------------------ */

void Plume::update_stagnation_spheres(float3 wind)
{
    for (int i = mFreeSpheres.size() - 1; i >= 0; i--)
    {
        if (mFreeSpheres[i].stagnate_long)
        {
            // closest layer
            unsigned int closest_layer_id = mFreeSpheres[i].closest_layer_idx;

            // get radial composant relative to layer center
            float3 p_radial = float3(mFreeSpheres[i].center.x, mFreeSpheres[i].center.y, 0);

            // update rotation axis with new radial vector (can change upon time because axis changes)
            mFreeSpheres[i].angle_vector = normalize(p_radial);

            // update radial speed and position by adding perturbation
            float speed_factor = length(float3(mFreeSpheres[i].center.x, mFreeSpheres[i].center.y, 0)) / 2000.;
            mFreeSpheres[i].perturbation = mStagnationSpeed / speed_factor;
            if (length(wind) != 0)
            {
                speed_factor = length(p_radial) / 2000.;
                mFreeSpheres[i].perturbation = mStagnationSpeed / speed_factor;
                if (dot(mFreeSpheres[i].speed, p_radial) < 0) mFreeSpheres[i].perturbation = 0;
            }

            //mFreeSpheres[i].angle_vector = normalize(float3(mFreeSpheres[i].angle_vector.x, mFreeSpheres[i].angle_vector.y, 0));

            // update
            mFreeSpheres[i].speed = 0.f * mFreeSpheres[i].speed + 1 * (mFreeSpheres[i].perturbation) * mFreeSpheres[i].angle_vector;

            float dxy = length(mFreeSpheres[i].speed) * mTStep;
            float3 relative_pos_at_max_alt = mFreeSpheres[i].center_at_max_altitude - mFreeSpheres[i].layer_center_at_max_altitude;
            float relative_xy_at_max_alt = sqrt(relative_pos_at_max_alt.x * relative_pos_at_max_alt.x + relative_pos_at_max_alt.y * relative_pos_at_max_alt.y);
            float a = (mFreeSpheres[i].max_altitude - mFreeSpheres[i].stagnation_altitude) * relative_xy_at_max_alt;

            float3 relative_pos = mFreeSpheres[i].center - mFreeSpheres[i].layer_center_at_max_altitude;
            float relative_xy = sqrt(relative_pos.x * relative_pos.x + relative_pos.y * relative_pos.y);
            float dz = -a * dxy / ((relative_xy) * (relative_xy));
            if (relative_xy < relative_xy_at_max_alt) dz = 0;
            float new_z = mFreeSpheres[i].stagnation_altitude + a / (relative_xy);
            if (relative_xy <= relative_xy_at_max_alt) new_z = mFreeSpheres[i].max_altitude;

            mFreeSpheres[i].center.z = new_z;
            mFreeSpheres[i].angular_speed = 0;
            mFreeSpheres[i].rho = mSmokeLayers[closest_layer_id].rho;

            mFreeSpheres[i].speed += wind;
        }
    }
}

void Plume::update_stagnation_spheres_position()
{
    // update positions
    for (unsigned int i = 0; i < mFreeSpheres.size(); i++)
    {
        if (mFreeSpheres[i].stagnate_long)
        {
            mFreeSpheres[i].center += mTStep * mFreeSpheres[i].speed;
            //update_spheres_on_stagnation_sphere(i);
            if (mFreeSpheres[i].closest_layer_idx != -1)
            {
                float3 relative_position = mFreeSpheres[i].center - mSmokeLayers[mFreeSpheres[i].closest_layer_idx].center;
                mFreeSpheres[i].relative_distance = length(relative_position);
                mFreeSpheres[i].angle_vector = normalize(relative_position);
            }
        }
    }
}

