#pragma once

#include <QString>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <map>
#include <optional>

#include "tokens.hpp"
#include "utils.hpp"
#include "websocket_base.hpp"

namespace korrelator {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace ip = net::ip;

using price_callback =
    std::function<void(QString const &, double const, trade_type_e const)>;

namespace detail {
class custom_socket : public websocket_base {
  using resolver_result_type = net::ip::tcp::resolver::results_type;

  static char const *const futures_url;
  static char const *const spot_url;
  static char const *const spot_port;
  static char const *const futures_port;

  using address_list_t = std::vector<internal_address_t>;

public:
  using resolver = ip::tcp::resolver;
  using results_type = resolver::results_type;

  custom_socket(net::io_context &ioContext, net::ssl::context &sslContext,
                token_proxy_iter &proxyIter)
      : m_tradeType(proxyIter.tradeType()),
        m_host(m_tradeType == trade_type_e::spot ? spot_url : futures_url),
        m_port(m_tradeType == trade_type_e::spot ? spot_port : futures_port),
        m_ioContext(ioContext), m_sslContext(sslContext),
        m_tokenProxyIter(proxyIter) {}

  ~custom_socket();

  void startFetching() override;
  void addSubscription(std::string const &tokenName) override {
    m_tokenName = internal_address_t{tokenName, false};
  }

  void requestStop() override;

private:
  custom_socket *shared_from_this() { return this; }
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
  token_proxy_iter &m_tokenProxyIter;
  internal_address_t m_tokenName;
  std::optional<resolver> m_resolver = std::nullopt;
  std::optional<beast::websocket::stream<beast::ssl_stream<beast::tcp_stream>>>
      m_sslWebStream;
  std::optional<beast::flat_buffer> m_readBuffer;
  std::string m_writeBuffer;
  bool m_requestedToStop = false;
};

double binanceGetCoinPrice(char const *str, size_t const size);

} // namespace detail

class cwebsocket {
  struct exchange_trade_pair {
    trade_type_e tradeType;
    QString tokenName;
  };

public:
  void addSubscription(token_proxy_iter &iter);
  void startWatch();

  cwebsocket();
  ~cwebsocket();

private:
  net::ssl::context &m_sslContext;
  net::io_context *m_ioContext = nullptr;
  std::vector<std::shared_ptr<websocket_base>> m_sockets;
  std::map<exchange_name_e, std::vector<exchange_trade_pair>> m_checker;
};

using cwebsocket_ptr = std::unique_ptr<cwebsocket>;
} // namespace korrelator
