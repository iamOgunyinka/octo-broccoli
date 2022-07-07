#pragma once

#include <boost/asio/io_context.hpp>
#include "utils.hpp"

namespace boost {
namespace asio {
namespace ssl {
class context;
}
}
}

namespace net = boost::asio;
namespace ssl = net::ssl;

namespace korrelator {

namespace details {
class binance_spots_plug;
class binance_futures_plug;
}


class binance_trader {
  trade_type_e m_tradeType;
  union {
    details::binance_spots_plug* spot;
    details::binance_futures_plug* futures;
  } m_binancePlug;
public:
  binance_trader(net::io_context &, ssl::context &,
                     trade_type_e const tradeType, api_data_t const &apiData,
                     trade_config_data_t *tradeConfig);
  ~binance_trader();
  void setLeverage();
  void setPrice(double const price);
  double averagePrice() const;
  QString errorString() const;
  void startConnect();
};

double format_quantity(double const value, int decimal_places);
net::io_context& getExchangeIOContext();

} // namespace korrelator
