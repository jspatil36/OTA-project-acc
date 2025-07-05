#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <optional>
#include <mutex>

class NVRAMManager {
public:
    explicit NVRAMManager(const std::string& filename) : m_filename(filename) {}

    bool load() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::ifstream file(m_filename);
        if (!file.is_open()) {
            std::cout << "[NVRAM] No existing NVRAM file found. Creating default." << std::endl;
            return create_default_nvram_internal();
        }

        m_data.clear();
        std::string line;
        while (std::getline(file, line)) {
            size_t delimiter_pos = line.find('=');
            if (delimiter_pos != std::string::npos) {
                std::string key = line.substr(0, delimiter_pos);
                std::string value = line.substr(delimiter_pos + 1);
                m_data[key] = value;
            }
        }
        return true;
    }

    bool save() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::ofstream file(m_filename, std::ios::trunc);
        if (!file.is_open()) {
            std::cerr << "[NVRAM] ERROR: Could not open file for writing: " << m_filename << std::endl;
            return false;
        }

        for (const auto& pair : m_data) {
            file << pair.first << "=" << pair.second << std::endl;
        }
        return true;
    }

    std::optional<std::string> get_string(const std::string& key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_data.find(key);
        if (it != m_data.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void set_string(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_data[key] = value;
    }

private:
    std::string m_filename;
    std::map<std::string, std::string> m_data;
    std::mutex m_mutex;

    // Creates a default NVRAM file with initial values for the advanced controller.
    bool create_default_nvram_internal() {
        m_data["FIRMWARE_VERSION"] = "5.0.0";
        m_data["ECU_SERIAL_NUMBER"] = "VECU-2025-005";
        m_data["LEAD_VEHICLE_SPEED"] = "65.0";
        m_data["OWN_VEHICLE_SPEED"] = "65.0";
        m_data["ACC_GAP_SETTING"] = "3";
        
        // --- New Advanced Parameters ---
        m_data["ACC_KP"] = "0.4"; // Proportional gain
        m_data["ACC_KI"] = "0.1"; // Integral gain
        m_data["ACC_MAX_ACCEL"] = "2.0"; // Max speed increase per cycle (mph)
        m_data["ACC_MAX_DECEL"] = "3.0"; // Max speed decrease per cycle (mph)
        
        std::ofstream file(m_filename);
        if (!file.is_open()) return false;
        for (const auto& pair : m_data) {
            file << pair.first << "=" << pair.second << std::endl;
        }
        return true;
    }
};
