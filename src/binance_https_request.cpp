#include "binance_https_request.hpp"

#include <QDebug>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

#include <rapidjson/document.h>
#include <thread>

#ifdef TESTNET
#include <sstream>
#endif

#include "constants.hpp"
#include "crypto.hpp"

namespace korrelator {

double format_quantity(double const value, int decimal_places) {
  if (decimal_places == -1)
    decimal_places = 8;
  double const multiplier = std::pow(10.0, decimal_places);
  return std::trunc(value * multiplier) / multiplier;
}

void binance_https_plug::sendHttpsData() {
  beast::get_lowest_layer(m_tcpStream)
      .expires_after(std::chrono::milliseconds(15'000));
  http::async_write(m_tcpStream, *m_httpRequest,
                    [this](auto const &a, auto const &b) { onDataSent(a, b); });
}

void binance_https_plug::onDataSent(beast::error_code ec, std::size_t const) {
  if (ec) {
    qDebug() << "Problem writing\n" << ec.message().c_str();
    return;
  }
  receiveData();
}

void binance_https_plug::receiveData() {
  beast::get_lowest_layer(m_tcpStream)
      .expires_after(std::chrono::milliseconds(15000));
  m_httpResponse.emplace();
  http::async_read(
      m_tcpStream, m_readBuffer, *m_httpResponse,
      [this](auto const &a, auto const &b) { onDataReceived(a, b); });
}

void binance_https_plug::doConnect() {
  using resolver = tcp::resolver;

  createRequestData();
  auto const host = m_isSpot ? constants::binance_http_spot_host
                             : constants::binance_http_futures_host;
  m_resolver.async_resolve(
      host, "https",
      [this](auto const &errorCode, resolver::results_type const &results) {
        if (errorCode) {
          qDebug() << errorCode.message().c_str();
          return;
        }
        onHostResolved(results);
      });
}

void binance_https_plug::startConnect() { doConnect(); }

void binance_https_plug::onHostResolved(
    tcp::resolver::results_type const &result) {
  beast::get_lowest_layer(m_tcpStream).expires_after(std::chrono::seconds(30));
  beast::get_lowest_layer(m_tcpStream)
      .async_connect(result, [this](auto const &errorCode, auto const &result) {
        if (errorCode) {
          qDebug() << errorCode.message().c_str();
          return;
        }
        performSSLHandshake(result);
      });
}

void binance_https_plug::createLeverageRequest() {
  using http::field;
  m_currentRequest = request_type_e::leverage;

  auto& httpRequest = m_httpRequest.emplace();
  auto const host = constants::binance_http_futures_host;
  std::string const path = "/fapi/v1/leverage";
  QString query =
      "symbol=" + m_tradeConfig->symbol.toUpper() + "&leverage=" +
      QString::number((int)m_tradeConfig->leverage) +
      "&recvWindow=5000&timestamp=" + QString::number(getGMTTimeMs());
  auto const signature = hmac256_encode(
        query.toStdString(), m_apiSecret.toStdString(), true);
  query += QString("&signature=") +
      reinterpret_cast<char const *>(signature.data());

  httpRequest.method(http::verb::post);
  httpRequest.version(11);
  httpRequest.target(path + "?" + query.toStdString());
  httpRequest.set(field::host, host);
  httpRequest.set(field::content_type, "application/json");
  httpRequest.set(field::user_agent, "postman");
  httpRequest.set(field::accept, "*/*");
  httpRequest.set(field::connection, "keep-alive");
  httpRequest.set("X-MBX-APIKEY", m_apiKey.toStdString());
  httpRequest.prepare_payload();
}

void binance_https_plug::createRequestData() {
  using http::field;

  if (!m_isSpot && m_currentRequest == request_type_e::initial)
    return createLeverageRequest();

  QString query("symbol=" + m_tradeConfig->symbol.toUpper());
  query += "&side=";
  query += (m_tradeConfig->side == trade_action_e::buy ? "BUY" : "SELL");

  auto const marketType = korrelator::marketTypeToString(m_tradeConfig->marketType);
  query += "&type=" + marketType.toUpper();

  double& size = m_tradeConfig->size;
  double& baseAmount = m_tradeConfig->baseAmount;

  if (m_tradeConfig->marketType == korrelator::market_type_e::market) {
    if (m_isSpot) {
      if ((size == 0.0 && baseAmount != 0.0)) {
        baseAmount = format_quantity(baseAmount, m_tradeConfig->quotePrecision);
        query += "&quoteOrderQty=";
        query += QString::number(baseAmount, 'f', m_tradeConfig->quotePrecision);
      } else if (baseAmount == 0.0 && size != 0.0) {
        size = format_quantity(size, m_tradeConfig->quantityPrecision);
        query += "&quantity=";
        query += QString::number(size, 'f', m_tradeConfig->quantityPrecision);
      }
    } else { // futures
      if (size == 0.0)
        size = ((m_tradeConfig->baseAmount / m_price) * m_tradeConfig->leverage);
      size = format_quantity(size, m_tradeConfig->quantityPrecision);
      query += "&quantity=";
      query += QString::number(size, 'f', m_tradeConfig->quantityPrecision);
    }
  } else {
    query += ("&timeInForce=GTC"); // Good Till Canceled
    if (size == 0.0 && m_tradeConfig->baseAmount != 0.0)
      size = m_tradeConfig->baseAmount / m_price;

    if (!m_isSpot) // futures
      size *= m_tradeConfig->leverage;

    size = format_quantity(size, m_tradeConfig->quantityPrecision);
    query += ("&quantity=");
    query += QString::number(size, 'f', m_tradeConfig->quantityPrecision);

    m_price = format_quantity(m_price, m_tradeConfig->pricePrecision);
    query += ("&price=");
    query += QString::number(m_price, 'f', m_tradeConfig->pricePrecision);
  }

  query += QString("&recvWindow=5000&timestamp=") +
      QString::number(getGMTTimeMs());

  auto const signature = hmac256_encode(
        query.toStdString(), m_apiSecret.toStdString(), true);
  query += QString("&signature=") +
      reinterpret_cast<char const *>(signature.data());
  auto const host = m_isSpot ? constants::binance_http_spot_host
                             : constants::binance_http_futures_host;

  std::string const path = m_isSpot ? "/api/v3/order" : "/fapi/v1/order";
  auto& httpRequest = m_httpRequest.emplace();

  httpRequest.method(http::verb::post);
  httpRequest.version(11);
  httpRequest.target(path + "?" + query.toStdString());
  httpRequest.set(field::host, host);
  httpRequest.set(field::content_type, "application/json");
  httpRequest.set(field::user_agent, "postman");
  httpRequest.set(field::accept, "*/*");
  httpRequest.set(field::connection, "keep-alive");
  httpRequest.set("X-MBX-APIKEY", m_apiKey.toStdString());

#ifdef TESTNET
  std::stringstream ss;
  ss << httpRequest;
  qDebug() << ss.str().c_str();
#endif
}

void binance_https_plug::performSSLHandshake(
    tcp::resolver::results_type::endpoint_type const &ip) {
  beast::get_lowest_layer(m_tcpStream).expires_after(std::chrono::seconds(15));
  auto const host = (m_isSpot ? constants::binance_http_spot_host
                              : constants::binance_http_futures_host) +
                    std::string(":") + std::to_string(ip.port());
  if (!SSL_set_tlsext_host_name(m_tcpStream.native_handle(), host.c_str())) {
    auto const ec = beast::error_code(static_cast<int>(::ERR_get_error()),
                                      net::error::get_ssl_category());
    qDebug() << ec.message().c_str();
    return;
  }

  m_tcpStream.async_handshake(ssl::stream_base::client,
                              [this](beast::error_code const ec) {
                                if (ec) {
                                  qDebug() << ec.message().c_str();
                                  return;
                                }
                                return sendHttpsData();
                              });
}

binance_https_plug::binance_https_plug(net::io_context &ioContext,
                                       ssl::context &sslContext,
                                       trade_type_e const tradeType,
                                       api_data_t const &apiData,
                                       trade_config_data_t *tradeConfig)
    : m_isSpot(tradeType == trade_type_e::spot),
      m_tradeAction(tradeConfig->side), m_ioContext(ioContext),
      m_sslContext(sslContext), m_tradeConfig(tradeConfig),
      m_apiKey((tradeType == trade_type_e::spot ? apiData.spotApiKey
                                                : apiData.futuresApiKey)),
      m_apiSecret((tradeType == trade_type_e::spot ? apiData.spotApiSecret
                                                   : apiData.futuresApiSecret)),
      m_tcpStream(net::make_strand(ioContext), sslContext),
      m_resolver(ioContext) {}

binance_https_plug::~binance_https_plug() {}

void binance_https_plug::onDataReceived(beast::error_code ec,
                                        std::size_t const bytesReceived) {
  if (ec) {
    qDebug() << ec.message().c_str() << bytesReceived;
    return;
  }

  auto& body = m_httpResponse->body();
  char const * const str = body.c_str();
  size_t const length = body.length();
  if (m_currentRequest == request_type_e::leverage)
    return processLeverageResponse(str, length);
  processOrderResponse(str, length);
}

#ifdef _MSC_VER
#undef GetObject
#endif

void binance_https_plug::processLeverageResponse(
    char const *str, size_t const length) {
  rapidjson::Document doc;
  doc.Parse(str, length);
  if (!doc.IsObject())
    return createErrorResponse();

  try {
    auto const jsonRoot = doc.GetObject();
    auto const leverageIter = jsonRoot.FindMember("leverage");
    if (leverageIter == jsonRoot.MemberEnd())
      return createErrorResponse();
    auto const leverage = leverageIter->value.GetInt();
    if (leverage != (int)m_tradeConfig->leverage)
      return createErrorResponse();
    m_currentRequest = request_type_e::market;

    qDebug() << str;
    createRequestData();
    return sendHttpsData();
  } catch(std::exception const & e) {
    qDebug() << e.what();
    return createErrorResponse();
  } catch (...) {
    return createErrorResponse();
  }
}

void binance_https_plug::processOrderResponse(
    char const *str, size_t const length) {
  rapidjson::Document doc;
  doc.Parse(str, length);
  if (!doc.IsObject())
    return createErrorResponse();

  try {
    auto const jsonRoot = doc.GetObject();
    auto const statusIter = jsonRoot.FindMember("status");
    auto const assignedOrderIDIter = jsonRoot.FindMember("clientOrderId");
    if (statusIter == jsonRoot.MemberEnd() ||
        assignedOrderIDIter == jsonRoot.MemberEnd())
      return createErrorResponse();
    QString const status = statusIter->value.GetString();
    m_userOrderID = assignedOrderIDIter->value.GetString();

    if (status.compare("new", Qt::CaseInsensitive) == 0) {
      auto const binanceOrderIDIter = jsonRoot.FindMember("orderId");
      if (binanceOrderIDIter != jsonRoot.MemberEnd() && (
          binanceOrderIDIter->value.IsInt() || binanceOrderIDIter->value.IsInt64())) {
        if (binanceOrderIDIter->value.IsInt64())
          m_binanceOrderID = binanceOrderIDIter->value.GetInt64();
        else if (binanceOrderIDIter->value.IsInt())
          m_binanceOrderID = (int64_t) binanceOrderIDIter->value.GetInt();
      }

      return startMonitoringNewOrder();
    }
    else if (status.compare("filled", Qt::CaseInsensitive) == 0) {
      auto const avgPriceIter = jsonRoot.FindMember("avgPrice");
      auto const executedQtyIter = jsonRoot.FindMember("executedQty");
      if (avgPriceIter != jsonRoot.MemberEnd() &&
          avgPriceIter->value.IsString()) {
        m_averagePriceExecuted += std::stod(avgPriceIter->value.GetString());
      }

      if (executedQtyIter != jsonRoot.MemberEnd() &&
          executedQtyIter->value.IsString()) {
        m_finalSizePurchased = std::stod(executedQtyIter->value.GetString());
      }

      return disconnectConnection();
    }
    return startMonitoringNewOrder();
  } catch(std::exception const & e){
    qDebug() << e.what();
    return createErrorResponse();
  } catch (...) {
    return createErrorResponse();
  }
}

void binance_https_plug::createErrorResponse() {
  m_errorString = m_httpResponse->body().c_str();
  qDebug() << "There must have been an error" << m_errorString;
  return disconnectConnection();
}

void binance_https_plug::disconnectConnection() {
  m_tcpStream.async_shutdown([](boost::system::error_code const) {
    qDebug() << "Stream closed";
  });
}

void binance_https_plug::startMonitoringNewOrder() {
  // allow for enough time before querying the exchange
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  createMonitoringRequest();
  sendHttpsData();
}

void binance_https_plug::createMonitoringRequest() {
  using http::field;

  auto const host = m_isSpot ? constants::binance_http_spot_host
                             : constants::binance_http_futures_host;
  QString urlQuery("symbol=" + m_tradeConfig->symbol.toUpper());
  if (m_binanceOrderID != -1)
    urlQuery += "&orderId=" + QString::number(m_binanceOrderID);
  else
    urlQuery += "&origClientOrderId=" + m_userOrderID;

  urlQuery += "&timestamp=" + QString::number(getGMTTimeMs());
  auto const signature = hmac256_encode(
        urlQuery.toStdString(), m_apiSecret.toStdString(), true);
  urlQuery += QString("&signature=") +
      reinterpret_cast<char const *>(signature.data());

  std::string const path = m_isSpot ? "/api/v3/order" : "/fapi/v1/order";

  auto &httpRequest = m_httpRequest.emplace();
  httpRequest.method(http::verb::get);
  httpRequest.version(11);
  httpRequest.target(path + "?" + urlQuery.toStdString());
  httpRequest.set(field::host, host);
  httpRequest.set(field::content_type, "application/json");
  httpRequest.set(field::user_agent, "postman");
  httpRequest.set(field::accept, "*/*");
  httpRequest.set(field::connection, "keep-alive");
  httpRequest.set("X-MBX-APIKEY", m_apiKey.toStdString());
  httpRequest.prepare_payload();

#ifdef TESTNET
  std::stringstream ss;
  ss << httpRequest;
  qDebug() << ss.str().c_str();
#endif
}

} // namespace korrelator
