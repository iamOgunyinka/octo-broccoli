#include "utilities.hpp"
#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <filesystem>
#include <fstream>
#include <openssl/sha.h>
#include <random>
#include <vector>

#if _MSC_VER && !__INTEL_COMPILER
#pragma warning(disable : 4996)
#endif

namespace korrelator {
using namespace fmt::v6::literals;

namespace utilities {

std::string decode_url(boost::string_view const &encoded_string) {
  std::string src{};
  for (size_t i = 0; i < encoded_string.size();) {
    char c = encoded_string[i];
    if (c != '%') {
      src.push_back(c);
      ++i;
    } else {
      char c1 = encoded_string[i + 1];
      unsigned int localui1 = 0L;
      if ('0' <= c1 && c1 <= '9') {
        localui1 = c1 - '0';
      } else if ('A' <= c1 && c1 <= 'F') {
        localui1 = c1 - 'A' + 10;
      } else if ('a' <= c1 && c1 <= 'f') {
        localui1 = c1 - 'a' + 10;
      }

      char c2 = encoded_string[i + 2];
      unsigned int localui2 = 0L;
      if ('0' <= c2 && c2 <= '9') {
        localui2 = c2 - '0';
      } else if ('A' <= c2 && c2 <= 'F') {
        localui2 = c2 - 'A' + 10;
      } else if ('a' <= c2 && c2 <= 'f') {
        localui2 = c2 - 'a' + 10;
      }

      unsigned int ui = localui1 * 16 + localui2;
      src.push_back(ui);

      i += 3;
    }
  }

  return src;
}

std::map<boost::string_view, boost::string_view>
parse_headers(std::string_view const &str) {
  std::map<boost::string_view, boost::string_view> header_map{};
  std::vector<boost::string_view> headers = utilities::split_string_view(
      boost::string_view(str.data(), str.size()), "\r\n");
  for (auto iter = headers.begin(); iter != headers.end(); ++iter) {
    auto header_key_value = utilities::split_string_view(*iter, ": ");
    if (header_key_value.size() != 2)
      continue;
    header_map[header_key_value[0]] = header_key_value[1];
  }
  return header_map;
}

void normalize_paths(std::string &str) {
  for (std::string::size_type i = 0; i != str.size(); ++i) {
    if (str[i] == '#')
      str[i] = '\\';
  }
};

void remove_file(std::string &filename) {
  std::error_code ec{};
  normalize_paths(filename);
  if (std::filesystem::exists(filename))
    std::filesystem::remove(filename, ec);
}

std::string view_to_string(boost::string_view const &str_view) {
  std::string str{str_view.begin(), str_view.end()};
  boost::trim(str);
  return str;
}

std::string str_to_sha1hash(std::string const &str) {
  std::vector<unsigned char> output_buf(SHA_DIGEST_LENGTH);
  SHA1((unsigned const char *)str.c_str(), str.size(), output_buf.data());
  std::basic_string<char> out_param(SHA_DIGEST_LENGTH * 2, '\0');
  char *out_param_buf = out_param.data();
  for (unsigned int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
    sprintf(out_param_buf, "%02x", output_buf[i]);
    out_param_buf += 2;
  }
  return out_param;
}

std::string_view bv2sv(boost::string_view view) {
  return std::string_view(view.data(), view.size());
}

std::string intlist_to_string(std::vector<uint32_t> const &vec) {
  std::ostringstream ss{};
  if (vec.empty())
    return {};
  for (std::size_t i = 0; i != vec.size() - 1; ++i) {
    ss << vec[i] << ", ";
  }
  ss << vec.back();
  return ss.str();
}

std::string get_todays_date() {
  std::string out_buf{};
  std::time_t const now_time = std::time(nullptr);
  auto const result = timet_to_string(out_buf, now_time);
  if (result == std::string::npos)
    return {};
  out_buf.resize(result);
  return out_buf;
}

std::vector<boost::string_view> split_string_view(boost::string_view const &str,
                                                  char const *delim) {
  std::size_t const delim_length = std::strlen(delim);
  std::size_t from_pos{};
  std::size_t index{str.find(delim, from_pos)};
  if (index == std::string::npos)
    return {str};
  std::vector<boost::string_view> result{};
  while (index != std::string::npos) {
    result.emplace_back(str.data() + from_pos, index - from_pos);
    from_pos = index + delim_length;
    index = str.find(delim, from_pos);
  }
  if (from_pos < str.length())
    result.emplace_back(str.data() + from_pos, str.size() - from_pos);
  return result;
}

std::size_t timet_to_string(std::string &output, std::size_t t,
                            char const *format) {
  std::time_t current_time = t;
#if _MSC_VER && !__INTEL_COMPILER
#pragma warning(disable : 4996)
#endif
  auto tm_t = std::localtime(&current_time);

  if (!tm_t)
    return std::string::npos;
  output.clear();
  output.resize(32);
  return std::strftime(output.data(), output.size(), format, tm_t);
}

} // namespace utilities
} // namespace korrelator
