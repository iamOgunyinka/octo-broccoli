#include "https_request.hpp"

#include <QDebug>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <rapidjson/document.h>

#include <sstream>
#include <thread>

#include "constants.hpp"
#include "crypto.hpp"

namespace korrelator {

double format_quantity(double value, int const decimal_places) {
  double const multiplier = std::pow(10.0, decimal_places);
  return std::trunc(value * multiplier) / multiplier;
  // return QString::number(returnValue, 'f', decimal_places);
}

void kc_https_plug::sendHttpsData() {
  beast::get_lowest_layer(m_tcpStream)
      .expires_after(std::chrono::milliseconds(15'000));
  http::async_write(m_tcpStream, m_httpRequest,
                    [this](auto const &a, auto const &b) { onDataSent(a, b); });
}

void kc_https_plug::onDataSent(beast::error_code ec, std::size_t const) {
  if (ec) {
    qDebug() << "Problem writing\n" << ec.message().c_str();
    return;
  }
  receiveData();
}

void kc_https_plug::receiveData() {
  beast::get_lowest_layer(m_tcpStream)
      .expires_after(std::chrono::milliseconds(15000));
  m_httpResponse.clear();
  http::async_read(
      m_tcpStream, m_readBuffer, m_httpResponse,
      [this](auto const &a, auto const &b) { onDataReceived(a, b); });
}

void kc_https_plug::doConnect() {
  createRequestData();
  using resolver = tcp::resolver;
  auto const host = m_isSpot ? constants::kucoin_https_spot_host
                             : constants::kc_futures_api_url;
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

void kc_https_plug::startConnect() { doConnect(); }

void kc_https_plug::onHostResolved(tcp::resolver::results_type const &result) {
  beast::get_lowest_layer(m_tcpStream).expires_after(std::chrono::seconds(30));
  beast::get_lowest_layer(m_tcpStream)
      .async_connect(result, [this](auto const &errorCode, auto const &) {
        if (errorCode) {
          qDebug() << errorCode.message().c_str();
          return;
        }
        performSSLHandshake();
      });
}

void kc_https_plug::createRequestData() {
  using http::field;

  char const *const path = "/api/v1/orders";
  std::string const unixEpochTime = std::to_string(std::time(nullptr) * 1'000);
  std::string const orderID = get_random_string(38);

  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);
  writer.StartObject(); // {

  writer.Key("clientOid");        // key
  writer.String(orderID.c_str()); // value

  writer.Key("symbol");
  writer.String(m_tradeConfig->symbol.toUpper().toStdString().c_str());

  auto const marketType = m_tradeConfig->marketType.isEmpty()
                              ? "market"
                              : m_tradeConfig->marketType;
  bool const isMarketType = marketType == "market";
  writer.Key("type");
  writer.String(marketType.toLower().toStdString().c_str());

  writer.Key("side");
  writer.String(m_tradeConfig->side == trade_action_e::buy ? "buy" : "sell");

  double size = m_tradeConfig->size;
  double baseAmount = m_tradeConfig->baseAmount;

  if (isMarketType) {
    if (m_isSpot) {
      if (size == 0.0 && baseAmount != 0.0) {
        writer.Key("funds");
        writer.String(std::to_string(baseAmount).c_str());
      } else if (size != 0.0) {
        writer.Key("size");
        writer.String(std::to_string(m_tradeConfig->size).c_str());
      }
    } else {
      auto size = m_tradeConfig->size;
      if (size == 0.0)
        size = (baseAmount / m_price);
      writer.Key("size");
      writer.Double(size);
    }
  } else {
    if (size == 0.0 && baseAmount != 0.0)
      size = baseAmount / m_price;
    writer.Key("price");

    m_price = format_quantity(m_price, m_tradeConfig->pricePrecision);

    auto const priceStr = QString::number(
          m_price, 'f', m_tradeConfig->pricePrecision);
    writer.String(priceStr.toStdString().c_str());
    writer.Key("size");
    writer.String(std::to_string(size).c_str());
  }

  if (m_isSpot) {
    writer.Key("tradeType");
    writer.String("TRADE");
  } else {
    writer.Key("leverage");
    writer.String(std::to_string(m_tradeConfig->leverage).c_str());
  }

  writer.EndObject(); // }
  auto const payload = s.GetString();

  auto const stringToSign = unixEpochTime + "POST" + path + payload;
  auto const signature =
      base64_encode(hmac256_encode(stringToSign, m_apiSecret));
  auto const host = m_isSpot ? constants::kucoin_https_spot_host
                             : constants::kc_futures_api_url;
  m_httpRequest.clear();
  m_httpRequest.method(http::verb::post);
  m_httpRequest.version(11);
  m_httpRequest.target(path);
  m_httpRequest.set(field::host, host);
  m_httpRequest.set(field::content_type, "application/json");
  m_httpRequest.set(field::user_agent, "postman");
  m_httpRequest.set(field::accept, "*/*");
  m_httpRequest.set(field::connection, "close");
  m_httpRequest.set("KC-API-SIGN", signature);
  m_httpRequest.set("KC-API-TIMESTAMP", unixEpochTime);
  m_httpRequest.set("KC-API-KEY", m_apiKey);
  m_httpRequest.set("KC-API-PASSPHRASE", m_apiPassphrase);
  m_httpRequest.set("KC-API-KEY-VERSION", "1");
  m_httpRequest.body() = payload;
  m_httpRequest.prepare_payload();

#ifdef _DEBUG
  std::ostringstream ss;
  ss << m_httpRequest;
  qDebug() << ss.str().c_str();
#endif
}

void kc_https_plug::performSSLHandshake() {
  beast::get_lowest_layer(m_tcpStream).expires_after(std::chrono::seconds(15));
  auto const host = m_isSpot ? constants::kucoin_https_spot_host
                             : constants::kc_futures_api_url;
  if (!SSL_set_tlsext_host_name(m_tcpStream.native_handle(), host)) {
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

kc_https_plug::kc_https_plug(net::io_context &ioContext,
                             ssl::context &sslContext,
                             trade_type_e const tradeType,
                             api_data_t const &apiData,
                             trade_config_data_t *tradeConfig)
    : m_isSpot(tradeType == trade_type_e::spot),
      m_tradeAction(tradeConfig->side), m_ioContext(ioContext),
      m_sslContext(sslContext), m_tradeConfig(tradeConfig),
      m_apiKey(m_isSpot ? apiData.spotApiKey.toStdString()
                        : apiData.futuresApiKey.toStdString()),
      m_apiSecret(m_isSpot ? apiData.spotApiSecret.toStdString()
                           : apiData.futuresApiSecret.toStdString()),
      m_apiPassphrase(m_isSpot ? apiData.spotApiPassphrase.toStdString()
                               : apiData.futuresApiPassphrase.toStdString()),
      m_tcpStream(net::make_strand(ioContext), sslContext),
      m_resolver(ioContext) {}

kc_https_plug::~kc_https_plug() {}

void kc_https_plug::onDataReceived(beast::error_code ec,
                                   std::size_t const bytesReceived) {
  if (ec)
    return;

  qDebug() << ec.message().c_str() << bytesReceived;
  qDebug() << m_httpResponse.body().c_str();
}

// ================================= BINANCE =========================

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

void binance_https_plug::createRequestData() {
  using http::field;

  QString query("symbol=" + m_tradeConfig->symbol.toUpper());
  query += "&side=";
  query += (m_tradeConfig->side == trade_action_e::buy ? "BUY" : "SELL");

  auto const marketType = m_tradeConfig->marketType.isEmpty()
                              ? "market"
                              : m_tradeConfig->marketType;
  query += "&type=" + marketType.toUpper();

  double& size = m_tradeConfig->size;
  double& baseAmount = m_tradeConfig->baseAmount;

  if (marketType == "market") {
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

#ifndef _DEBUG
  std::stringstream ss;
  ss << m_httpRequest;
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

#ifdef _MSC_VER
#undef GetObject
#endif

void binance_https_plug::onDataReceived(beast::error_code ec,
                                        std::size_t const bytesReceived) {
  if (ec) {
    qDebug() << ec.message().c_str() << bytesReceived;
    return;
  }

  auto& body = m_httpResponse->body();
  char const * const str = body.c_str();
  size_t const length = body.length();

  if (m_isFirstRequest)
    m_isFirstRequest = false;
  processResponse(str, length);
}

bool binance_https_plug::processResponse(
    char const *str, size_t const length) {
  rapidjson::Document doc;
  doc.Parse(str, length);
  bool noError = true;

  try {
    if (!doc.IsObject())
      goto onErrorEnd;
    auto const jsonRoot = doc.GetObject();
    auto const statusIter = jsonRoot.FindMember("status");
    auto const assignedOrderIDIter = jsonRoot.FindMember("clientOrderId");
    auto const executedQtyIter = jsonRoot.FindMember("executedQty");
    if (statusIter == jsonRoot.MemberEnd() ||
        assignedOrderIDIter == jsonRoot.MemberEnd() ||
        executedQtyIter == jsonRoot.MemberEnd())
      goto onErrorEnd;
    QString const status = statusIter->value.GetString();
    m_userOrderID = assignedOrderIDIter->value.GetString();
    m_totalExecuted += std::stod(executedQtyIter->value.GetString());

    auto const hasContemporary = m_tradeAction == trade_action_e::buy &&
        m_tradeConfig->contemporary != nullptr;
    if (status.compare("new", Qt::CaseInsensitive) == 0){
      if (hasContemporary)
        m_tradeConfig->contemporary->size = 0.0;
      startMonitoringNewOrder();
      return true;
    } else if (status.compare("filled", Qt::CaseInsensitive) == 0) {
      if (hasContemporary)
        m_tradeConfig->contemporary->size = m_totalExecuted;
      goto noErrorEnd;
    }
    startMonitoringNewOrder();
    return true;
  } catch(std::exception const & e){
    qDebug() << e.what();
    goto onErrorEnd;
  } catch (...) {
    goto onErrorEnd;
  }

onErrorEnd:
  noError = false;
  qDebug() << "There must have been an error"
           << m_httpResponse->body().c_str();
  goto noErrorEnd;

noErrorEnd:
  m_tcpStream.async_shutdown([this](boost::system::error_code const) {
    qDebug() << "Stream closed";
    m_totalExecuted = 0.0;
  });
  return noError;
}

void binance_https_plug::startMonitoringNewOrder() {
  createMonitoringRequest();
  sendHttpsData();
}

void binance_https_plug::createMonitoringRequest() {
  using http::field;

  auto const host = m_isSpot ? constants::binance_http_spot_host
                             : constants::binance_http_futures_host;
  QString urlQuery("symbol=" + m_tradeConfig->symbol.toUpper());
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

#ifndef _DEBUG
  std::stringstream ss;
  ss << m_httpRequest;
  qDebug() << ss.str().c_str();
#endif
}

} // namespace korrelator
