#pragma once

#include <string>

namespace korrelator {

inline namespace utils {

enum class trade_type_e { spot, futures };
enum class exchange_name_e { binance, kucoin, none };

struct internal_address_t {
  std::string tokenName;
  bool subscribed = false;
};

} // namespace utils

enum class trade_action_e { buy, sell, do_nothing };

enum tick_line_type_e { normal, ref, all, special };

} // namespace korrelator
