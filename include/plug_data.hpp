#pragma once

#include "utils.hpp"

namespace korrelator {

struct plug_data_t {
  api_data_t apiInfo;
  QString correlatorID;
  trade_config_data_t *tradeConfig;
  trade_type_e tradeType;
  exchange_name_e exchange;
  time_t currentTime;
  double tokenPrice;
  // for kucoin only
  double multiplier = 0.0;
  double tickSize = 0.0;
};

}
