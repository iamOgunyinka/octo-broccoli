#pragma once

#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include "utils.hpp"
#include <optional>

#include <rapidjson/document.h>

namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;
namespace ssl = boost::asio::ssl;

namespace korrelator {
namespace details {

using JsonObject = rapidjson::GenericValue<rapidjson::UTF8<>>::Object;

using tcp = boost::asio::ip::tcp;

class kucoin_spots_plug {
  enum class process_e {
    market_initiated, // first step ever
    monitoring_failed_market,
    monitoring_successful_request,
    market_404,
    limit_initiated,
  };

  trade_action_e const m_tradeAction;
  process_e m_process;
  int m_numberOfRetries = 0;
  double m_price = 0.0;

  double m_averagePrice = 0.0;

  net::io_context &m_ioContext;
  ssl::context &m_sslContext;
  trade_config_data_t *m_tradeConfig = nullptr;
  std::string const m_apiKey;
  std::string const m_apiSecret;
  std::string const m_apiPassphrase;
  QString m_kucoinOrderID;
  QString m_errorString;
  beast::ssl_stream<beast::tcp_stream> m_tcpStream;
  tcp::resolver m_resolver;
  std::optional<beast::flat_buffer> m_readBuffer;
  std::optional<http::response<http::string_body>> m_httpResponse;
  std::optional<http::request<http::string_body>> m_httpRequest;

private:
  [[nodiscard]] bool createRequestData();
  void processRemainingDataPage();
  void performSSLHandshake();
  void onHandshook(beast::error_code ec);
  void onHostResolved(tcp::resolver::results_type const &);
  void sendHttpsData();
  void onDataSent(beast::error_code, std::size_t const);
  void receiveData();
  void onDataReceived(beast::error_code, std::size_t const);
  void doConnect();
  void startMonitoringLastOrder();
  void createMonitoringRequest();
  void initiateLimitOrder();
  void severConnection();
  void reportError(QString const &errString = {});
  void parseSuccessfulResponse(JsonObject const &);

public:
  kucoin_spots_plug(net::io_context &, ssl::context &,
                    api_data_t const &apiData, trade_config_data_t*);

  ~kucoin_spots_plug();
  void setPrice(double const price) { m_price = price; }
  void startConnect();

  double quantityPurchased() const { return m_averagePrice; }
  double sizePurchased() const { return 1.0; }
  QString errorString() const { return m_errorString; }
};

}
double format_quantity(double const value, int decimal_places);
bool normalizeQuoteAmount(trade_config_data_t*);
}
