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

namespace details {

class binance_futures_plug {
  enum class request_type_e {
    initial, leverage, market, limit
  };

  request_type_e m_currentRequest = request_type_e::initial;
  double m_price = 0.0;
  double m_averagePriceExecuted = 0.0;
  double m_finalSizePurchased = 0.0;
  int64_t m_binanceOrderID = -1;

  net::io_context &m_ioContext;
  ssl::context &m_sslContext;
  trade_config_data_t *m_tradeConfig = nullptr;
  QString const m_apiKey;
  QString const m_apiSecret;
  QString m_userOrderID;
  QString m_errorString;
  tcp::resolver m_resolver;
  std::optional<beast::flat_buffer> m_readBuffer;
  std::optional<beast::ssl_stream<beast::tcp_stream>> m_tcpStream;
  std::optional<http::response<http::string_body>> m_httpResponse;
  std::optional<http::request<http::empty_body>> m_httpRequest;

private:
  bool createLeverageRequest();
  [[nodiscard]] bool createRequestData();
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
  void processLeverageResponse(char const *, size_t const);
  void processOrderResponse(char const *, size_t const);
  void disconnectConnection();
  void createErrorResponse();

public:
  binance_futures_plug(net::io_context &, ssl::context &,api_data_t const &,
                       trade_config_data_t*);
  ~binance_futures_plug();
  void setLeverage();
  void setPrice(double const price) {
    m_price = price;
  }

  double averagePrice() const;
  QString errorString() const { return m_errorString; }
  void startConnect();
};

}

double format_quantity(double const value, int decimal_places);

} // namespace korrelator
