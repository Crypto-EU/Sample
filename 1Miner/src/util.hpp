#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace oneminer {

std::string trim(std::string s);
std::vector<std::string> split(const std::string& s, char delim);
bool parse_host_port(const std::string& endpoint, std::string& host, uint16_t& port);
int64_t now_us();
std::string now_log_time();
void log_info(const std::string& msg);
void log_warn(const std::string& msg);
void log_error(const std::string& msg);

// Infer TLS from well-known RabbitMiner Saseul ports.
// 1901/1921 = SSL, 1911/1931 = plain TCP. Unknown ports: use hint.
bool endpoint_should_use_tls(uint16_t port, bool default_tls);

}  // namespace oneminer
