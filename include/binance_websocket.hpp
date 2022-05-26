#pragma once

#include <QObject>

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <functional>
#include <optional>

#include "utils.hpp"

namespace boost {

namespace asio {

namespace ssl {

class context;

}

class io_context;

}

}

namespace korrelator {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace ip = net::ip;

using price_callback =
    std::function<void(QString const &, double const, trade_type_e const)>;

class binance_ws : public QObject {

  Q_OBJECT

  using resolver_result_type = net::ip::tcp::resolver::results_type;

  static char const *const futures_url;
  static char const *const spot_url;
  static char const *const spot_port;
  static char const *const futures_port;

  using address_list_t = std::vector<internal_address_t>;

public:
  using resolver = ip::tcp::resolver;
  using results_type = resolver::results_type;

  binance_ws(net::io_context &ioContext, net::ssl::context &sslContext,
                trade_type_e const tradeType)
      : m_tradeType(tradeType)
      , m_host(m_tradeType == trade_type_e::spot ? spot_url : futures_url)
      , m_port(m_tradeType == trade_type_e::spot ? spot_port : futures_port)
      , m_ioContext(ioContext)
      , m_sslContext(sslContext){}

  ~binance_ws();
  void startFetching();
  void requestStop(){ m_requestedToStop = true; }
  void addSubscription(QString const &tokenName) {
    m_tokenName = internal_address_t{tokenName, false};
  }

signals:
  void onNewPriceAvailable(QString const &tokenName,
                           double const,
                           korrelator::exchange_name_e const,
                           korrelator::trade_type_e const);
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
  trade_type_e const m_tradeType;
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
  bool m_requestedToStop = false;
};

double binanceGetCoinPrice(char const *str, size_t const size);

}
