#pragma once

#include "utils.hpp"

namespace boost {
namespace asio {
namespace ssl {
class context;
}
class io_context;
}
}

namespace net = boost::asio;
namespace ssl = net::ssl;

namespace korrelator {

namespace details {
class kucoin_spots_plug;
class kucoin_futures_plug;
}

class kucoin_trader {
  trade_type_e const m_tradeType;
  union {
    details::kucoin_futures_plug* futures;
    details::kucoin_spots_plug* spot;
  } m_exchangePlug;

public:
  kucoin_trader(net::io_context &, ssl::context &,
                trade_type_e const tradeType,
                api_data_t const &apiData, trade_config_data_t*,
                int const errorMaxRetries);

  ~kucoin_trader();
  void setPrice(double const price);
  void startConnect();

  double quantityPurchased() const;
  double sizePurchased() const;
  QString errorString() const;
};

double format_quantity(double const value, int decimal_places);
}

