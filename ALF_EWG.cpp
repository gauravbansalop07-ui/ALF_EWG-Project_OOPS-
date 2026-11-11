#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace DisasterCore::Sensors {

struct GeoCoordinate {
    double latitude{};
    double longitude{};
};

struct TelemetryState {
    double rainfallMmPerHour{};
    double slopeDegrees{};
    double soilSaturation{};
    double vibrationHz{};
    double riverLevelMeters{};
    double hazardIndex{};
};

struct RainfallSlope {
    double rainfallMmPerHour;
    double slopeDegrees;
};

struct SaturationVibration {
    double soilSaturation;
    double vibrationHz;
};

inline double clampRisk(double value) {
    return value < 0.0 ? 0.0 : value > 100.0 ? 100.0 : value;
}

inline double computeHazardIndex(RainfallSlope reading) {
    return clampRisk(reading.rainfallMmPerHour * 0.55 + reading.slopeDegrees * 0.85);
}

inline double computeHazardIndex(SaturationVibration reading) {
    return clampRisk(reading.soilSaturation * 0.65 + reading.vibrationHz * 1.8);
}

class SensorFailureException : public std::exception {
public:
    const char* what() const noexcept override { return "Sensor node is unavailable"; }
};

class TelemetryChecksumMismatch : public std::exception {
public:
    const char* what() const noexcept override { return "Telemetry validation failed"; }
};

class ThresholdOverflowException : public std::exception {
public:
    const char* what() const noexcept override { return "Telemetry value exceeded the supported threshold"; }
};
GeoCoordinate& transformCoordinate(GeoCoordinate& coordinate, double latitudeOffset, double longitudeOffset) {
    coordinate.latitude += latitudeOffset;
    coordinate.longitude += longitudeOffset;
    return coordinate;
}

TelemetryState& accumulateTelemetry(TelemetryState& target, const TelemetryState& sample) {
    target.rainfallMmPerHour += sample.rainfallMmPerHour;
    target.soilSaturation += sample.soilSaturation;
    target.vibrationHz += sample.vibrationHz;
    target.riverLevelMeters += sample.riverLevelMeters;
    target.slopeDegrees = sample.slopeDegrees;
    target.hazardIndex = std::max(target.hazardIndex, sample.hazardIndex);
    return target;
}

class TelemetryBuffer {
public:
    explicit TelemetryBuffer(std::size_t capacity)
        : values_(capacity ? new double[capacity]{} : nullptr), size_(0), capacity_(capacity) {}

    TelemetryBuffer(const TelemetryBuffer& other)
        : values_(other.capacity_ ? new double[other.capacity_]{} : nullptr), size_(other.size_), capacity_(other.capacity_) {
        if (size_) std::copy(other.values_, other.values_ + size_, values_);
    }

    TelemetryBuffer& operator=(const TelemetryBuffer& other) {
        if (this != &other) {
            double* replacement = other.capacity_ ? new double[other.capacity_]{} : nullptr;
            if (other.size_) std::copy(other.values_, other.values_ + other.size_, replacement);
            delete[] values_;
            values_ = replacement;
            size_ = other.size_;
            capacity_ = other.capacity_;
        }
        return *this;
    }

    ~TelemetryBuffer() { delete[] values_; }

    void push(double value) {
        if (size_ == capacity_) throw std::length_error("Telemetry buffer capacity exceeded");
        values_[size_++] = value;
    }

    double at(std::size_t index) const {
        if (index >= size_) throw std::out_of_range("Telemetry buffer index is outside stored data");
        return values_[index];
    }

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }

private:
    double* values_;
    std::size_t size_;
    std::size_t capacity_;
};

class TelemetryAuditor;

class SensorStation {
public:
    SensorStation(std::string id, GeoCoordinate location)
        : id_(std::move(id)), location_(location) { ++activeCount_; }

    SensorStation(const SensorStation& other)
        : id_(other.id_), location_(other.location_), raw_(other.raw_), hazard_(other.hazard_) { ++activeCount_; }

    SensorStation(SensorStation&& other) noexcept
        : id_(std::move(other.id_)), location_(other.location_), raw_(other.raw_), hazard_(other.hazard_) { ++activeCount_; }

    SensorStation& operator=(const SensorStation& other) {
        if (this != &other) {
            id_ = other.id_;
            location_ = other.location_;
            raw_ = other.raw_;
            hazard_ = other.hazard_;
        }
        return *this;
    }

    SensorStation& operator=(SensorStation&& other) noexcept {
        if (this != &other) {
            id_ = std::move(other.id_);
            location_ = other.location_;
            raw_ = other.raw_;
            hazard_ = other.hazard_;
        }
        return *this;
    }

    ~SensorStation() { --activeCount_; }

    void update(const TelemetryState& state) {
        if (state.rainfallMmPerHour < 0.0 || state.slopeDegrees < 0.0 || state.soilSaturation < 0.0 || state.vibrationHz < 0.0)
            throw TelemetryChecksumMismatch();
        if (state.rainfallMmPerHour > 180.0 || state.slopeDegrees > 90.0 || state.soilSaturation > 100.0 || state.vibrationHz > 55.0)
            throw ThresholdOverflowException();
        raw_ = state;
        hazard_ = std::max(computeHazardIndex(RainfallSlope{state.rainfallMmPerHour, state.slopeDegrees}),
                           computeHazardIndex(SaturationVibration{state.soilSaturation, state.vibrationHz}));
    }

    const std::string& id() const { return id_; }
    const GeoCoordinate& location() const { return location_; }
    double hazard() const { return hazard_; }
    bool operator>(const SensorStation& other) const { return hazard_ > other.hazard_; }
    static std::size_t activeCount() { return activeCount_; }

private:
    std::string id_;
    GeoCoordinate location_;
    TelemetryState raw_{};
    double hazard_{};
    static std::size_t activeCount_;
    friend class TelemetryAuditor;
    friend std::ostream& operator<<(std::ostream&, const SensorStation&);
    friend std::istream& operator>>(std::istream&, SensorStation&);
};

std::size_t SensorStation::activeCount_ = 0;

std::ostream& operator<<(std::ostream& stream, const SensorStation& station) {
    return stream << station.id_ << ' ' << std::fixed << std::setprecision(4)
                  << station.location_.latitude << ' ' << station.location_.longitude << ' ' << station.hazard_;
}

std::istream& operator>>(std::istream& stream, SensorStation& station) {
    double latitude = 0.0;
    double longitude = 0.0;
    double hazard = 0.0;
    if (stream >> station.id_ >> latitude >> longitude >> hazard) {
        station.location_ = {latitude, longitude};
        station.hazard_ = clampRisk(hazard);
    }
    return stream;
}

class TelemetryAuditor {
public:
    static double rawVibration(const SensorStation& station) { return station.raw_.vibrationHz; }
    static double rawSoilSaturation(const SensorStation& station) { return station.raw_.soilSaturation; }
};

class PowerSubsystem {
public:
    explicit PowerSubsystem(double batteryPercent = 100.0) : batteryPercent_(batteryPercent) {
        if (batteryPercent_ < 0.0 || batteryPercent_ > 100.0) throw ThresholdOverflowException();
    }

    bool operational() const { return batteryPercent_ > 0.0; }
    void consume(double amount) { batteryPercent_ = std::max(0.0, batteryPercent_ - amount); }

private:
    double batteryPercent_;
};

class DisasterTelemetryDevice {
public:
    explicit DisasterTelemetryDevice(std::string nodeId) : nodeId_(std::move(nodeId)) {}
    virtual ~DisasterTelemetryDevice() = default;
    const std::string& nodeId() const { return nodeId_; }

private:
    std::string nodeId_;
};

class BaseSensorNode : public virtual DisasterTelemetryDevice {
public:
    explicit BaseSensorNode(std::string nodeId) : DisasterTelemetryDevice(std::move(nodeId)), power_(100.0) {}
    virtual TelemetryState readTelemetry() = 0;
    virtual bool evaluateThreshold(const TelemetryState& state) const = 0;
    virtual ~BaseSensorNode() = default;

protected:
    PowerSubsystem power_;
};

class SolarPoweredNode : public virtual BaseSensorNode {
public:
    explicit SolarPoweredNode(std::string nodeId)
        : DisasterTelemetryDevice(nodeId), BaseSensorNode(std::move(nodeId)) {}
};

class WirelessLoRaTransceiver : public virtual BaseSensorNode {
public:
    explicit WirelessLoRaTransceiver(std::string nodeId)
        : DisasterTelemetryDevice(nodeId), BaseSensorNode(std::move(nodeId)) {}

    bool transmit(const TelemetryState& state) const { return power_.operational() && state.hazardIndex >= 0.0; }
};

class AutonomousHybridProbe final : public SolarPoweredNode, public WirelessLoRaTransceiver {
public:
    explicit AutonomousHybridProbe(std::string nodeId)
        : DisasterTelemetryDevice(nodeId), BaseSensorNode(nodeId), SolarPoweredNode(nodeId), WirelessLoRaTransceiver(std::move(nodeId)) {}

    TelemetryState readTelemetry() override {
        if (!power_.operational()) throw SensorFailureException();
        TelemetryState sample{42.0, 38.0, 74.0, 12.0, 3.2, 0.0};
        sample.hazardIndex = std::max(computeHazardIndex(RainfallSlope{sample.rainfallMmPerHour, sample.slopeDegrees}),
                                      computeHazardIndex(SaturationVibration{sample.soilSaturation, sample.vibrationHz}));
        power_.consume(0.25);
        return sample;
    }

    bool evaluateThreshold(const TelemetryState& state) const override {
        return transmit(state) && state.hazardIndex >= 60.0;
    }
};

class SensorCluster {
public:
    SensorCluster() = default;
    explicit SensorCluster(std::vector<SensorStation> stations) : stations_(std::move(stations)) {}

    SensorCluster operator+(const SensorCluster& other) const {
        std::vector<SensorStation> combined = stations_;
        combined.insert(combined.end(), other.stations_.begin(), other.stations_.end());
        return SensorCluster(std::move(combined));
    }

    bool operator>(const SensorCluster& other) const { return maximumHazard() > other.maximumHazard(); }
    const std::vector<SensorStation>& stations() const { return stations_; }

    double maximumHazard() const {
        double maximum = 0.0;
        for (const auto& station : stations_) maximum = std::max(maximum, station.hazard());
        return maximum;
    }

private:
    std::vector<SensorStation> stations_;
};

}

namespace DisasterCore::Alerts {

using DisasterCore::Sensors::SensorStation;

class EarlyWarningSiren {
public:
    explicit EarlyWarningSiren(std::string region) : region_(std::move(region)) {}

    void dispatch(double hazard) {
        emergencyLevel_ = hazard >= 80.0 ? 3 : hazard >= 60.0 ? 2 : hazard >= 40.0 ? 1 : 0;
        if (emergencyLevel_ > 0) std::cout << "SIREN " << region_ << " level " << emergencyLevel_ << '\n';
    }

    const std::string& region() const { return region_; }
    static int emergencyLevel() { return emergencyLevel_; }

private:
    std::string region_;
    static int emergencyLevel_;
};

int EarlyWarningSiren::emergencyLevel_ = 0;

struct IncidentRecord {
    long long epochSeconds{};
    double hazardIndex{};
    char stationId[32]{};
};

void appendIncident(const std::string& path, const IncidentRecord& incident) {
    namespace fs = std::filesystem;
    if (!fs::exists(path)) {
        std::ofstream create(path, std::ios::binary);
        if (!create) throw std::ios_base::failure("Unable to create incident log");
    }
    const auto originalSize = fs::file_size(path);
    std::fstream stream(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!stream) throw std::ios_base::failure("Unable to open incident log");
    if (originalSize >= sizeof(IncidentRecord)) {
        IncidentRecord previous{};
        stream.seekg(static_cast<std::streamoff>(originalSize - sizeof(IncidentRecord)));
        stream.read(reinterpret_cast<char*>(&previous), sizeof(previous));
        if (!stream) throw std::ios_base::failure("Incident log integrity check failed");
    }
    stream.clear();
    stream.seekp(0, std::ios::end);
    stream.write(reinterpret_cast<const char*>(&incident), sizeof(incident));
    stream.flush();
    if (!stream) {
        stream.close();
        fs::resize_file(path, originalSize);
        throw std::ios_base::failure("Incident write rolled back");
    }
}

std::vector<IncidentRecord> readIncidents(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::vector<IncidentRecord> records;
    IncidentRecord record{};
    while (stream.read(reinterpret_cast<char*>(&record), sizeof(record))) records.push_back(record);
    if (!stream.eof()) throw std::ios_base::failure("Unable to read incident log");
    return records;
}

struct EvacuationPriority {
    bool operator()(const SensorStation& left, const SensorStation& right) const { return left > right; }
};

std::ostream& tabular(std::ostream& stream) {
    return stream << std::left << std::setw(12) << "STATION" << std::setw(12) << "LATITUDE"
                  << std::setw(12) << "LONGITUDE" << std::setw(10) << "RISK" << '\n';
}

}

int main() {
    using namespace DisasterCore::Sensors;
    using namespace DisasterCore::Alerts;

    try {
        GeoCoordinate calibratedLocation{30.3753, 79.0193};
        transformCoordinate(calibratedLocation, 0.0, 0.0);
        AutonomousHybridProbe probe("HIMALAYA-07");
        std::vector<BaseSensorNode*> sweep{&probe};
        std::map<std::string, SensorCluster> zones;
        SensorStation station("HIMALAYA-07", calibratedLocation);
        TelemetryBuffer recentReadings(8);
        TelemetryState accumulated{};
        EarlyWarningSiren siren("Alaknanda Valley");

        for (BaseSensorNode* node : sweep) {
            const TelemetryState sample = node->readTelemetry();
            accumulateTelemetry(accumulated, sample);
            station.update(sample);
            recentReadings.push(sample.hazardIndex);
            zones["Alaknanda"] = SensorCluster({station});
            std::for_each(zones["Alaknanda"].stations().begin(), zones["Alaknanda"].stations().end(),
                          [&siren](const SensorStation& item) { siren.dispatch(item.hazard()); });
        }

        std::vector<SensorStation> priorities = zones.at("Alaknanda").stations();
        std::sort(priorities.begin(), priorities.end(), EvacuationPriority{});
        std::cout << tabular << station << '\n';
        std::cout << "Raw vibration audited: " << TelemetryAuditor::rawVibration(station) << " Hz\n";
        std::cout << "Buffered readings: " << recentReadings.size()
                  << ", active stations: " << SensorStation::activeCount() << '\n';

        IncidentRecord incident{};
        incident.epochSeconds = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        incident.hazardIndex = station.hazard();
        std::strncpy(incident.stationId, station.id().c_str(), sizeof(incident.stationId) - 1);
        appendIncident("incidents.bin", incident);
        std::cout << "Persisted incidents: " << readIncidents("incidents.bin").size() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "ALF-EWG: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

