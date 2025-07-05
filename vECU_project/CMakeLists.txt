# --- Project Definition ---
cmake_minimum_required(VERSION 3.15)
project(VirtualECU VERSION 4.0 LANGUAGES CXX)

# --- Standard and Properties ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Find Dependencies ---
find_package(OpenSSL REQUIRED)
set(Boost_NO_BOOST_CMAKE ON)
find_package(Boost REQUIRED COMPONENTS system)

# --- Target Definitions ---
# The main ECU executable platform
add_executable(TargetECU main.cpp)

# The DoIP client for sending updates and commands
add_executable(doip_client client.cpp)

# The ACC application, compiled as a shared library
add_library(acc_app SHARED Adaptive_Cruise_Control/acc_controller.cpp)


# --- Linking Dependencies for the ECU ---
target_include_directories(TargetECU PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(TargetECU
    PRIVATE
    OpenSSL::Crypto
    Boost::system
    dl # Library for dynamic loading (dlsym, dlopen)
)

# --- Linking Dependencies for the Client ---
target_include_directories(doip_client PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(doip_client
    PRIVATE
    OpenSSL::Crypto
    Boost::system
)

# --- Installation ---
# Install the executables and the ACC application library
install(TARGETS TargetECU doip_client acc_app DESTINATION bin)
