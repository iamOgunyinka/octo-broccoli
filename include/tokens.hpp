#pragma once

#include "utils.hpp"
#include <QString>
#include <map>
#include <mutex>
#include <optional>
#include <vector>
#include <functional>
#include <memory>

class QCPGraph;
class QCPLayoutGrid;

namespace korrelator {

struct cross_over_data_t {
  double signalPrice = 0.0;
  double openPrice = 0.0;
  trade_action_e action = trade_action_e::nothing;
  QString time;
};

class token_t {
public:
  bool calculatingNewMinMax = true;
  bool crossedOver = false;

  int8_t pricePrecision = -1;
  int8_t quantityPrecision = pricePrecision;
  int8_t baseAssetPrecision = pricePrecision;
  int8_t quotePrecision = pricePrecision;

  double minPrice = std::numeric_limits<double>::max();
  double maxPrice = -1 * std::numeric_limits<double>::max();
  double prevNormalizedPrice = std::numeric_limits<double>::max();
  double alpha = 1.0;
  double normalizedPrice = 0.0;
  double multiplier = 1.0; // only used by kucoin
  double tickSize = 0.0; // used as the stepSize in Binance
  double baseMinSize = 0.0; // The minimum quantity requried to place an order
  double quoteMinSize = 0.0; // The minimum funds required to place a market order
  qint64 graphPointsDrawnCount = 0;
  std::shared_ptr<double> realPrice;
  QCPGraph *graph = nullptr;

  std::optional<cross_over_data_t> crossOver;
  trade_type_e tradeType;
  exchange_name_e exchange = exchange_name_e::none;

  QString baseCurrency;
  QString quoteCurrency;
  QString symbolName;
  QString legendName;
  void reset();
};

struct token_compare_t {
  bool operator()(QString const &tokenName, token_t const &t) const {
    return tokenName.toUpper() < t.symbolName.toUpper();
  }
  bool operator()(token_t const &t, QString const &tokenName) const {
    return t.symbolName.toUpper() < tokenName.toUpper();
  }

  bool operator()(token_t const &a, token_t const &b) const {
    return std::tuple(a.symbolName.toUpper(), a.tradeType, a.exchange) <
           std::tuple(b.symbolName.toUpper(), b.tradeType, b.exchange);
  }
};

using token_list_t = std::vector<token_t>;
using token_map_t = std::map<QString, token_t>;
using success_callback_t = std::function<void(korrelator::token_list_t &&, exchange_name_e)>;
using error_callback_t = std::function<void(QString const &)>;

} // namespace korrelator
