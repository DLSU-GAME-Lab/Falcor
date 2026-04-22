#include "PlumeManager.hpp"
#include "PlumeTracker.hpp"

#include <algorithm>

PlumeManager* PlumeManager::sharedInstance = nullptr;

PlumeManager::PlumeManager()
{
	mState = SimulatorState::Stopped;
	mTStep = 0.0f;
	mFrameCount = 0;

	mAllAngles = false;
	mMinAltitude = 0;
	mMaxAltitude = 10000;
	mAltitudeStep = 2000;
	mAltitudeSize = int(mMaxAltitude / mAltitudeStep) + 1;
	for (unsigned int i = 0; i < (unsigned int)mAltitudeSize; i++)
	{
		mWindAltitudes.push_back(i * mAltitudeStep);
		mWinds.push_back(WindStructure(0, 0));
		this->mDegAngle.push_back(0);
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

void PlumeManager::createPlume(unsigned int id, std::string ventName, float3 ventLoc,EruptionParams eruptParams)
{
	this->mPlumes.push_back(Plume(id, ventName, ventLoc, eruptParams));
	this->mToUpdate.push_back(false);
	PlumeTracker::getInstance()->addTrackerData();

	this->mSortedPlumes.clear();
	for (int i = 0; i < this->mPlumes.size(); i++)
	{
		this->mSortedPlumes.push_back(&this->mPlumes[i]);
	}

}
void PlumeManager::setupTransitionValues(int dMaxSmoke, float fTransitionSpeed, float fTransitionDelay)
{
	for (int i = 0; i < this->mPlumes.size(); i++)
	{
		this->mPlumes[i].setMaxSmoke(dMaxSmoke);
		this->mPlumes[i].setTransitionSpeed(fTransitionSpeed);
		this->mPlumes[i].setTranstionDelay(fTransitionDelay);
		for (int j = 0; j < dMaxSmoke; j++)
		{
			this->mPlumes[i].getTransitionLifetime().push_back(this->mPlumes[i].getTransitionDelay() * j);

		}

	}
}

void PlumeManager::setupTerrainStruct(std::vector<float3>& position, std::vector<float3>& normal, Vao terrain, float4x4 transform)
{
	this->mTerrainStruct.fill_height_field(position, normal, terrain, transform);
}

void PlumeManager::update(float dt)
{

    float scale = 1.0f; // adjust if needed
    if(dt <= 1e-6f)
        mTStep = 0.0f;
    else
        mTStep = scale * 0.002f;


	for (int i = 0; i < this->mPlumes.size(); i++)
	{
		if (this->mToUpdate[i])
		{
			this->mPlumes[i].set_t_step(mTStep);
			//this->mPlumes[i].remove_colliding_smoke();
			this->mPlumes[i].remove_smoke_layers();
			this->mPlumes[i].update_smoke_layer_init();
		}
	}

	for (unsigned int nb_steps_per_frame = 0; nb_steps_per_frame < 10; nb_steps_per_frame++)
	{
		for (int i = 0; i < this->mPlumes.size(); i++)
		{
			if (this->mToUpdate[i])
			{
				// update of layer and spheres
				for (unsigned int id = 0; id < this->mPlumes[i].mSmokeLayers.size(); id++)
				{
					this->mPlumes[i].smoke_layer_update(id, computeWindVector(this->mPlumes[i].mSmokeLayers[id].center.z));
				}

				this->mPlumes[i].update_free_spheres();
				if (mFrameCount % 100 == 0) this->mPlumes[i].falling_spheres_update(mTerrainStruct, 100);

				for (int id = this->mPlumes[i].mFreeSpheres.size() - 1; id >= 0; id--)
				{
					this->mPlumes[i].update_stagnation_spheres(computeWindVector(this->mPlumes[i].mFreeSpheres[id].center.z));
				}
				this->mPlumes[i].update_stagnation_spheres_position();

				// update subspheres
				//if (mFrameCount %50 == 0) update_subspheres_params();

				// export (comment or uncomment)
				//if (export_data && mFrameCount % 50 == 0) export_spheres();

				//// store data for replay
				//if (!export_data && mFrameCount %50 == 0)
				//{
				//    smoke_layers_frames.push_back(mSmokeLayers);
				//    free_spheres_frames.push_back(mFreeSpheres);
				//    stagnate_spheres_frames.push_back(stagnate_spheres);
				//    falling_spheres_frames.push_back(mFallingSpheres);
				//    falling_spheres_buffers_frames.push_back(mFallingSpheresBuffers);
				//}
			}
		}
		mFrameCount++;
	}

	for (int i = 0; i < this->mPlumes.size(); i++)
	{
		if (this->mToUpdate[i]) PlumeTracker::getInstance()->checkSmokePosition(&this->mPlumes[i]);
	}

}

bool PlumeManager::getToUpdate(unsigned int plumeID)
{
	if (plumeID >= this->mToUpdate.size()) return false;
	return this->mToUpdate[plumeID];
}

void PlumeManager::setToUpdate(bool ToUpdate)
{
	for (int i = 0; i < this->mToUpdate.size(); i++)
		this->mToUpdate[i] = ToUpdate;
}

void PlumeManager::setToUpdate(unsigned int plumeID, bool toUpdate)
{
	if (plumeID >= this->mToUpdate.size()) return;
	this->mToUpdate[plumeID] = toUpdate;
}


void PlumeManager::playSimulation()
{
	if (mState == SimulatorState::Stopped)
		this->reset();
     // find way to fix timer to not rely on vcl::timer
	/*timer.start();*/
	mState = SimulatorState::Playing;
}

void PlumeManager::pauseSimulation()
{
    // find way to fix timer to not rely on vcl::timer
	/*timer.stop();*/
	mState = SimulatorState::Paused;
}

void PlumeManager::stopSimulation()
{
    // find way to fix timer to not rely on vcl::timer
	/*timer.stop();*/
	mFrameCount = 0;
	mState = SimulatorState::Stopped;
	PlumeTracker::getInstance()->resetPlumePositions();
	this->reset();
}

void PlumeManager::reset()
{
	for (int i = 0; i < this->mPlumes.size(); i++)
	{
		this->mPlumes[i].reset();
	}
}

void PlumeManager::sortNearestPlumes(float3 camPos)
{
	std::sort(this->mSortedPlumes.begin(), this->mSortedPlumes.end(),
		[camPos](Plume* a, Plume* b)
		{
            float3 aDist = a->getPosition() - camPos;
            float3 bDist = b->getPosition() - camPos;
			float distA = dot(aDist, aDist);
			float distB = dot(bDist, bDist);
			return distA > distB;
		});
}

SimulatorState PlumeManager::getState() const
{
	return this->mState;
}

std::vector<Plume>& PlumeManager::getPlumes()
{
	return this->mPlumes;
}

std::vector<Plume*>& PlumeManager::getSortedPlumes()
{
	return this->mSortedPlumes;
}

Plume& PlumeManager::getPlume(unsigned int plumeID)
{
	return this->mPlumes[plumeID];
}

void PlumeManager::setLinearWind(float linearWindBase)
{
	for (unsigned int i = 0; i < mWinds.size(); i++)
	{		
		if (i > 3) mWinds[i].intensity = 3 * linearWindBase;
		else mWinds[i].intensity = i * linearWindBase;

		if (mWinds[i].intensity == 0) mWinds[i].intensity = 1;
		mWinds[i] = WindStructure(mWinds[i].intensity, mDegAngle[i]);
		mWinds[i].recalcWindVector();
	}
}

void PlumeManager::setWindIntensity(unsigned int index, int intensity)
{
	if (index >= mWinds.size()) return;

	mWinds[index] = WindStructure(intensity, mDegAngle[index]);
	mWinds[index].recalcWindVector();
}

void PlumeManager::setWindAngle(unsigned int index, int angle)
{
	if (index >= mWinds.size()) return;

	mDegAngle[index] = angle;
	mWinds[index] = WindStructure(mWinds[index].intensity, mDegAngle[index]);
	mWinds[index].recalcWindVector();
}

void PlumeManager::setWind(unsigned int index, int intensity, int angle)
{
	mDegAngle[index] = angle;
	mWinds[index] = WindStructure(intensity, mDegAngle[index]);
	mWinds[index].recalcWindVector();
}

void PlumeManager::setAllWindIntensities(int intensity)
{
	for (unsigned int i = 0; i < mWinds.size(); i++)
	{
		setWindIntensity(i, intensity);
	}
}

void PlumeManager::setAllWindAngles(int angle)
{
	for (int i = 0; i < mWinds.size(); i++)
	{
		setWindAngle(i, angle);
	}
}

void PlumeManager::setAllWinds(int intensity, int angle)
{
	for (int i = 0; i < mWinds.size(); i++)
	{
		setWind(i, intensity, angle);
	}
}

float3 PlumeManager::computeWindVector(float height)
{
	// find altitude interval
	unsigned int low_altitude_idx = 0;
	for (unsigned int i = 0; i < mWindAltitudes.size(); i++)
	{
		if (mWindAltitudes[i] < height)
		{
			low_altitude_idx = i;
		}
	}

	// compute wind vec by interpolating
	if (low_altitude_idx == mWindAltitudes.size() - 1)
	{
		return mWinds[low_altitude_idx].windVector;
	}
	else
	{
		float low_height = (float)mWindAltitudes[low_altitude_idx];
		float high_height = (float)mWindAltitudes[low_altitude_idx + 1];
		float lambda = (height - low_height) / (high_height - low_height);
		float3 interpo_wind = mWinds[low_altitude_idx].windVector + lambda * (mWinds[low_altitude_idx + 1].windVector - mWinds[low_altitude_idx].windVector);
		return interpo_wind;
	}
}

float3 PlumeManager::getAverageWindDirection()
{
	float3 mWinds_vec = { 0,0,0 };
	for (int i = 0; i < mWinds.size(); i++)
	{
		mWinds_vec += mWinds[i].windVector;
	}

	float mWinds_squared_x = mWinds_vec.x * mWinds_vec.x;
	float mWinds_squared_y = mWinds_vec.y * mWinds_vec.y;
	float mWinds_squared_z = mWinds_vec.z * mWinds_vec.z;

	float mag = sqrt(mWinds_squared_x + mWinds_squared_y + mWinds_squared_z);
	float3 avg_wind_direction = { 0, 0, 0 };
	if (mag != 0) avg_wind_direction = float3(mWinds_vec.x, mWinds_vec.y, mWinds_vec.z) / mag;
	return avg_wind_direction;
}

float PlumeManager::getAverageWindAngle()
{
	float3 windDir = getAverageWindDirection();
	float windAngle = -1.0f;

	if (windDir.x != 0 || windDir.y != 0 || windDir.z != 0)
		windAngle = vectorToAngle(windDir);
	return windAngle;
}

std::vector<int>& PlumeManager::getWindAlts()
{
	return this->mWindAltitudes;
}

std::vector<WindStructure>& PlumeManager::getWinds()
{
	return this->mWinds;
}

std::vector<int>& PlumeManager::getDegAngle()
{
	return this->mDegAngle;
}

float PlumeManager::getMaxAlt()
{
	return this->mMaxAltitude;
}

float PlumeManager::getAltStep()
{
	return this->mAltitudeStep;
}

int PlumeManager::getAltSize()
{
	return this->mAltitudeSize;
}

unsigned int PlumeManager::getSmokeLayersCount()
{
	unsigned int smokeLayersCount = 0;
	for (int i = 0; i < this->mPlumes.size(); i++)
	{
		smokeLayersCount += this->mPlumes[i].mSmokeLayers.size();
	}
	return smokeLayersCount;
}

unsigned int PlumeManager::getFreeSphereCount()
{
	unsigned int totalSphereCount = 0;
	for (int i = 0; i < this->mPlumes.size(); i++)
	{
		totalSphereCount += this->mPlumes[i].mFreeSpheres.size();
		totalSphereCount += this->mPlumes[i].mFallingSpheres.size();

		for (int j = 0; j < this->mPlumes[i].mFallingSpheresBuffers.size(); j++)
		{
			totalSphereCount += this->mPlumes[i].mFallingSpheresBuffers[j].size();
		}
	}

	return totalSphereCount;
}

unsigned int PlumeManager::getSubsphereCount()
{
	unsigned int totalSphereCount = 0;
	for (int i = 0; i < this->mPlumes.size(); i++)
	{
		totalSphereCount += this->mPlumes[i].mS2Spheres.size();
		totalSphereCount += this->mPlumes[i].mS3Spheres.size();
	}

	return totalSphereCount;
}

float PlumeManager::vectorToAngle(const float3& v)
{
    float radians = atan2(v.y, v.x);
    radians = (radians < 0.0f) ? radians + 2.0f * 3.14159265f : radians;
    return radians * (180.0f / 3.14159265f); 
}
