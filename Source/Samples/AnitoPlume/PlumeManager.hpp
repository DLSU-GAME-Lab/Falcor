#pragma once
#include "vector"
#include "unordered_map"
#include "string"
#include "Plume.hpp"

enum class SimulatorState { Stopped, Playing, Paused };

class PlumeManager
{
private:
    SimulatorState mState;
	float mTStep;
	unsigned int mFrameCount;

	std::vector<Plume> mPlumes;
	std::vector<Plume*> mSortedPlumes;
	std::vector<bool> mToUpdate;
	std::vector<int> mWindAltitudes;
	std::vector<WindStructure> mWinds;
	TerrainStructure mTerrainStruct;

	float mMinAltitude;
	float mMaxAltitude;
	float mAltitudeStep;
	int mAltitudeSize;

	std::vector<int> mDegAngle; // UI wind angles
	bool mAllAngles; // UI toggle

public:
	static PlumeManager* getInstance();
	static void initialize();
	static void destroy();

public:
	void createPlume(unsigned int id, std::string ventName, float3 ventLoc, EruptionParams eruptParams);
	void setupTransitionValues(int maxSmoke, float transitionSpeed, float transitionDelay);
     void setupTerrainStruct(std::vector<float3>& position, std::vector<float3>& normal, Vao terrain, float4x4 transform);
	void update(float dt);

	bool getToUpdate(unsigned int plumeID);
	void setToUpdate(bool toUpdate);
	void setToUpdate(unsigned int plumeID, bool toUpdate);

	void setTimerScale(float scale);
	void playSimulation();
	void pauseSimulation();
	void stopSimulation();
	void reset();
	void sortNearestPlumes(float3 camPos);

	SimulatorState getState() const;
	std::vector<Plume>& getPlumes();
	std::vector<Plume*>& getSortedPlumes();
	Plume& getPlume(unsigned int plumeID);

public:
	void setWindIntensity(unsigned int index, int intensity);
	void setWindAngle(unsigned int index, int angle);
	void setWind(unsigned int index, int intensity, int angle);

	void setLinearWind(float linearWindBase);
	void setAllWindIntensities(int intensity);
	void setAllWindAngles(int angle);
	void setAllWinds(int intensity, int angle);

	float3 computeWindVector(float height);
	float3 getAverageWindDirection();
	float getAverageWindAngle();
	std::vector<int>& getWindAlts();
	std::vector<WindStructure>& getWinds();
	std::vector<int>& getDegAngle();
	float getMaxAlt();
	float getAltStep();
	int getAltSize();
	unsigned int getSmokeLayersCount();
	unsigned int getFreeSphereCount();
	unsigned int getSubsphereCount();
    float vectorToAngle(const float3& v);
        //singleton Stuff
private:
	PlumeManager();
	PlumeManager(const PlumeManager&) {};
	PlumeManager operator=(const PlumeManager&) {};
	static PlumeManager* sharedInstance;
};

