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
    this->data.push_back(TrackerData());
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

            this->locationNames.push_back(cell[0]);
            this->arcStart.push_back(std::atof(cell[1].c_str()));
            this->arcEnd.push_back(std::atof(cell[2].c_str()));
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
        angle = this->windAngle;
        if (this->windAngle < 0.0f) return locNames;
    }

    float lowAngle = angle - coneRadius;
    float hiAngle = angle + coneRadius;

    for (int i = 0; i < this->arcStart.size(); i++)
    {
        float locStart = this->arcStart[i];
        float locEnd = this->arcEnd[i];

        if (locStart > locEnd)
        {
            if (angle <= 180) locStart -= 360.0f;
            else if (angle > 180) locEnd += 360.0f;
        }

        if ((locStart <= lowAngle || locStart <= hiAngle) &&
            (locEnd >= lowAngle || locEnd >= hiAngle))
        {
            locNames.push_back(this->locationNames[i]);
        }
    }

    return locNames;
}

std::vector<std::string> PlumeTracker::getIntersectingLocations()
{
    return getIntersectingLocations(windAngle, getConeRadius());
}

void PlumeTracker::checkSmokePosition(Plume* plume)
{
    int smokeSize = plume->smoke_layers.size();
    int sub = smokeSize / stepSize;
    if (smokeSize < stepSize) return;
    for (int i = smokeSize - 1; i >= 0; i -= sub)
    {
        int index = (smokeSize - (i + 1)) / sub;
        data[plume->getID()].setData(index, stepSize, plume->smoke_layers[i].center, plume->smoke_layers[i].r);
    }
}

unsigned int PlumeTracker::getDataCount()
{
    return data.size();
}

std::vector<std::string>& PlumeTracker::getLocationNames()
{
    return this->locationNames;
}

std::vector<vcl::vec3>& PlumeTracker::getPositions(unsigned int plumeID)
{
    return data[plumeID].positions;
}

std::vector<float>& PlumeTracker::getRadii(unsigned int plumeID)
{
    return data[plumeID].radii;
}

float PlumeTracker::getConeRadius() const
{
    float coneRadius = 0.0f;
    for (int i = 0; i < data.size(); i++) coneRadius += data[i].maxRadius;
    return coneRadius;
}

void PlumeTracker::resetPlumePositions()
{
    for (int i = 0; i < data.size(); i++) data[i].reset();
}

void PlumeTracker::setWindDirection(vcl::vec3 windVector)
{
    this->windVector = windVector;
    if (windVector.x == 0 && windVector.y == 0 && windVector.z == 0)
        this->windAngle = -1.0f;
    else this->windAngle = vcl::vector_to_angle(windVector);
}

void PlumeTracker::TrackerData::setData(unsigned int index, unsigned int maxSize, vcl::vec3 position, float radius)
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

vcl::vec3 PlumeTracker::getWindDirection() const
{
    return this->windVector;
}

float PlumeTracker::getWindDirectionAngle() const
{
    return this->windAngle;
}