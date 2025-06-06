// Copyright (c) 2025 CyberCortex Robotics SRL. All rights reserved
// Author: Sorin Mihai Grigorescu

#include "CLidarFilter.h"

#ifndef _WIN32
//#include "api/HesaiLidar/CLidarHesaiInterface.h"
//#include "api/SlamwareLidar/CLidarSlamwareInterface.h"
#endif

#define DYNALO_EXPORT_SYMBOLS
#include <dynalo/symbol_helper.hpp>

DYNALO_EXPORT CyC_FILTER_TYPE DYNALO_CALL getFilterType()
{
	CycDatablockKey key;
	return CLidarFilter(key).getFilterType();
}

DYNALO_EXPORT CCycFilterBase* DYNALO_CALL createFilter(const ConfigFilterParameters _params)
{
	return new CLidarFilter(_params);
}

static int StringToEnumType(const std::string& str_ctrl_name)
{
	if (str_ctrl_name.compare("sim") == 0)
		return Lidar_SIM_INTERFACE;
	else if (str_ctrl_name.compare("rp") == 0)
		return Lidar_Rp_INTERFACE;
	else if (str_ctrl_name.compare("hessai") == 0)
		return Lidar_Hessai_INTERFACE;
	else if (str_ctrl_name.compare("slamware") == 0)
		return Lidar_Slamware_INTERFACE;
	else if (str_ctrl_name == "lslidar")
		return Lidar_LSLidar_INTERFACE;

	return Lidar_SIM_INTERFACE;
}

CLidarFilter::CLidarFilter(CycDatablockKey key) :
	CCycFilterBase(key)
{
	setFilterType("CyC_LIDAR_FILTER_TYPE");
	m_OutputDataType = CyC_VOXELS;
}

CLidarFilter::CLidarFilter(const ConfigFilterParameters params) :
	CCycFilterBase(params)
{
	setFilterType("CyC_LIDAR_FILTER_TYPE");
	m_OutputDataType = CyC_VOXELS;

	if (!m_CustomParameters["interface"].empty())
	{
		m_InterfaceType = StringToEnumType(m_CustomParameters["interface"]);
	}
	else
	{
		spdlog::warn("Filter [{}-{}]: CLidarFilter: No lidar interface type defined. Switching to simulation.", getFilterKey().nCoreID, getFilterKey().nFilterID);
		m_InterfaceType = Lidar_SIM_INTERFACE;
	}

	// Load the sensor model
	if (!m_CustomParameters.at("CalibrationFile").empty())
	{
		this->m_pSensorModel = new CLidarSensorModel(fs::path(params.sGlobalBasePath) / m_CustomParameters.at("CalibrationFile"));
	}
}

CLidarFilter::~CLidarFilter()
{
	if (m_bIsEnabled)
		disable();
}

bool CLidarFilter::enable()
{
	if (!isNetworkFilter() && !isReplayFilter())
	{
		// Get the pose data filter
		m_pPoseDataFilter = CFilterUtils::getStateFilter(this->getInputSources());

		if (m_InterfaceType == Lidar_SIM_INTERFACE)
		{

		}
		else if (m_InterfaceType == Lidar_Hessai_INTERFACE)
        {
#ifdef GENIUS_BOARD
            m_HesaiLidarInterface = new CHesaiLidarInterface();
		    m_HesaiLidarInterface->enable();
		    spdlog::info("Hesai Lidar object interface declared");
#endif
        }
        else if (m_InterfaceType == Lidar_Slamware_INTERFACE)
        {
#ifdef GENIUS_BOARD
            m_SlamwareLidarInterface = new CLidarSlamwareInterface();
            if (m_SlamwareLidarInterface->connect())
            {
                spdlog::info("Slamware Lidar connected");
            }
            else
            {
                spdlog::error("Slamware Lidar error");
            }
#endif
        }
		else if (m_InterfaceType == Lidar_LSLidar_INTERFACE)
		{
			if (!m_lslidar.open(m_CustomParameters["port"]))
			{
				log_error("Failed to connect to LSLidar on port {}", m_CustomParameters["port"]);
				return false;
			}
			else
			{
				log_info("Connected to LSLidar!");
				if (!m_lslidar.init())
				{
					log_error("Failed to init LSLidar!");
					return false;
				}
				else
				{
					log_info("Initialized LSLidar!");
					m_thread = std::thread{ [this]() {
						while (isRunning() && m_lslidar.polling())
						{
							std::unique_lock lock{ m_mutex };
							m_lidar_points = m_lslidar.get();
						}
					} };
				}
			}
		}
	}

	spdlog::info("Filter [{}-{}]: CLidarFilter::enable() successful", getFilterKey().nCoreID, getFilterKey().nFilterID);

	m_bIsEnabled = true;
	return true;
}

bool CLidarFilter::disable()
{
	if (isRunning())
		stop();

	if (m_thread.joinable())
	{
		m_thread.join();
	}
	
	m_bIsEnabled = false;
	return true;
}

bool CLidarFilter::process()
{
	bool bReturn(false);
	const CLidarSensorModel* lidar_sensor_model = static_cast<CLidarSensorModel*>(this->m_pSensorModel);

	CPose pose;
	if (getLidarPose(pose))
		this->m_pSensorModel->updatePose(pose * this->m_pSensorModel->extrinsics());

	if (m_InterfaceType == Lidar_SIM_INTERFACE)
	{
		m_LidarVoxels.clear();

		CycVoxel vx1(3.f, 0.f, 0.f);
		CycVoxel vx2(3.f, -2.f, 0.f);
		CycVoxel vx3(4.f, 1.f, 0.f);
		m_LidarVoxels.push_back(vx1);
		m_LidarVoxels.push_back(vx2);
		m_LidarVoxels.push_back(vx3);

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		bReturn = true;
	}
	else if(m_InterfaceType == Lidar_Hessai_INTERFACE)
    {
		m_LidarVoxels.clear();

#ifdef GENIUS_BOARD
	    m_HesaiLidarInterface->getLidarOutputVoxels(m_LidarVoxels);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        bReturn = true;
#endif
    }
    else if (m_InterfaceType == Lidar_Slamware_INTERFACE)
    {
		m_LidarVoxels.clear();

#ifdef GENIUS_BOARD
        std::vector<lidar_point> points;
        if (m_SlamwareLidarInterface->process(points))
        {
            m_LidarVoxels.clear();
            for (const auto& point : points)
            {
                if (!point.valid)
                    continue;

                const float x = cosf(point.angle) * point.distance;
                const float y = sinf(point.angle) * point.distance;
                const float z = 0.F;

                m_LidarVoxels.emplace_back(x, y, z);
            }

            bReturn = true;
        }
#endif
    }
	else if (m_InterfaceType == Lidar_LSLidar_INTERFACE)
	{
		std::vector<ScanPoint> lidar_points;
		{
			std::scoped_lock lock{ m_mutex };
			lidar_points = m_lidar_points;
		}

		for (auto const& pt : lidar_points)
		{
			if (pt.range >= lidar_sensor_model->min_range() && pt.range <= lidar_sensor_model->max_range())
			{
				CycVoxel vx{ cosf(DEG2RAD * (float)pt.degree) * (float)pt.range, sinf(DEG2RAD * (float)pt.degree) * (float)pt.range, 0.f };
				m_LidarVoxels.emplace_back(vx);
			}
		}

		if (m_LidarVoxels.size() > lidar_sensor_model->num_points())
		{
			m_LidarVoxels.erase(
				m_LidarVoxels.begin(),
				std::next(m_LidarVoxels.begin(), m_LidarVoxels.size() - lidar_sensor_model->num_points()));
		}

		bReturn = !m_LidarVoxels.empty();
	}

	if (bReturn)
	{
		updateData(m_LidarVoxels);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	return bReturn;
}

void CLidarFilter::loadFromDatastream(const std::string& _datastream_entry, const std::string& _db_root_path)
{
	this->m_bIsProcessing = true;

	// Update sensor pose
	CPose pose;
	if (getLidarPose(pose))
		this->m_pSensorModel->updatePose(pose * this->m_pSensorModel->extrinsics());

	csv::reader::row row;
	row.parse_line(_datastream_entry, ',');

	enum { TS_STOP, SAMPLING_TIME, LIDAR_FILE_PATH, NUM };
	if (row.size() != NUM)
	{
		spdlog::error("CLidarFilter: Wrong number of columns. {} provided, but expected {}.", row.size(), NUM);
		return;
	}

	const auto lidar_path = _db_root_path + row.get<std::string>(LIDAR_FILE_PATH);
	
	std::ifstream stream_in(lidar_path, std::ios::binary);
	int64_t voxels_size = 0;
    stream_in.read((char*)&voxels_size, sizeof(voxels_size));

    CycVoxels lidar_voxels;
    lidar_voxels.resize(voxels_size);
	
    for (const auto& voxel : lidar_voxels)
    {
        stream_in.read((char*)&voxel.id, sizeof(voxel.id));
        stream_in.read((char*)&voxel.error, sizeof(voxel.error));
        stream_in.read((char*)&voxel.pt3d.x(), sizeof(voxel.pt3d.x()));
        stream_in.read((char*)&voxel.pt3d.y(), sizeof(voxel.pt3d.y()));
        stream_in.read((char*)&voxel.pt3d.z(), sizeof(voxel.pt3d.z()));
        stream_in.read((char*)&voxel.pt3d.w(), sizeof(voxel.pt3d.w()));
    }
	
	const auto tTimestampStop  = row.get<CyC_TIME_UNIT>(TS_STOP);
    const auto tSamplingTime   = row.get<CyC_TIME_UNIT>(SAMPLING_TIME);
    const auto tTimestampStart = tTimestampStop - tSamplingTime;

	updateData(lidar_voxels, std::unordered_map<CycDatablockKey, CyC_TIME_UNIT>(), tTimestampStart, tTimestampStop, tSamplingTime);
}

bool CLidarFilter::getLidarPose(CPose& _out_pose)
{
	if (m_pPoseDataFilter == nullptr)
		return false;

	bool bReturn = false;

	// Update pose from filter data, depending on the filter type (vehicle or drone)
	const CyC_TIME_UNIT readTsPose = m_pPoseDataFilter->getTimestampStop();

	if (readTsPose > m_lastReadTsPose)
	{
		m_lastReadTsPose = readTsPose;

		CycState state;
		if (m_pPoseDataFilter->getData(state))
		{
			bReturn = CFilterUtils::state2pose(state, m_pPoseDataFilter->getFilterType(), _out_pose);
		}
	}

	return bReturn;
}
