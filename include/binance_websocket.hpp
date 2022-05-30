#pragma once

#include <QString>

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <optional>

#include "constants.hpp"
#include "utils.hpp"

namespace boost {

namespace asio {

namespace ssl {

class context;

}

class io_context;

} // namespace asio

} // namespace boost

namespace korrelator {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace ip = net::ip;

class binance_ws {

  using resolver_result_type = net::ip::tcp::resolver::results_type;
  using address_list_t = std::vector<internal_address_t>;

public:
  using resolver = ip::tcp::resolver;
  using results_type = resolver::results_type;

  binance_ws(net::io_context &ioContext, net::ssl::context &sslContext,
             double &priceResult, trade_type_e const tradeType)
      : m_host(tradeType == trade_type_e::spot
                   ? constants::binance_ws_spot_url
                   : constants::binance_ws_futures_url),
        m_port(tradeType == trade_type_e::spot
                   ? constants::binance_ws_spot_port
                   : constants::binance_ws_futures_port),
        m_ioContext(ioContext), m_sslContext(sslContext),
        m_priceResult(priceResult) {}

  ~binance_ws();
  void startFetching();
  void requestStop() { m_requestedToStop = true; }
  void addSubscription(QString const &tokenName) {
    m_tokenName = internal_address_t{tokenName, false};
  }

private:
  binance_ws *shared_from_this() { return this; }
  void interpretGenericMessages();
  void websockConnectToResolvedNames(resolver_result_type const &);
  void websockPerformSslHandshake(resolver_result_type::endpoint_type const &);
  void negotiateWebsocketConnection();
  void performWebsocketHandshake();
  void waitForMessages();
  void makeSubscription();

private:
  std::string const m_host;
  std::string const m_port;
  net::io_context &m_ioContext;
  net::ssl::context &m_sslContext;
  internal_address_t m_tokenName;
  std::optional<resolver> m_resolver = std::nullopt;
  std::optional<beast::websocket::stream<beast::ssl_stream<beast::tcp_stream>>>
      m_sslWebStream;
  std::optional<beast::flat_buffer> m_readBuffer;
  std::string m_writeBuffer;
  double &m_priceResult;
  bool m_requestedToStop = false;
};

double binanceGetCoinPrice(char const *str, size_t const size);

} // namespace korrelator
