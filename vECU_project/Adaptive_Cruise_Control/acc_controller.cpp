#include "acc_controller.hpp"
#include "../nvram_manager.hpp"
#include <iostream>
#include <string>
#include <algorithm> // For std::clamp

// --- PI Controller State ---
// These variables need to persist between runs of the controller.
// In a real ECU, they would be class members or static variables.
float integral_error = 0.0f;

// The main function for the advanced ACC application logic.
void run_acc_application() {
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "[ACC] Advanced Controller Cycle Started." << std::endl;

    NVRAMManager nvram("nvram.dat");
    if (!nvram.load()) {
        std::cerr << "[ACC] ERROR: Could not load NVRAM data." << std::endl;
        return;
    }

    // --- Read Parameters from NVRAM ---
    float lead_speed = std::stof(nvram.get_string("LEAD_VEHICLE_SPEED").value_or("0.0"));
    float own_speed = std::stof(nvram.get_string("OWN_VEHICLE_SPEED").value_or("0.0"));
    int gap_setting = std::stoi(nvram.get_string("ACC_GAP_SETTING").value_or("2"));
    
    // Read new PI controller and limit parameters
    float Kp = std::stof(nvram.get_string("ACC_KP").value_or("0.4"));
    float Ki = std::stof(nvram.get_string("ACC_KI").value_or("0.1"));
    float max_accel = std::stof(nvram.get_string("ACC_MAX_ACCEL").value_or("2.0"));
    float max_decel = std::stof(nvram.get_string("ACC_MAX_DECEL").value_or("3.0"));


    // --- PI Controller Logic ---
    float error = lead_speed - own_speed;

    // Update integral error (with anti-windup)
    integral_error += error;
    // Simple anti-windup: clamp the integral term to prevent it from growing too large
    integral_error = std::clamp(integral_error, -20.0f, 20.0f);

    // Calculate control output (how much to accelerate/decelerate)
    float control_output = (Kp * error) + (Ki * integral_error);

    // Clamp the output to the max acceleration/deceleration rates
    float speed_change = std::clamp(control_output, -max_decel, max_accel);

    // --- Apply Control Action ---
    own_speed += speed_change;
    
    // Ensure speed doesn't go below zero
    if (own_speed < 0) own_speed = 0;

    std::cout << "[ACC] Target: " << lead_speed << " mph | Current: " << own_speed << " mph | Gap: " << gap_setting << std::endl;
    printf("[ACC] Error: %.2f | Control Output: %.2f | Final Speed Change: %.2f\n", error, control_output, speed_change);


    // --- Save State for Next Cycle ---
    nvram.set_string("OWN_VEHICLE_SPEED", std::to_string(own_speed));
    nvram.save();

    std::cout << "[ACC] Advanced Controller Cycle Finished." << std::endl;
    std::cout << "----------------------------------------" << std::endl;
}
