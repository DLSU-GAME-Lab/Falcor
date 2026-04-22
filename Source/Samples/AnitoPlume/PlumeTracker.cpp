#include "PlumeTracker.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

PlumeTracker* PlumeTracker::sharedInstance = nullptr;

PlumeTracker::PlumeTracker()
{
    
}

PlumeTracker::~PlumeTracker()
{

}

PlumeTracker* PlumeTracker::getInstance()
{
    return sharedInstance;
}

void PlumeTracker::initialize()
{
    sharedInstance = new PlumeTracker();
}

void PlumeTracker::destroy()
{
    delete sharedInstance;
}

void PlumeTracker::addTrackerData()
{
    this->mData.push_back(TrackerData());
}

void PlumeTracker::loadData(std::string filePath)
{
    std::ifstream file;
    file.open(filePath);
    if (file.is_open())
    {
        for (std::string line; std::getline(file, line);)
        {
            std::istringstream ss(std::move(line));
            std::vector<std::string> cell;
            for (std::string value; std::getline(ss, value, ',');)
                cell.push_back(std::move(value));

            this->mLocationNames.push_back(cell[0]);
            this->mArcStart.push_back(std::atof(cell[1].c_str()));
            this->mArcEnd.push_back(std::atof(cell[2].c_str()));
        }
    }
    else std::cout << "[PlumeTracker] ERROR: File with path " << filePath << " could not be opened." << std::endl;

    file.close();
}

std::vector<std::string> PlumeTracker::getIntersectingLocations(float coneRadius, float angle)
{
    std::vector<std::string> locNames;
    if (angle < 0.0f)
    {
        angle = this->mWindAngle;
        if (this->mWindAngle < 0.0f) return locNames;
    }

    float lowAngle = angle - coneRadius;
    float hiAngle = angle + coneRadius;

    for (int i = 0; i < this->mArcStart.size(); i++)
    {
        float locStart = this->mArcStart[i];
        float locEnd = this->mArcEnd[i];

        if (locStart > locEnd)
        {
            if (angle <= 180) locStart -= 360.0f;
            else if (angle > 180) locEnd += 360.0f;
        }

        if ((locStart <= lowAngle || locStart <= hiAngle) &&
            (locEnd >= lowAngle || locEnd >= hiAngle))
        {
            locNames.push_back(this->mLocationNames[i]);
        }
    }

    return locNames;
}

std::vector<std::string> PlumeTracker::getIntersectingLocations()
{
    return getIntersectingLocations(mWindAngle, getConeRadius());
}

void PlumeTracker::checkSmokePosition(Plume* plume)
{
    int smokeSize = plume->mSmokeLayers.size();
    int sub = smokeSize / mStepSize;
    if (smokeSize < mStepSize) return;
    for (int i = smokeSize - 1; i >= 0; i -= sub)
    {
        int index = (smokeSize - (i + 1)) / sub;
        mData[plume->getID()].setData(index, mStepSize, plume->mSmokeLayers[i].center, plume->mSmokeLayers[i].r);
    }
}

unsigned int PlumeTracker::getDataCount()
{
    return mData.size();
}

std::vector<std::string>& PlumeTracker::getLocationNames()
{
    return this->mLocationNames;
}

std::vector<float3>& PlumeTracker::getPositions(unsigned int plumeID)
{
    return mData[plumeID].positions;
}

std::vector<float>& PlumeTracker::getRadii(unsigned int plumeID)
{
    return mData[plumeID].radii;
}

float PlumeTracker::getConeRadius() const
{
    float coneRadius = 0.0f;
    for (int i = 0; i < mData.size(); i++) coneRadius += mData[i].maxRadius;
    return coneRadius;
}

void PlumeTracker::resetPlumePositions()
{
    for (int i = 0; i < mData.size(); i++) mData[i].reset();
}

void PlumeTracker::setWindDirection(float3 windVector)
{
    this->mWindVector = windVector;
    if (windVector.x == 0 && windVector.y == 0 && windVector.z == 0)
        this->mWindAngle = -1.0f;
    else this->mWindAngle = vectorToAngle(windVector);
}

void PlumeTracker::TrackerData::setData(unsigned int index, unsigned int maxSize, float3 position, float radius)
{
    if (index == this->positions.size())
    {
        this->positions.push_back(position);
        this->radii.push_back(radius);
        this->maxRadius = radii[radii.size() - 1];
    }
    else if (index < this->positions.size())
    {
        this->positions[index] = position;
        this->radii[index] = radius;
        this->maxRadius = radii[radii.size() - 1];
    }
}

void PlumeTracker::TrackerData::reset()
{
    this->positions.clear();
    this->radii.clear();
    this->maxRadius = 0.0f;
}

float3 PlumeTracker::getWindDirection() const
{
    return this->mWindVector;
}

float PlumeTracker::getWindDirectionAngle() const
{
    return this->mWindAngle;
}
float PlumeTracker::vectorToAngle(const float3& v)
{
    float radians = atan2(v.y, v.x);
    radians = (radians < 0.0f) ? radians + 2.0f * 3.14159265f : radians;
    return radians * (180.0f / 3.14159265f);
}
