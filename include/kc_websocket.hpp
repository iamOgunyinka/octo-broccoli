#pragma once

#include <QString>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <optional>
#include "uri.hpp"

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
namespace ssl = net::ssl;
namespace beast = boost::beast;
namespace ws = beast::websocket;
namespace ip = net::ip;

struct instance_server_data_t {
  std::string endpoint; // wss://foo.com/path
  int pingIntervalMs = 0;
  int pingTimeoutMs = 0;
  int encryptProtocol = 0; // bool encrypt or not
};

enum class trade_type_e;
enum class exchange_name_e;

// kucoin websocket
class kucoin_ws {

  using resolver = ip::tcp::resolver;
  using results_type = resolver::results_type;

public:
  kucoin_ws(net::io_context &ioContext, ssl::context &sslContext,
            double& result, trade_type_e const);
  ~kucoin_ws();
  void addSubscription(QString const &);
  void startFetching() { restApiInitiateConnection(); }
  void requestStop() { m_requestedToStop = true; }

private:
  void restApiSendRequest();
  void restApiReceiveResponse();
  void restApiInitiateConnection();
  void restApiConnectToResolvedNames(results_type const &);
  void restApiPerformSSLHandshake();
  void restApiInterpretHttpResponse();

  void negotiateWebsocketConnection();
  void initiateWebsocketConnection();
  void websockPerformSSLHandshake(results_type::endpoint_type const &);
  void websockConnectToResolvedNames(results_type const &);
  void performWebsocketHandshake();
  void waitForMessages();
  void interpretGenericMessages();
  void makeSubscription();
  void resetBuffer();

  net::io_context &m_ioContext;
  ssl::context &m_sslContext;
  double &m_priceResult;
  std::optional<resolver> m_resolver;
  std::optional<ws::stream<beast::ssl_stream<beast::tcp_stream>>>
      m_sslWebStream;
  std::optional<beast::http::response<beast::http::string_body>> m_response;
  beast::flat_buffer m_readWriteBuffer;
  std::vector<instance_server_data_t> m_instanceServers;
  std::string m_websocketToken;
  std::string m_subscriptionString;
  QString m_tokenList;
  korrelator::uri m_uri;
  trade_type_e const m_tradeType;
  bool const m_isSpotTrade;
  bool m_tokensSubscribedFor = false;
  bool m_requestedToStop = false;
};

char get_random_char();
std::string get_random_string(std::size_t);
std::size_t get_random_integer();

} // namespace korrelator
