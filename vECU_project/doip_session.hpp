#pragma once

#include <iostream>
#include <fstream>
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <cstdio>
#include <boost/asio.hpp>

#include "ecu_state.hpp"
#include "nvram_manager.hpp" // Include NVRAM manager

// Forward declare global state variables and functions from main.cpp
extern std::atomic<EcuState> g_ecu_state;
extern std::string g_executable_path;
extern NVRAMManager g_nvram; // Access the global NVRAM object
extern std::optional<std::string> calculate_file_hash(const std::string& file_path);
extern void apply_update(const std::string& current_executable_path);

using boost::asio::ip::tcp;

#pragma pack(push, 1)
struct DoIPHeader {
    uint8_t  protocol_version;
    uint8_t  inverse_protocol_version;
    uint16_t payload_type;
    uint32_t payload_length;
};
#pragma pack(pop)

// Define UDS Service IDs
const uint8_t UDS_WRITE_DATA_BY_IDENTIFIER = 0x2E;
const uint8_t UDS_ROUTINE_CONTROL = 0x31;
const uint8_t UDS_REQUEST_DOWNLOAD = 0x34;
const uint8_t UDS_TRANSFER_DATA = 0x36;
const uint8_t UDS_REQUEST_TRANSFER_EXIT = 0x37;

// Define Data Identifiers for service 0x2E
const uint16_t DID_LEAD_VEHICLE_SPEED = 0xF101;
const uint16_t DID_ACC_GAP_SETTING = 0xF102;


class DoIPSession : public std::enable_shared_from_this<DoIPSession> {
public:
    DoIPSession(tcp::socket socket)
        : m_socket(std::move(socket)),
          m_firmware_file_size(0),
          m_bytes_received(0)
    {}

    void start() {
        do_read_header();
    }

private:
    void do_read_header() {
        auto self = shared_from_this();
        boost::asio::async_read(m_socket,
            boost::asio::buffer(&m_received_header, sizeof(DoIPHeader)),
            [this, self](const boost::system::error_code& ec, std::size_t length) {
                if (!ec) {
                    m_received_header.payload_type = ntohs(m_received_header.payload_type);
                    m_received_header.payload_length = ntohl(m_received_header.payload_length);
                    do_read_payload();
                } else if (ec != boost::asio::error::eof) {
                    std::cerr << "[SESSION] Error reading header: " << ec.message() << std::endl;
                }
            });
    }

    void do_read_payload() {
        auto self = shared_from_this();
        m_payload.resize(m_received_header.payload_length);

        if (m_received_header.payload_length == 0) {
            process_message();
            return;
        }

        boost::asio::async_read(m_socket,
            boost::asio::buffer(m_payload.data(), m_received_header.payload_length),
            [this, self](const boost::system::error_code& ec, std::size_t length) {
                if (!ec) {
                    process_message();
                } else if (ec != boost::asio::error::eof) {
                    std::cerr << "[SESSION] Error reading payload: " << ec.message() << std::endl;
                }
            });
    }

    void process_message() {
        switch (m_received_header.payload_type) {
            case 0x0004: // Vehicle Identification Request
                do_write_vehicle_announcement();
                break;
            case 0x8001: // UDS Message
                handle_uds_message();
                break;
            default:
                do_read_header();
                break;
        }
    }

    void handle_uds_message() {
        if (m_payload.empty()) {
            do_read_header();
            return;
        }

        uint8_t service_id = m_payload[0];
        std::vector<uint8_t> response_payload;

        switch (service_id) {
            case UDS_WRITE_DATA_BY_IDENTIFIER: {
                if (m_payload.size() < 4) {
                    break;
                }
                uint16_t data_id = (m_payload[1] << 8) | m_payload[2];
                uint8_t value = m_payload[3];

                std::cout << "[SESSION] Received Write Data By ID. ID: 0x" << std::hex << data_id << ", Value: " << std::dec << (int)value << std::endl;

                if (data_id == DID_LEAD_VEHICLE_SPEED) {
                    g_nvram.set_string("LEAD_VEHICLE_SPEED", std::to_string(value));
                } else if (data_id == DID_ACC_GAP_SETTING) {
                    g_nvram.set_string("ACC_GAP_SETTING", std::to_string(value));
                }
                g_nvram.save();
                
                response_payload.push_back(0x6E); // Positive Response for 0x2E
                response_payload.push_back(m_payload[1]);
                response_payload.push_back(m_payload[2]);
                do_write_generic_response(0x8001, response_payload);
                return;
            }

            case UDS_ROUTINE_CONTROL: {
                if (m_payload.size() < 4) {
                    break;
                }
                uint16_t routine_id = (m_payload[2] << 8) | m_payload[3];
                if (routine_id == 0xFF00) {
                    g_ecu_state = EcuState::UPDATE_PENDING;
                    response_payload.push_back(0x71);
                    response_payload.insert(response_payload.end(), m_payload.begin() + 1, m_payload.end());
                    do_write_generic_response(0x8001, response_payload);
                    return;
                }
                break;
            }

            case UDS_REQUEST_DOWNLOAD: {
                if (g_ecu_state != EcuState::UPDATE_PENDING || m_payload.size() < 10) {
                    break;
                }
                m_firmware_file_size = (m_payload[6] << 24) | (m_payload[7] << 16) | (m_payload[8] << 8) | m_payload[9];
                m_update_file.open("update.bin", std::ios::binary | std::ios::trunc);
                if (!m_update_file.is_open()) {
                    break;
                }
                m_bytes_received = 0;
                response_payload.push_back(0x74);
                response_payload.push_back(0x20);
                response_payload.push_back(0x10);
                response_payload.push_back(0x00);
                do_write_generic_response(0x8001, response_payload);
                return;
            }

            case UDS_TRANSFER_DATA: {
                if (g_ecu_state != EcuState::UPDATE_PENDING || !m_update_file.is_open()) {
                    break;
                }
                const char* data_to_write = reinterpret_cast<const char*>(m_payload.data() + 2);
                size_t data_size = m_payload.size() - 2;
                m_update_file.write(data_to_write, data_size);
                m_bytes_received += data_size;
                response_payload.push_back(0x76);
                response_payload.push_back(m_payload[1]);
                do_write_generic_response(0x8001, response_payload);
                return;
            }

            case UDS_REQUEST_TRANSFER_EXIT: {
                if (g_ecu_state != EcuState::UPDATE_PENDING || !m_update_file.is_open()) {
                    break;
                }
                m_update_file.close();
                auto calculated_hash_opt = calculate_file_hash("update.bin");
                if (!calculated_hash_opt) {
                    break;
                }
                std::string expected_hash_hex(m_payload.begin() + 1, m_payload.end());
                if (*calculated_hash_opt == expected_hash_hex) {
                    response_payload.push_back(0x77);
                    do_write_generic_response(0x8001, response_payload);
                    apply_update(g_executable_path);
                } else {
                    std::cerr << "[SESSION] !!! INTEGRITY CHECK FAILED for new firmware !!!" << std::endl;
                    do_write_generic_response(0x8002, {});
                }
                return;
            }
            
            default:
                break;
        }

        // Default case for any unhandled service ID or a 'break' from a case
        std::cout << "[SESSION] Received unsupported or out-of-sequence UDS command." << std::endl;
        do_write_generic_response(0x8002, {});
    }

    void do_write_generic_response(uint16_t payload_type, const std::vector<uint8_t>& payload) {
        auto self = shared_from_this();
        auto response_header = std::make_shared<DoIPHeader>();
        response_header->protocol_version = 0x02;
        response_header->inverse_protocol_version = ~response_header->protocol_version;
        response_header->payload_type = htons(payload_type);
        response_header->payload_length = htonl(payload.size());

        std::vector<boost::asio::const_buffer> buffers;
        buffers.push_back(boost::asio::buffer(response_header.get(), sizeof(DoIPHeader)));
        if (!payload.empty()) {
            buffers.push_back(boost::asio::buffer(payload));
        }

        boost::asio::async_write(m_socket, buffers,
            [this, self, response_header](const boost::system::error_code& ec, std::size_t bytes) {
                if (!ec) {
                    do_read_header();
                } else {
                    std::cerr << "[SESSION] Error on write: " << ec.message() << std::endl;
                }
            });
    }

    void do_write_vehicle_announcement() {
        std::string vin = "VECU-SIM-1234567";
        std::vector<uint8_t> payload(vin.begin(), vin.end());
        do_write_generic_response(0x0005, payload);
    }

    tcp::socket m_socket;
    DoIPHeader m_received_header;
    std::vector<uint8_t> m_payload;
    std::ofstream m_update_file;
    uint32_t m_firmware_file_size;
    uint32_t m_bytes_received;
};
