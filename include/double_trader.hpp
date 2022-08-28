#pragma once

#include <functional>
#include "plug_data.hpp"

namespace boost {

namespace asio {

namespace ssl {

class context;

} // ssl

class io_context;

} // namespace asio

} // namespace boost

namespace korrelator {

namespace net = boost::asio;

class binance_trader;
class kucoin_trader;
class ftx_trader;
class order_model;

class double_trader_t {
public:
    double_trader_t(std::function<void()> refreshModel,
                 std::unique_ptr<order_model> &model, int &maxRetries);
    void operator()(plug_data_t &&firstMetaData, plug_data_t &&secondMetadata);

private:
  union connector_t {
    kucoin_trader* kucoinTrader;
    binance_trader* binanceTrader;
    ftx_trader* ftxTrader;
  };

  net::io_context& m_ioContext;
  net::ssl::context& m_sslContext;
  int& m_maxRetries;
  std::unique_ptr<order_model>& m_model;
  std::function<void()> m_modelRefreshCallback = nullptr;
  trade_action_e m_lastAction = trade_action_e::nothing;
  double m_lastQuantity = NAN;
  bool m_futuresLeverageIsSet = false;
  bool m_isFirstTrade = true;
};

net::ssl::context &getSSLContext();
}
