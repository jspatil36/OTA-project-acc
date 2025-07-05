#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <optional>
#include <mutex>

/**
 * @class NVRAMManager
 * @brief Simulates a simple Non-Volatile RAM by reading from and writing to a file.
 *
 * This class provides a basic key-value store that persists data in a plain text file,
 * mimicking how an ECU might store configuration data in its flash memory.
 * It is now thread-safe to prevent race conditions.
 */
class NVRAMManager {
public:
    /**
     * @brief Constructor.
     * @param filename The path to the file to be used for persistent storage.
     */
    explicit NVRAMManager(const std::string& filename) : m_filename(filename) {}

    /**
     * @brief Loads the key-value data from the NVRAM file in a thread-safe manner.
     *
     * If the file doesn't exist, it creates a default configuration.
     * @return True if loading was successful, false otherwise.
     */
    bool load() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::ifstream file(m_filename);
        if (!file.is_open()) {
            std::cout << "[NVRAM] No existing NVRAM file found. Creating default." << std::endl;
            return create_default_nvram_internal();
        }

        m_data.clear(); // Clear existing data before loading
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

    /**
     * @brief Saves the current key-value data to the NVRAM file in a thread-safe manner.
     * @return True if saving was successful, false otherwise.
     */
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

    /**
     * @brief Retrieves a string value for a given key.
     * @param key The key to look up.
     * @return An std::optional containing the value if the key exists, otherwise std::nullopt.
     */
    std::optional<std::string> get_string(const std::string& key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_data.find(key);
        if (it != m_data.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Sets a string value for a given key.
     * @param key The key to set.
     * @param value The value to associate with the key.
     */
    void set_string(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_data[key] = value;
    }

private:
    std::string m_filename;
    std::map<std::string, std::string> m_data;
    std::mutex m_mutex;

    /**
     * @brief Internal method to create a default NVRAM file. Not thread-safe by itself.
     */
    bool create_default_nvram_internal() {
        m_data["FIRMWARE_VERSION"] = "3.0.0";
        m_data["ECU_SERIAL_NUMBER"] = "VECU-2025-001";
        m_data["LEAD_VEHICLE_SPEED"] = "65";
        m_data["OWN_VEHICLE_SPEED"] = "65";
        m_data["ACC_GAP_SETTING"] = "3"; // e.g., 3 car lengths
        
        std::ofstream file(m_filename);
        if (!file.is_open()) return false;
        for (const auto& pair : m_data) {
            file << pair.first << "=" << pair.second << std::endl;
        }
        return true;
    }
};
