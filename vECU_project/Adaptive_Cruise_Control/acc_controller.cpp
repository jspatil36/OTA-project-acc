#include "acc_controller.hpp"
#include "../nvram_manager.hpp" // Include NVRAM to read live data
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

// The main function for the ACC application logic.
// This is a dynamic, reactive controller.
void run_acc_application() {
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "[ACC] Controller Cycle Started." << std::endl;

    NVRAMManager nvram("nvram.dat");
    if (!nvram.load()) {
        std::cerr << "[ACC] ERROR: Could not load NVRAM data." << std::endl;
        return;
    }

    // Get runtime parameters from NVRAM
    int lead_speed = std::stoi(nvram.get_string("LEAD_VEHICLE_SPEED").value_or("0"));
    int own_speed = std::stoi(nvram.get_string("OWN_VEHICLE_SPEED").value_or("0"));
    int gap_setting = std::stoi(nvram.get_string("ACC_GAP_SETTING").value_or("2"));

    std::cout << "[ACC] Lead Vehicle Speed: " << lead_speed << " mph | "
              << "Own Speed: " << own_speed << " mph | "
              << "Gap Setting: " << gap_setting << std::endl;

    // Simple P-controller logic
    if (own_speed < lead_speed) {
        own_speed++;
        std::cout << "[ACC] ACTION: Accelerating." << std::endl;
    } else if (own_speed > lead_speed) {
        own_speed--;
        std::cout << "[ACC] ACTION: Decelerating." << std::endl;
    } else {
        std::cout << "[ACC] ACTION: Maintaining speed." << std::endl;
    }

    // Update our own speed in NVRAM for the next cycle
    nvram.set_string("OWN_VEHICLE_SPEED", std::to_string(own_speed));
    nvram.save();

    std::cout << "[ACC] Controller Cycle Finished." << std::endl;
    std::cout << "----------------------------------------" << std::endl;
}
