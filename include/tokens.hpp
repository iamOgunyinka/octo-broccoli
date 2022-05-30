#pragma once

#include "utils.hpp"
#include <QString>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

class QCPGraph;
class QCPLayoutGrid;

namespace korrelator {

struct cross_over_data_t {
  double price = 0.0;
  trade_action_e action = trade_action_e::do_nothing;
  QString time;
};

class token_t {
  friend class token_proxy_iter;

public:
  double minPrice = std::numeric_limits<double>::max();
  double maxPrice = -1 * std::numeric_limits<double>::max();
  double prevNormalizedPrice = std::numeric_limits<double>::max();
  double alpha = 1.0;
  double normalizedPrice = 0.0;
  double realPrice = 0.0;
  qint64 graphPointsDrawnCount = 0;
  std::optional<cross_over_data_t> crossOver;
  QString tokenName;
  QString legendName;
  QCPGraph *graph = nullptr;
  bool calculatingNewMinMax = true;
  bool crossedOver = false;
  trade_type_e tradeType;
  exchange_name_e exchange = exchange_name_e::none;

  void reset();
};

struct token_compare_t {
  bool operator()(QString const &tokenName, token_t const &t) const {
    return tokenName.toUpper() < t.tokenName.toUpper();
  }
  bool operator()(token_t const &t, QString const &tokenName) const {
    return t.tokenName.toUpper() < tokenName.toUpper();
  }

  bool operator()(token_t const &a, token_t const &b) const {
    return std::tuple(a.tokenName.toUpper(), a.tradeType, a.exchange) <
           std::tuple(b.tokenName.toUpper(), b.tradeType, b.exchange);
  }
};

using token_list_t = std::vector<token_t>;
using token_map_t = std::map<QString, token_t>;
} // namespace korrelator
