#pragma once

#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include "utils.hpp"
#include <string>
#include <optional>

namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;
namespace ssl = boost::asio::ssl;

namespace korrelator {
using tcp = boost::asio::ip::tcp;

class kc_https_plug {
  bool const m_isSpot;
  trade_action_e const m_tradeAction;
  double m_price = 0.0;
  net::io_context &m_ioContext;
  ssl::context &m_sslContext;
  trade_config_data_t *m_tradeConfig = nullptr;
  std::string const m_apiKey;
  std::string const m_apiSecret;
  std::string const m_apiPassphrase;
  beast::ssl_stream<beast::tcp_stream> m_tcpStream;
  tcp::resolver m_resolver;
  beast::flat_buffer m_readBuffer;
  http::response<http::string_body> m_httpResponse;
  http::request<http::string_body> m_httpRequest;

private:
  void createRequestData();
  void performSSLHandshake();
  void onHandshook(beast::error_code ec);
  void onHostResolved(tcp::resolver::results_type const &);
  void sendHttpsData();
  void onDataSent(beast::error_code, std::size_t const);
  void receiveData();
  void onDataReceived(beast::error_code, std::size_t const);
  void doConnect();

public:
  kc_https_plug(net::io_context &, ssl::context &, trade_type_e const tradeType,
                api_data_t const &apiData, trade_config_data_t *tradeConfig);

  ~kc_https_plug();
  void setPrice(double const price) { m_price = price; }
  void startConnect();
};

class binance_https_plug {
  bool const m_isSpot;
  bool m_isFirstRequest = true;
  trade_action_e const m_tradeAction;
  double m_price = 0.0;
  double m_totalExecuted = 0.0;
  net::io_context &m_ioContext;
  ssl::context &m_sslContext;
  trade_config_data_t *m_tradeConfig = nullptr;
  QString const m_apiKey;
  QString const m_apiSecret;
  QString m_userOrderID;
  beast::ssl_stream<beast::tcp_stream> m_tcpStream;
  tcp::resolver m_resolver;
  beast::flat_buffer m_readBuffer;
  std::optional<http::response<http::string_body>> m_httpResponse;
  std::optional<http::request<http::empty_body>> m_httpRequest;

private:
  void createRequestData();
  void performSSLHandshake(tcp::resolver::results_type::endpoint_type const &);
  void onHandshook(beast::error_code ec);
  void onHostResolved(tcp::resolver::results_type const &);
  void sendHttpsData();
  void onDataSent(beast::error_code, std::size_t const);
  void receiveData();
  void onDataReceived(beast::error_code, std::size_t const);
  void doConnect();
  void startMonitoringNewOrder();
  void createMonitoringRequest();
  bool processResponse(char const *, size_t const);

public:
  binance_https_plug(net::io_context &, ssl::context &,
                     trade_type_e const tradeType, api_data_t const &apiData,
                     trade_config_data_t *tradeConfig);
  ~binance_https_plug();
  void setPrice(double const price) {
    m_price = price;
  }

  void startConnect();
};
} // namespace korrelator
