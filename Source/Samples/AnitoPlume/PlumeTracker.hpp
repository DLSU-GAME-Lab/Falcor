#pragma once

#include "vcl/vcl.hpp"
#include "scenes/sources/smoke/Plume.hpp"
#include <unordered_map>
#include <string>
#include <vector>

class PlumeTracker
{
private:
	struct TrackerData
	{
		std::vector<vcl::vec3> positions;
		std::vector<float> radii;
		float maxRadius;

		void setData(unsigned int index, unsigned int maxSize, vcl::vec3 position, float radius = 0.0f);
		void reset();
	};

	std::vector<TrackerData> data;
	std::vector<std::string> locationNames;
	std::vector<float> arcStart;
	std::vector<float> arcEnd;

	const int stepSize = 10;
	const float minAltStep = 100.0f;
	float altStep = 1000.0f;

	float windAngle = 0.0f;
	vcl::vec3 windVector = { 0.0f, 0.0f, 0.0f };

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

	void setWindDirection(vcl::vec3 wind_vector);

	unsigned int getDataCount();
	std::vector<std::string>& getLocationNames();
	std::vector<vcl::vec3>& getPositions(unsigned int plumeID);
	std::vector<float>& getRadii(unsigned int plumeID);
	float getConeRadius() const;
	std::vector<std::string> getIntersectingLocations();
	std::vector<std::string> getIntersectingLocations(float coneRadius, float angle = -1.0f);
	vcl::vec3 getWindDirection() const;
	float getWindDirectionAngle() const;

};