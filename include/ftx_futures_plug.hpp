#pragma once

#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include "utils.hpp"
#include <optional>
#include <string>

namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;
namespace ssl = boost::asio::ssl;

namespace korrelator {
using tcp = boost::asio::ip::tcp;

namespace details {

class ftx_futures_plug {
  enum class request_status_e {
    none, setting_leverage, new_order, check_status, check_fills
  };

public:
  ftx_futures_plug(net::io_context& ioContext, ssl::context&,
                   api_data_t const &, trade_config_data_t*);
  ~ftx_futures_plug();
  void setAccountLeverage();
  void startConnect() { doConnect(); }
  double getAveragePrice() const { return m_averagePrice; }
  QString errorString() const { return m_errorString; }
  void setPrice(double const price) {
    m_price = price;
  }

private:
  [[nodiscard]] bool createRequestData();
  [[nodiscard]] bool createLeverageRequestData();
  [[nodiscard]] bool createNewOrderRequestData();

  void doConnect();
  void onHostResolved(tcp::resolver::results_type const &);
  void performSSLHandshake();
  void sendDataToFtx();
  void receiveData();
  void processOrderResponse(char const *, size_t const);
  void createErrorResponse();
  void disconnectConnection();
  void monitorOrderStatus();
  void createMonitoringRequest();

private:
  net::io_context &m_ioContext;
  ssl::context &m_sslContext;
  trade_config_data_t *m_tradeConfig = nullptr;
  tcp::resolver m_resolver;
  std::optional<beast::flat_buffer> m_readBuffer;
  std::optional<beast::ssl_stream<beast::tcp_stream>> m_tcpStream;
  std::optional<http::response<http::string_body>> m_httpResponse;
  std::optional<http::request<http::string_body>> m_httpRequest;

  QString const m_apiKey;
  QString const m_apiSecret;
  QString m_errorString;
  QString m_userOrderID;
  uint64_t m_ftxOrderID = 0;
  double m_price = 0;
  double m_averagePrice = 0.0;
  request_status_e m_requestStatus = request_status_e::none;
};

} // namespace details

bool normalizeQuoteAmount(trade_config_data_t* tradeConfig);
double format_quantity(double const value, int decimal_places);

} // namespace korrelator
