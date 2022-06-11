#pragma once

#include <string>
#include <string_view>

namespace korrelator {

std::string base64_encode(std::basic_string<unsigned char> const &binary_data);
std::string base64_encode(std::string const &binary_data);
std::string base64_decode(std::string const &asc_data);
std::basic_string<unsigned char> hmac256_encode(std::string const &data,
                                                std::string const &key,
                                                bool const toHex = false);
time_t getGMTTimeMs();
} // namespace korrelator
