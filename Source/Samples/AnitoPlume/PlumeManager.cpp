#include "PlumeManager.hpp"
#include "PlumeTracker.hpp"
#include <algorithm>

PlumeManager* PlumeManager::sharedInstance = nullptr;

PlumeManager::PlumeManager()
{
	timer.stop();
	state = SimulatorState::Stopped;
	t_step = 0.0f;
	frame_count = 0;

	all_angles = false;
	min_altitude = 0;
	max_altitude = 10000;
	altitude_step = 2000;
	altitude_size = int(max_altitude / altitude_step) + 1;
	for (unsigned int i = 0; i < altitude_size; i++)
	{
		wind_altitudes.push_back(i * altitude_step);
		winds.push_back(wind_structure(0, 0));
		this->deg_angle.push_back(0);
	}
}
void PlumeManager::initialize()
{
	if (sharedInstance == nullptr)
		sharedInstance = new PlumeManager();
}
void PlumeManager::destroy()
{
	delete sharedInstance;
}
PlumeManager* PlumeManager::getInstance()
{
	return sharedInstance;
}

void PlumeManager::createPlume(unsigned int id, std::string ventName, vcl::vec3 ventLoc,EruptionParams eruptParams)
{
	this->plumes.push_back(Plume(id, ventName, ventLoc, eruptParams));
	this->toUpdate.push_back(false);
	PlumeTracker::getInstance()->addTrackerData();

	this->sortedPlumes.clear();
	for (int i = 0; i < this->plumes.size(); i++)
	{
		this->sortedPlumes.push_back(&this->plumes[i]);
	}

}
void PlumeManager::setupTransitionValues(int dMaxSmoke, float fTransitionSpeed, float fTransitionDelay)
{
	for (int i = 0; i < this->plumes.size(); i++)
	{
		this->plumes[i].setMaxSmoke(dMaxSmoke);
		this->plumes[i].setTransitionSpeed(fTransitionSpeed);
		this->plumes[i].setTranstionDelay(fTransitionDelay);
		for (int j = 0; j < dMaxSmoke; j++)
		{
			this->plumes[i].getTransitionLifetime().push_back(this->plumes[i].getTransitionDelay() * j);

		}

	}
}

void PlumeManager::setupTerrainStruct(vcl::buffer<vcl::vec3>& position, vcl::buffer<vcl::vec3>& normal, vcl::mesh_drawable terrain)
{
	this->terrain_struct.fill_height_field(position, normal, terrain);
}

void PlumeManager::update()
{
	// Force constant time step
	float dt = timer.update();
	t_step = dt <= 1e-6f ? 0.0f : timer.scale * 0.002f; //0.0003f

	for (int i = 0; i < this->plumes.size(); i++)
	{
		if (this->toUpdate[i])
		{
			this->plumes[i].set_t_step(t_step);
			//this->plumes[i].remove_colliding_smoke();
			this->plumes[i].remove_smoke_layers();
			this->plumes[i].update_smoke_layer_init();
		}
	}

	for (unsigned int nb_steps_per_frame = 0; nb_steps_per_frame < 10; nb_steps_per_frame++)
	{
		for (int i = 0; i < this->plumes.size(); i++)
		{
			if (this->toUpdate[i])
			{
				// update of layer and spheres
				for (unsigned int id = 0; id < this->plumes[i].smoke_layers.size(); id++)
				{
					this->plumes[i].smoke_layer_update(id, computeWindVector(this->plumes[i].smoke_layers[id].center.z));
				}

				this->plumes[i].update_free_spheres();
				if (frame_count % 100 == 0) this->plumes[i].falling_spheres_update(terrain_struct, 100);

				for (int id = this->plumes[i].free_spheres.size() - 1; id >= 0; id--)
				{
					this->plumes[i].update_stagnation_spheres(computeWindVector(this->plumes[i].free_spheres[id].center.z));
				}
				this->plumes[i].update_stagnation_spheres_position();

				// update subspheres
				//if (frame_count %50 == 0) update_subspheres_params();

				// export (comment or uncomment)
				//if (export_data && frame_count % 50 == 0) export_spheres();

				//// store data for replay
				//if (!export_data && frame_count %50 == 0)
				//{
				//    smoke_layers_frames.push_back(smoke_layers);
				//    free_spheres_frames.push_back(free_spheres);
				//    stagnate_spheres_frames.push_back(stagnate_spheres);
				//    falling_spheres_frames.push_back(falling_spheres);
				//    falling_spheres_buffers_frames.push_back(falling_spheres_buffers);
				//}
			}
		}
		frame_count++;
	}

	for (int i = 0; i < this->plumes.size(); i++)
	{
		if (this->toUpdate[i]) PlumeTracker::getInstance()->checkSmokePosition(&this->plumes[i]);
	}

}

bool PlumeManager::getToUpdate(unsigned int plumeID)
{
	if (plumeID >= this->toUpdate.size()) return false;
	return this->toUpdate[plumeID];
}

void PlumeManager::setToUpdate(bool toUpdate)
{
	for (int i = 0; i < this->toUpdate.size(); i++)
		this->toUpdate[i] = toUpdate;
}

void PlumeManager::setToUpdate(unsigned int plumeID, bool toUpdate)
{
	if (plumeID >= this->toUpdate.size()) return;
	this->toUpdate[plumeID] = toUpdate;
}

void PlumeManager::setTimerScale(float scale)
{
	timer.scale = scale;
}

void PlumeManager::playSimulation()
{
	if (state == SimulatorState::Stopped)
		this->reset();

	timer.start();
	state = SimulatorState::Playing;
}

void PlumeManager::pauseSimulation()
{
	timer.stop();
	state = SimulatorState::Paused;
}

void PlumeManager::stopSimulation()
{
	timer.stop();
	frame_count = 0;
	state = SimulatorState::Stopped;
	PlumeTracker::getInstance()->resetPlumePositions();
	this->reset();
}

void PlumeManager::reset()
{
	for (int i = 0; i < this->plumes.size(); i++)
	{
		this->plumes[i].reset();
	}
}

void PlumeManager::sortNearestPlumes(vcl::vec3 camPos)
{
	std::sort(this->sortedPlumes.begin(), this->sortedPlumes.end(),
		[camPos](Plume* a, Plume* b)
		{
			float distA = vcl::sqr_mag(a->getPosition() - camPos);
			float distB = vcl::sqr_mag(b->getPosition() - camPos);
			return distA > distB;
		});
}

SimulatorState PlumeManager::getState() const
{
	return this->state;
}

std::vector<Plume>& PlumeManager::getPlumes()
{
	return this->plumes;
}

std::vector<Plume*>& PlumeManager::getSortedPlumes()
{
	return this->sortedPlumes;
}

Plume& PlumeManager::getPlume(unsigned int plumeID)
{
	return this->plumes[plumeID];
}

void PlumeManager::setLinearWind(float linearWindBase)
{
	for (unsigned int i = 0; i < winds.size(); i++)
	{		
		if (i > 3) winds[i].intensity = 3 * linearWindBase;
		else winds[i].intensity = i * linearWindBase;

		if (winds[i].intensity == 0) winds[i].intensity = 1;
		winds[i] = wind_structure(winds[i].intensity, deg_angle[i]);
		winds[i].recalc_wind_vector();
	}
}

void PlumeManager::setWindIntensity(unsigned int index, int intensity)
{
	if (index >= winds.size()) return;

	winds[index] = wind_structure(intensity, deg_angle[index]);
	winds[index].recalc_wind_vector();
}

void PlumeManager::setWindAngle(unsigned int index, int angle)
{
	if (index >= winds.size()) return;

	deg_angle[index] = angle;
	winds[index] = wind_structure(winds[index].intensity, deg_angle[index]);
	winds[index].recalc_wind_vector();
}

void PlumeManager::setWind(unsigned int index, int intensity, int angle)
{
	deg_angle[index] = angle;
	winds[index] = wind_structure(intensity, deg_angle[index]);
	winds[index].recalc_wind_vector();
}

void PlumeManager::setAllWindIntensities(int intensity)
{
	for (unsigned int i = 0; i < winds.size(); i++)
	{
		setWindIntensity(i, intensity);
	}
}

void PlumeManager::setAllWindAngles(int angle)
{
	for (int i = 0; i < winds.size(); i++)
	{
		setWindAngle(i, angle);
	}
}

void PlumeManager::setAllWinds(int intensity, int angle)
{
	for (int i = 0; i < winds.size(); i++)
	{
		setWind(i, intensity, angle);
	}
}

vcl::vec3 PlumeManager::computeWindVector(float height)
{
	// find altitude interval
	unsigned int low_altitude_idx = 0;
	for (unsigned int i = 0; i < wind_altitudes.size(); i++)
	{
		if (wind_altitudes[i] < height)
		{
			low_altitude_idx = i;
		}
	}

	// compute wind vec by interpolating
	if (low_altitude_idx == wind_altitudes.size() - 1)
	{
		return winds[low_altitude_idx].wind_vector;
	}
	else
	{
		float low_height = (float)wind_altitudes[low_altitude_idx];
		float high_height = (float)wind_altitudes[low_altitude_idx + 1];
		float lambda = (height - low_height) / (high_height - low_height);
		vcl::vec3 interpo_wind = winds[low_altitude_idx].wind_vector + lambda * (winds[low_altitude_idx + 1].wind_vector - winds[low_altitude_idx].wind_vector);
		return interpo_wind;
	}
}

vcl::vec3 PlumeManager::getAverageWindDirection()
{
	vcl::vec3 winds_vec = { 0,0,0 };
	for (int i = 0; i < winds.size(); i++)
	{
		winds_vec += winds[i].wind_vector;
	}

	float winds_squared_x = winds_vec.x * winds_vec.x;
	float winds_squared_y = winds_vec.y * winds_vec.y;
	float winds_squared_z = winds_vec.z * winds_vec.z;

	float mag = sqrt(winds_squared_x + winds_squared_y + winds_squared_z);
	vcl::vec3 avg_wind_direction = { 0, 0, 0 };
	if (mag != 0) avg_wind_direction = vcl::vec3(winds_vec.x, winds_vec.y, winds_vec.z) / mag;
	return avg_wind_direction;
}

float PlumeManager::getAverageWindAngle()
{
	vcl::vec3 windDir = getAverageWindDirection();
	float windAngle = -1.0f;

	if (windDir.x != 0 || windDir.y != 0 || windDir.z != 0)
		windAngle = vcl::vector_to_angle(windDir);
	return windAngle;
}

std::vector<int>& PlumeManager::getWindAlts()
{
	return this->wind_altitudes;
}

std::vector<wind_structure>& PlumeManager::getWinds()
{
	return this->winds;
}

std::vector<int>& PlumeManager::getDegAngle()
{
	return this->deg_angle;
}

float PlumeManager::getMaxAlt()
{
	return this->max_altitude;
}

float PlumeManager::getAltStep()
{
	return this->altitude_step;
}

int PlumeManager::getAltSize()
{
	return this->altitude_size;
}

unsigned int PlumeManager::getSmokeLayersCount()
{
	unsigned int smokeLayersCount = 0;
	for (int i = 0; i < this->plumes.size(); i++)
	{
		smokeLayersCount += this->plumes[i].smoke_layers.size();
	}
	return smokeLayersCount;
}

unsigned int PlumeManager::getFreeSphereCount()
{
	unsigned int totalSphereCount = 0;
	for (int i = 0; i < this->plumes.size(); i++)
	{
		totalSphereCount += this->plumes[i].free_spheres.size();
		totalSphereCount += this->plumes[i].falling_spheres.size();

		for (int j = 0; j < this->plumes[i].falling_spheres_buffers.size(); j++)
		{
			totalSphereCount += this->plumes[i].falling_spheres_buffers[j].size();
		}
	}

	return totalSphereCount;
}

unsigned int PlumeManager::getSubsphereCount()
{
	unsigned int totalSphereCount = 0;
	for (int i = 0; i < this->plumes.size(); i++)
	{
		totalSphereCount += this->plumes[i].s2_spheres.size();
		totalSphereCount += this->plumes[i].s3_spheres.size();
	}

	return totalSphereCount;
}
