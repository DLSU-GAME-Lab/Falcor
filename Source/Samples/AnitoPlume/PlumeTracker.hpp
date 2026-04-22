#pragma once

#include "Plume.hpp"
#include <unordered_map>
#include <string>
#include <vector>

class PlumeTracker
{
private:
	struct TrackerData
	{
		std::vector<float3> positions;
		std::vector<float> radii;
		float maxRadius;

		void setData(unsigned int index, unsigned int maxSize, float3 position, float radius = 0.0f);
		void reset();
	};

	std::vector<TrackerData> mData;
	std::vector<std::string> mLocationNames;
	std::vector<float> mArcStart;
	std::vector<float> mArcEnd;

	const int mStepSize = 10;
	const float mMinAltStep = 100.0f;
	float mAltStep = 1000.0f;

	float mWindAngle = 0.0f;
	float3 mWindVector = { 0.0f, 0.0f, 0.0f };

private:
	PlumeTracker();
	~PlumeTracker();
	PlumeTracker(const PlumeTracker&) {};
	PlumeTracker operator=(const PlumeTracker&) {};
	static PlumeTracker* sharedInstance;

public:
	static PlumeTracker* getInstance();
	static void initialize();
	static void destroy();

	void addTrackerData();
	void checkSmokePosition(Plume* plume);
	void resetPlumePositions();
	void loadData(std::string filePath);

	void setWindDirection(float3 wind_vector);

	unsigned int getDataCount();
	std::vector<std::string>& getLocationNames();
	std::vector<float3>& getPositions(unsigned int plumeID);
	std::vector<float>& getRadii(unsigned int plumeID);
	float getConeRadius() const;
	std::vector<std::string> getIntersectingLocations();
	std::vector<std::string> getIntersectingLocations(float coneRadius, float angle = -1.0f);
	float3 getWindDirection() const;
	float getWindDirectionAngle() const;
    float vectorToAngle(const float3& v);

};
