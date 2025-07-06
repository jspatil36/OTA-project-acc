#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <boost/asio.hpp>
#include <arpa/inet.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

using boost::asio::ip::tcp;

#pragma pack(push, 1)
struct DoIPHeader {
    uint8_t  protocol_version;
    uint8_t  inverse_protocol_version;
    uint16_t payload_type;
    uint32_t payload_length;
};
#pragma pack(pop)

// UDS Service and Data Identifiers
const uint8_t UDS_READ_DATA_BY_IDENTIFIER = 0x22;
const uint8_t UDS_WRITE_DATA_BY_IDENTIFIER = 0x2E;
const uint8_t UDS_ROUTINE_CONTROL = 0x31;
const uint8_t UDS_REQUEST_DOWNLOAD = 0x34;
const uint8_t UDS_TRANSFER_DATA = 0x36;
const uint8_t UDS_REQUEST_TRANSFER_EXIT = 0x37;

const uint16_t UDS_ENTER_PROGRAMMING_SESSION = 0xFF00;
const uint16_t DID_LEAD_VEHICLE_SPEED = 0xF101;
const uint16_t DID_OWN_VEHICLE_SPEED = 0xF103;
const uint16_t DID_ACC_GAP_SETTING = 0xF102;
const uint16_t DID_ACC_KP = 0xD101;
const uint16_t DID_ACC_KI = 0xD102;
const uint16_t DID_ACC_MAX_ACCEL = 0xD103;
const uint16_t DID_ACC_MAX_DECEL = 0xD104;

// Function Prototypes
bool send_and_receive(tcp::socket& socket, uint16_t type, const std::vector<uint8_t>& payload, std::vector<uint8_t>& response_payload);
std::optional<std::string> calculate_file_hash(const std::string& file_path);
void print_usage();

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    try {
        boost::asio::io_context io_context;
        tcp::socket socket(io_context);
        tcp::resolver resolver(io_context);
        boost::asio::connect(socket, resolver.resolve("localhost", "13400"));
        
        std::string command = argv[1];
        std::vector<uint8_t> response_payload;

        if (command == "--identify") {
            if (!send_and_receive(socket, 0x0004, {}, response_payload)) return 1;
            std::cout << "[CLIENT] Vehicle VIN: " << std::string(response_payload.begin(), response_payload.end()) << std::endl;
        } else if (command == "--program") {
            std::vector<uint8_t> payload = {UDS_ROUTINE_CONTROL, 0x01, (UDS_ENTER_PROGRAMMING_SESSION >> 8) & 0xFF, UDS_ENTER_PROGRAMMING_SESSION & 0xFF};
            if (!send_and_receive(socket, 0x8001, payload, response_payload)) return 1;
        } else if (command.rfind("--get", 0) == 0) { // Check if command starts with --get
            if (argc != 2) { print_usage(); return 1; }
            uint16_t did = 0;
            bool is_float = false;
            if (command == "--get-lead-speed") did = DID_LEAD_VEHICLE_SPEED;
            else if (command == "--get-own-speed") did = DID_OWN_VEHICLE_SPEED;
            else if (command == "--get-gap") did = DID_ACC_GAP_SETTING;
            else if (command == "--get-kp") { did = DID_ACC_KP; is_float = true; }
            else if (command == "--get-ki") { did = DID_ACC_KI; is_float = true; }
            else { print_usage(); return 1; }
            
            std::vector<uint8_t> payload = {UDS_READ_DATA_BY_IDENTIFIER, (uint8_t)(did >> 8), (uint8_t)did};
            if (!send_and_receive(socket, 0x8001, payload, response_payload) || response_payload.size() < 4) return 1;
            
            float value = response_payload[3];
            if (is_float) {
                value /= 10.0f;
            }
            std::cout << "[CLIENT] Read value for " << command << ": " << value << std::endl;

        } else if (command.rfind("--set", 0) == 0) { // Check if command starts with --set
            if (argc != 3) { print_usage(); return 1; }
            uint16_t did = 0;
            uint8_t value = 0;

            if (command == "--set-lead-speed") {
                did = DID_LEAD_VEHICLE_SPEED;
                value = static_cast<uint8_t>(std::stoi(argv[2]));
            } else if (command == "--set-gap") {
                did = DID_ACC_GAP_SETTING;
                value = static_cast<uint8_t>(std::stoi(argv[2]));
            } else if (command == "--set-kp") {
                did = DID_ACC_KP;
                value = static_cast<uint8_t>(std::stof(argv[2]) * 10.0f);
            } else if (command == "--set-ki") {
                did = DID_ACC_KI;
                value = static_cast<uint8_t>(std::stof(argv[2]) * 10.0f);
            } else if (command == "--set-max-accel") {
                did = DID_ACC_MAX_ACCEL;
                value = static_cast<uint8_t>(std::stof(argv[2]) * 10.0f);
            } else if (command == "--set-max-decel") {
                did = DID_ACC_MAX_DECEL;
                value = static_cast<uint8_t>(std::stof(argv[2]) * 10.0f);
            } else { 
                print_usage(); return 1; 
            }
            
            std::vector<uint8_t> payload = {UDS_WRITE_DATA_BY_IDENTIFIER, (uint8_t)(did >> 8), (uint8_t)did, value};
            if (!send_and_receive(socket, 0x8001, payload, response_payload)) return 1;

        } else if (command == "--update") {
             if (argc != 3) { print_usage(); return 1; }
            std::string file_path = argv[2];
            auto new_firmware_hash_opt = calculate_file_hash(file_path);
            if (!new_firmware_hash_opt) return 1;
            std::ifstream file(file_path, std::ios::binary);
            if (!file.is_open()) return 1;
            file.seekg(0, std::ios::end);
            uint32_t file_size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::vector<uint8_t> req_payload = {UDS_REQUEST_DOWNLOAD, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, (uint8_t)(file_size >> 24), (uint8_t)(file_size >> 16), (uint8_t)(file_size >> 8), (uint8_t)file_size};
            if (!send_and_receive(socket, 0x8001, req_payload, response_payload)) return 1;
            const size_t CHUNK_SIZE = 4096;
            std::vector<char> buffer(CHUNK_SIZE);
            uint8_t block_counter = 1;
            while (file.read(buffer.data(), buffer.size())) {
                std::vector<uint8_t> transfer_payload = {UDS_TRANSFER_DATA, block_counter++};
                transfer_payload.insert(transfer_payload.end(), buffer.begin(), buffer.end());
                if (!send_and_receive(socket, 0x8001, transfer_payload, response_payload)) return 1;
            }
            if (file.gcount() > 0) {
                std::vector<uint8_t> transfer_payload = {UDS_TRANSFER_DATA, block_counter++};
                transfer_payload.insert(transfer_payload.end(), buffer.begin(), buffer.begin() + file.gcount());
                if (!send_and_receive(socket, 0x8001, transfer_payload, response_payload)) return 1;
            }
            std::vector<uint8_t> exit_payload = {UDS_REQUEST_TRANSFER_EXIT};
            exit_payload.insert(exit_payload.end(), new_firmware_hash_opt->begin(), new_firmware_hash_opt->end());
            if (!send_and_receive(socket, 0x8001, exit_payload, response_payload)) return 1;
        } else {
            print_usage();
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Client Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

void print_usage() {
    std::cerr << "Usage: doip_client <command> [options]" << std::endl;
    std::cerr << "Commands:" << std::endl;
    std::cerr << "  --identify                  Get Vehicle VIN" << std::endl;
    std::cerr << "  --program                   Enter Programming Session for OTA" << std::endl;
    std::cerr << "  --update <file>             Perform OTA update with a file" << std::endl;
    std::cerr << "  --get-lead-speed            Read lead vehicle speed" << std::endl;
    std::cerr << "  --get-own-speed             Read own vehicle speed" << std::endl;
    std::cerr << "  --set-lead-speed <mph>      Set lead vehicle speed" << std::endl;
    std::cerr << "  --get-gap                   Read ACC following gap" << std::endl;
    std::cerr << "  --set-gap <cars>            Set ACC following gap" << std::endl;
    std::cerr << "  --get-kp                    Read ACC Proportional Gain" << std::endl;
    std::cerr << "  --set-kp <value>            Set ACC Proportional Gain (e.g., 0.4)" << std::endl;
    std::cerr << "  --get-ki                    Read ACC Integral Gain" << std::endl;
    std::cerr << "  --set-ki <value>            Set ACC Integral Gain (e.g., 0.1)" << std::endl;
}

bool send_and_receive(tcp::socket& socket, uint16_t type, const std::vector<uint8_t>& payload, std::vector<uint8_t>& response_payload) {
    DoIPHeader header = {0x02, (uint8_t)~0x02, htons(type), htonl((uint32_t)payload.size())};
    std::vector<boost::asio::const_buffer> request_buffers;
    request_buffers.push_back(boost::asio::buffer(&header, sizeof(header)));
    if (!payload.empty()) {
        request_buffers.push_back(boost::asio::buffer(payload));
    }
    boost::asio::write(socket, request_buffers);
    DoIPHeader response_header;
    boost::asio::read(socket, boost::asio::buffer(&response_header, sizeof(response_header)));
    response_header.payload_type = ntohs(response_header.payload_type);
    response_header.payload_length = ntohl(response_header.payload_length);
    response_payload.resize(response_header.payload_length);
    if (response_header.payload_length > 0) {
        boost::asio::read(socket, boost::asio::buffer(response_payload));
    }
    if (response_header.payload_type == 0x8002 || (!response_payload.empty() && response_payload[0] == 0x7F)) {
        std::cerr << "--- FAILED: ECU returned a Negative Response. ---" << std::endl;
        return false;
    }
    std::cout << "--- SUCCESS ---" << std::endl;
    return true;
}

std::optional<std::string> calculate_file_hash(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return std::nullopt;
    EVP_MD_CTX* md_context = EVP_MD_CTX_new();
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    EVP_DigestInit_ex(md_context, EVP_sha256(), NULL);
    char buffer[1024];
    while (file.read(buffer, sizeof(buffer))) {
        EVP_DigestUpdate(md_context, buffer, file.gcount());
    }
    if(file.gcount() > 0) {
        EVP_DigestUpdate(md_context, buffer, file.gcount());
    }
    EVP_DigestFinal_ex(md_context, hash, &hash_len);
    EVP_MD_CTX_free(md_context);
    std::stringstream ss;
    for (unsigned int i = 0; i < hash_len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}
