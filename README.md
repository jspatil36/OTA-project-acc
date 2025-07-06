# OTA-project-ACC
Adaptive Cruise Control on a virtual ECU with OTA capabilities

Version: 1.0.0
Author: Jayesh Patil
Date: July 6, 2025

1. Project Overview
This project is a C++ simulation of a modern automotive Electronic Control Unit (ECU) designed to demonstrate the core principles of the Software-Defined Vehicle (SDV). It provides a complete, end-to-end platform for developing, deploying, and updating a standalone vehicle feature—in this case, an Adaptive Cruise Control (ACC) application—using industry-standard protocols and a modular, service-oriented architecture.

The simulation allows a user to interact with the virtual ECU in real-time through a command-line diagnostic tool, enabling them to read data, write new calibration parameters, and perform a full Over-the-Air (OTA) update of the ACC feature without stopping the core ECU platform.

2. Key Architectural Concepts
The primary goal of this project was to explore and implement a robust and scalable software architecture, not just a single feature. The design is built on the following key concepts:

Decoupled Platform and Application: The system is divided into two main components:

The Platform (TargetECU): A stable, validated executable that manages the ECU's core responsibilities, such as the boot sequence, state machine, network communication (DoIP), and diagnostic services (UDS).

The Application (acc_app): A self-contained feature, compiled as a separate shared library (.so or .dylib). This component contains all the logic for the Adaptive Cruise Control.

Dynamic Application Loading: The Platform dynamically loads the Application at runtime using dlopen(). This is the cornerstone of the architecture, as it allows the feature logic to be developed, tested, and updated completely independently of the core ECU software.

Interface-Based Communication:

Internal: The Platform communicates with the Application through a simple function pointer (run_acc_application()) and a simulated NVRAMManager which acts as a shared data bus for configuration and state variables. This prevents tight coupling.

External: The system exposes a diagnostic interface based on standard automotive protocols, DoIP (Diagnostics over IP) and UDS (Unified Diagnostic Services), ensuring predictable and standardized external communication.

3. Core Features
Realistic ECU State Machine: Simulates BOOT, APPLICATION, and UPDATE_PENDING states.

Over-the-Air (OTA) Updates: Demonstrates a full OTA flashing sequence for the standalone ACC application, including file transfer and integrity verification via SHA-256 hash.

Live Diagnostics & Calibration: A powerful command-line client (doip_client) allows for real-time interaction with the ECU.

Read Data by Identifier (UDS Service 0x22): Read the current state of vehicle parameters like speed and controller gains.

Write Data by Identifier (UDS Service 0x2E): Calibrate and tune the running controller by writing new values for parameters like target speed, following gap, and PI controller gains.

Advanced PI Controller: The ACC application uses a Proportional-Integral (PI) controller for more realistic speed management, complete with tunable gains and acceleration/deceleration limits.

Cross-Platform Compatibility: The code is designed to compile and run on both Linux (using .so libraries) and macOS (using .dylib libraries).

4. Prerequisites and Dependencies
To build and run this project, you will need:

A C++17 compliant compiler (e.g., g++, Clang)

CMake (version 3.15 or higher)

Boost Libraries (specifically system and asio)

OpenSSL Libraries (for SHA-256 hashing)

On a Debian/Ubuntu-based system, these can be installed with:

sudo apt-get update
sudo apt-get install build-essential cmake libboost-system-dev libssl-dev

5. How to Build the Project
Place all 9 source files (.cpp, .hpp, CMakeLists.txt) into a single directory.

Open a terminal in that directory.

Create a build directory to keep the compiled files separate:

mkdir build
cd build

Run CMake to configure the project, then run make to compile it:

cmake ..
make

This will generate the TargetECU executable, the doip_client executable, and the libacc_app.so (or .dylib) shared library inside the build directory.

6. How to Run the Simulation
The simulation requires two separate terminals, both navigated to the build directory.

Terminal 1: Start the Virtual ECU
This command starts the main ECU platform. It will boot up and begin its main loop, which involves loading and executing the ACC application every two seconds.

./TargetECU

Terminal 2: Use the Diagnostic Client
This terminal is used to send commands to the running ECU. You can use it at any time while the TargetECU is running.

7. User Guide: Interacting with the ECU
The doip_client is your tool for diagnostics and calibration.

Reading Data (UDS Service 0x22)

Get the current speed of the lead vehicle:

./doip_client --get-lead-speed

Get the current speed of your own vehicle:

./doip_client --get-own-speed

Get the current proportional gain (Kp) of the controller:

./doip_client --get-kp

Writing Data (UDS Service 0x2E)

Set the lead vehicle's speed to 55 mph:

./doip_client --set-lead-speed 55

Change the desired following gap to 2 car lengths:

./doip_client --set-gap 2

Tune the controller's proportional gain to 0.5:

./doip_client --set-kp 0.5

8. OTA Update Procedure
This process allows you to update the ACC application logic without stopping the ECU.

Modify the Application: Make a code change to acc_controller.cpp. For example, change a std::cout message to indicate a new version.

Re-compile the Application: In your terminal (inside the build directory), compile only the application library. This is very fast.

make acc_app

Switch ECU to Programming Mode: In Terminal 2, send the command to prepare the ECU for an update.

./doip_client --program

Verification: The ECU in Terminal 1 will log that it has entered the UPDATE_PENDING state.

Send the New Application File: Perform the OTA update by sending the newly compiled library.

On macOS:

./doip_client --update ./libacc_app.dylib

On Linux:

./doip_client --update ./libacc_app.so

Verify: The ECU will automatically verify the file hash, apply the update, and return to the APPLICATION state, now running your new code.

9. Project Structure
.
├── acc_controller.cpp      # Source for the standalone ACC feature
├── acc_controller.hpp      # Header for the ACC feature
├── client.cpp              # Source for the diagnostic client tool
├── CMakeLists.txt          # Build configuration file
├── doip_server.hpp         # Defines the main DoIP server class
├── doip_session.hpp        # Handles logic for a single client connection and UDS messages
├── ecu_state.hpp           # Defines the ECU's state machine enum
├── main.cpp                # The main entry point for the ECU platform
└── nvram_manager.hpp       # Simulates non-volatile memory for storing parameters

10. Future Work & Potential Improvements
Multi-Application Support: Modify the ECU platform to load and manage a list of multiple application libraries instead of just one.

More Complex Controller: Evolve the PI controller into a full PID (Proportional-Integral-Derivative) controller for more responsive handling.

Enhanced Security: Implement digital signatures for OTA updates in addition to the hash check to ensure the firmware is from a trusted source.

Robust Data Handling: Expand the UDS implementation to handle data values larger than a single byte, requiring more complex data serialization and deserialization.

