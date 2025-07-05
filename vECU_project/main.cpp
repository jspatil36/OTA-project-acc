#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <memory>
#include <vector>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <random>

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <dlfcn.h> // Required for dynamic library loading

#include "ecu_state.hpp"
#include "nvram_manager.hpp"
#include "doip_server.hpp"

// --- Global state and control variables ---
std::atomic<EcuState> g_ecu_state(EcuState::BOOT);
std::atomic<bool> g_running(true);
NVRAMManager g_nvram("nvram.dat");
std::string g_executable_path;

// --- Dynamic Library Handling ---
void* g_acc_library_handle = nullptr;
void (*g_run_acc_application)() = nullptr;

// This uses preprocessor directives to set the correct library name based on the OS
#if defined(__APPLE__)
    const std::string ACC_LIBRARY_PATH = "./libacc_app.dylib";
#else
    const std::string ACC_LIBRARY_PATH = "./libacc_app.so";
#endif


// --- Networking objects ---
boost::asio::io_context g_io_context;
std::unique_ptr<DoIPServer> g_doip_server;
std::thread g_server_thread;

// --- Function Prototypes ---
void run_boot_sequence(const std::string& executable_path);
void run_application_mode();
void handle_signal(int signal);
void start_network_server();
void stop_network_server();
std::optional<std::string> calculate_file_hash(const std::string& file_path);
void apply_update(const std::string& library_path);
bool load_acc_application();
void unload_acc_application();


std::optional<std::string> calculate_file_hash(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[HASH] ERROR: Could not open file: " << file_path << std::endl;
        return std::nullopt;
    }

    EVP_MD_CTX* md_context = EVP_MD_CTX_new();
    if (!md_context || 1 != EVP_DigestInit_ex(md_context, EVP_sha256(), NULL)) {
        if(md_context) EVP_MD_CTX_free(md_context);
        return std::nullopt;
    }

    char buffer[1024];
    while (file.read(buffer, sizeof(buffer))) {
        if (1 != EVP_DigestUpdate(md_context, buffer, file.gcount())) {
            EVP_MD_CTX_free(md_context);
            return std::nullopt;
        }
    }
    if (file.gcount() > 0) {
        if (1 != EVP_DigestUpdate(md_context, buffer, file.gcount())) {
            EVP_MD_CTX_free(md_context);
            return std::nullopt;
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    if (1 != EVP_DigestFinal_ex(md_context, hash, &hash_len)) {
        EVP_MD_CTX_free(md_context);
        return std::nullopt;
    }
    EVP_MD_CTX_free(md_context);

    std::stringstream ss;
    for (unsigned int i = 0; i < hash_len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

int main(int argc, char* argv[]) {
    if (argc < 1) return 1;
    g_executable_path = argv[0];

    signal(SIGINT, handle_signal);

    std::cout << "--- Virtual ECU Simulation V4 Started ---" << std::endl;
    std::cout << "--- Use the client to change speed and gap settings at runtime. ---" << std::endl;
    std::cout << "Press Ctrl+C to shut down." << std::endl;

    start_network_server();

    while (g_running) {
        switch (g_ecu_state) {
            case EcuState::BOOT:
                run_boot_sequence(g_executable_path);
                break;
            case EcuState::APPLICATION:
                run_application_mode();
                break;
            case EcuState::UPDATE_PENDING:
                std::cout << "[STATE] In UPDATE_PENDING. Waiting for commands..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(2));
                break;
            case EcuState::BRICKED:
                std::cerr << "[STATE] ECU is BRICKED. Halting operations." << std::endl;
                g_running = false;
                break;
        }
    }

    stop_network_server();
    unload_acc_application(); // Unload the library on shutdown
    std::cout << "--- Virtual ECU Simulation Shutting Down ---" << std::endl;
    return 0;
}

void start_network_server() {
    try {
        g_doip_server = std::make_unique<DoIPServer>(g_io_context, 13400);
        g_server_thread = std::thread([]() {
            g_doip_server->run();
        });
    } catch (const std::exception& e) {
        std::cerr << "Failed to start network server: " << e.what() << std::endl;
        g_ecu_state = EcuState::BRICKED;
    }
}

void stop_network_server() {
    if (g_doip_server) {
        g_doip_server->stop();
    }
    if (g_server_thread.joinable()) {
        g_server_thread.join();
    }
}

void run_boot_sequence(const std::string& executable_path) {
    std::cout << "[STATE] Entering BOOT..." << std::endl;
    if (!g_nvram.load()) {
        std::cerr << "[BOOT] CRITICAL: Failed to load NVRAM. Entering BRICKED state." << std::endl;
        g_ecu_state = EcuState::BRICKED;
        return;
    }
    
    std::cout << "[BOOT] Boot sequence complete. Transitioning to APPLICATION state." << std::endl;
    g_ecu_state = EcuState::APPLICATION;
}

void run_application_mode() {
    if (!g_running) return;
    
    // The environment is now stable and only changes via client commands.
    if (load_acc_application()) {
        if (g_run_acc_application) {
            g_run_acc_application();
        }
    } else {
        std::cerr << "[APP] Failed to run application logic." << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

bool load_acc_application() {
    if (g_acc_library_handle) {
        dlclose(g_acc_library_handle);
        g_acc_library_handle = nullptr;
    }

    g_acc_library_handle = dlopen(ACC_LIBRARY_PATH.c_str(), RTLD_LAZY);
    if (!g_acc_library_handle) {
        std::cerr << "[APP] ERROR: Cannot load shared library: " << dlerror() << std::endl;
        return false;
    }

    g_run_acc_application = (void (*)())dlsym(g_acc_library_handle, "run_acc_application");
    const char* dlsym_error = dlerror();
    if (dlsym_error) {
        std::cerr << "[APP] ERROR: Cannot find symbol 'run_acc_application': " << dlsym_error << std::endl;
        dlclose(g_acc_library_handle);
        g_acc_library_handle = nullptr;
        return false;
    }
    return true;
}

void unload_acc_application() {
    if (g_acc_library_handle) {
        dlclose(g_acc_library_handle);
        g_acc_library_handle = nullptr;
        g_run_acc_application = nullptr;
        std::cout << "[APP] Unloaded ACC application library." << std::endl;
    }
}

void handle_signal(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n[INFO] Shutdown signal received. Initiating shutdown..." << std::endl;
        if (g_doip_server) {
            g_doip_server->stop();
        }
        g_running = false;
    }
}

void apply_update(const std::string& current_executable_path) {
    std::cout << "[OTA] Applying update to ACC application..." << std::endl;
    
    unload_acc_application();

    if (std::rename("update.bin", ACC_LIBRARY_PATH.c_str()) != 0) {
        perror("[OTA] CRITICAL: Failed to apply update to library");
    } else {
        std::cout << "[OTA] Update applied successfully to " << ACC_LIBRARY_PATH << ". ECU will reload it." << std::endl;
    }
    g_ecu_state = EcuState::APPLICATION; 
}
