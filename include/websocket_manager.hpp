#pragma once

#include <QString>
#include <map>
#include <variant>
#include <vector>

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

class binance_ws;
class kucoin_ws;

enum class trade_type_e;
enum class exchange_name_e;

net::ssl::context &getSSLContext();
net::io_context* getIOContext();

class websocket_manager {

  struct exchange_trade_pair {
    trade_type_e tradeType;
    QString tokenName;
  };

  using socket_variant = std::variant<binance_ws*, kucoin_ws*>;
public:
  websocket_manager();
  ~websocket_manager();
  void addSubscription(QString const &tokenName, trade_type_e const tradeType,
                       exchange_name_e const exchange, double& result);
  void startWatch();

private:
  net::ssl::context &m_sslContext;
  net::io_context *m_ioContext = nullptr;
  std::vector<socket_variant> m_sockets;
  std::map<exchange_name_e, std::vector<exchange_trade_pair>> m_checker;
};

} // namespace korrelator
