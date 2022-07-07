#pragma once

#include "utils.hpp"

namespace boost {
namespace asio {
namespace ssl {
class context;
}
class io_context;
} // namespace asio
} // namespace boost

namespace net = boost::asio;
namespace ssl = net::ssl;

namespace korrelator {

namespace details {
class ftx_spots_plug;
class ftx_futures_plug;
} // namespace details

class ftx_trader {
  trade_type_e const m_tradeType;
  union {
    details::ftx_futures_plug *futures;
    details::ftx_spots_plug *spot;
  } m_exchangePlug;

public:
  ftx_trader(net::io_context &, ssl::context &, trade_type_e const tradeType,
             api_data_t const &apiData, trade_config_data_t *);

  ~ftx_trader();
  void setPrice(double const price);
  void startConnect();
  void setAccountLeverage();

  double getAveragePrice() const;
  QString errorString() const;
};

double format_quantity(double const value, int decimal_places);
} // namespace korrelator
