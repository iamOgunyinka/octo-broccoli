#pragma once

#include <string>

namespace korrelator {

inline namespace utils {

enum class trade_type_e { spot, futures };
enum class exchange_name_e {
  binance, kucoin, none
};

struct internal_address_t {
  std::string tokenName;
  bool        subscribed = false;
};

} // namespace utils

} // namespace korrelator
