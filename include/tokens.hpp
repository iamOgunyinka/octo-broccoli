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
  cross_over_data_t() = default;
};

class token_t {
  friend class token_proxy_iter;

public:
  double minPrice = std::numeric_limits<double>::max();
  double maxPrice = -1 * std::numeric_limits<double>::max();
  double prevNormalizedPrice = std::numeric_limits<double>::max();
  double alpha = 1.0;
  qint64 graphPointsDrawnCount = 0;
  std::optional<cross_over_data_t> crossOver;
  QString tokenName;
  QString legendName;
  mutable QCPGraph *graph = nullptr;
  bool calculatingNewMinMax = true;
  bool crossedOver = false;
  bool isReferenceType = false;
  trade_type_e tradeType;
  utils::exchange_name_e exchange = exchange_name_e::none;

  double getNormalizedPrice() const { return normalizedPrice; }
  void setNormalizedPriceNoRace(double const d) { normalizedPrice = d; }
  void reset();

private:
  double normalizedPrice = 0.0;
  double realPrice = 0.0;
};

struct token_compare_t {
  bool operator()(QString const &tokenName, token_t const &t) const {
    return tokenName < t.tokenName;
  }
  bool operator()(token_t const &t, QString const &tokenName) const {
    return t.tokenName < tokenName;
  }

  bool operator()(token_t const &a, token_t const &b) const {
    return std::tie(a.tokenName, a.tradeType, a.exchange) <
           std::tie(b.tokenName, b.tradeType, b.exchange);
  }
};

using token_list_t = std::vector<token_t>;

void updateTokenIter(token_list_t::iterator iter, double const price);

class token_proxy_iter {
public:
  using iter_t = std::vector<token_t>::iterator;

  token_proxy_iter(iter_t iter) : m_iter(iter) {}
  auto &value() { return *m_iter; }
  token_t const &value() const { return *m_iter; }

  trade_type_e tradeType() const { return m_iter->tradeType; }

  exchange_name_e exchange() const { return m_iter->exchange; }

  double getRealPrice() const {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    return m_iter->realPrice;
  }

  void setRealPrice(double const price) const {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    m_iter->realPrice = price;
    updateTokenIter(m_iter, price);
    m_iter->normalizedPrice =
        (price - m_iter->minPrice) / (m_iter->maxPrice - m_iter->minPrice);
  }

private:
  mutable std::mutex m_mutex;
  iter_t m_iter;
};

using token_map_t = std::map<QString, token_t>;
using token_proxy_iter_ptr = std::unique_ptr<token_proxy_iter>;
} // namespace korrelator
