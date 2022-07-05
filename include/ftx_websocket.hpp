#pragma once

#include <QString>

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/asio/deadline_timer.hpp>
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

class ftx_websocket
{
  using resolver = ip::tcp::resolver;
  using resolver_result_type = resolver::results_type;
  using address_list_t = std::vector<internal_address_t>;
  using results_type = resolver::results_type;

  enum class step_e {
    unsubscribed,
    subscribed,
    ticker_data
  };

private:
  net::io_context& m_ioContext;
  net::ssl::context& m_sslContext;
  double& m_priceResult;
  std::optional<beast::websocket::stream<beast::ssl_stream<
    beast::tcp_stream>>> m_webStream = std::nullopt;
  std::optional<net::deadline_timer> m_pingTimer = std::nullopt;
  std::optional<resolver> m_resolver = std::nullopt;
  std::optional<beast::flat_buffer> m_readBuffer = std::nullopt;
  std::string m_writerBuffer;
  internal_address_t m_tokenInfo;
  bool m_requestedToStop = false;
  bool m_isSpot = false;
  step_e m_step;

private:
  void readSubscriptionResponse();
  void readTickerResponse();
  void performSubscriptionToChannel();
  void waitForMessages();
  void interpretGenericMessages();
  void negotiateWebsocketConnection();
  void performWebsocketHandshake();
  void websockConnectToResolvedNames(results_type const & results);
  void websockPerformSslHandshake();
public:
  ftx_websocket(net::io_context& ioContext, net::ssl::context& sslContext,
                double &priceResult, trade_type_e const tradeType);
  ~ftx_websocket();
  void startFetching();
  void requestStop() { m_requestedToStop = true; }
  void addSubscription(QString const &tokenName) {
    m_tokenInfo = internal_address_t{tokenName, false};
  }
};

}
