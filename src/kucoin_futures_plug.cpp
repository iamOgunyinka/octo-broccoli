#include "kucoin_futures_plug.hpp"

#include <QDebug>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

#include "constants.hpp"
#include "crypto.hpp"
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <thread>

extern double maxOrderRetries;

namespace korrelator {

namespace details {
void kucoin_futures_plug::sendHttpsData() {
  beast::get_lowest_layer(m_tcpStream)
      .expires_after(std::chrono::milliseconds(15'000));
  http::async_write(m_tcpStream, *m_httpRequest,
                    [this](auto const &a, auto const &b) { onDataSent(a, b); });
}

void kucoin_futures_plug::onDataSent(beast::error_code ec, std::size_t const) {
  if (ec) {
    qDebug() << "Problem writing\n" << ec.message().c_str();
    return;
  }
  receiveData();
}

void kucoin_futures_plug::receiveData() {
  m_httpResponse.emplace();
  beast::get_lowest_layer(m_tcpStream)
      .expires_after(std::chrono::milliseconds(15000));
  http::async_read(
      m_tcpStream, m_readBuffer, *m_httpResponse,
      [this](auto const &a, auto const &b) { onDataReceived(a, b); });
}

void kucoin_futures_plug::doConnect() {
  using resolver = tcp::resolver;

  m_process = process_e::market_initiated;
  createRequestData();

  m_resolver.async_resolve(
      constants::kc_futures_api_host, "https",
      [this](auto const &errorCode, resolver::results_type const &results) {
        if (errorCode) {
          qDebug() << errorCode.message().c_str();
          return;
        }
        onHostResolved(results);
      });
}

void kucoin_futures_plug::startConnect() { doConnect(); }

void kucoin_futures_plug::onHostResolved(tcp::resolver::results_type const &result) {
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

void kucoin_futures_plug::createRequestData() {
  using http::field;

  char const *const path = "/api/v1/orders";
  m_userOrderID = get_random_string(38);

  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);
  writer.StartObject(); // {

  writer.Key("clientOid");              // key
  writer.String(m_userOrderID.c_str()); // value

  writer.Key("symbol");
  writer.String(m_tradeConfig->symbol.toUpper().toStdString().c_str());

  auto const marketType =
      korrelator::marketTypeToString(m_tradeConfig->marketType);
  bool const isMarketType = m_tradeConfig->marketType == market_type_e::market;
  writer.Key("type");
  writer.String(marketType.toStdString().c_str());

  writer.Key("side");
  writer.String(m_tradeConfig->side == trade_action_e::buy ? "buy" : "sell");

  double &size = m_tradeConfig->size;
  double &quoteAmount = m_tradeConfig->quoteAmount;

  if (isMarketType) {
    if (size == 0.0)
      size = quoteAmount;
    writer.Key("size");
    writer.Double(size);
  } else {
    m_price = format_quantity(m_price, 6);

    writer.Key("price");
    writer.String(QString::number(m_price).toStdString().c_str());

    writer.Key("size");
    writer.Int(quoteAmount);
  }

  writer.Key("leverage");
  writer.String(std::to_string(m_tradeConfig->leverage).c_str());

  writer.EndObject(); // }

  auto const payload = s.GetString();
  std::string const unixEpochTime = std::to_string(std::time(nullptr) * 1'000);
  auto const stringToSign = unixEpochTime + "POST" + path + payload;
  auto const signature =
      base64_encode(hmac256_encode(stringToSign, m_apiSecret));

  auto &httpRequest = m_httpRequest.emplace();
  httpRequest.clear();
  httpRequest.method(http::verb::post);
  httpRequest.version(11);
  httpRequest.target(path);
  httpRequest.set(field::host, constants::kc_futures_api_host);
  httpRequest.set(field::content_type, "application/json");
  httpRequest.set(field::user_agent, "postman");
  httpRequest.set(field::accept, "*/*");
  httpRequest.set(field::connection, "keep-alive");
  httpRequest.set("KC-API-SIGN", signature);
  httpRequest.set("KC-API-TIMESTAMP", unixEpochTime);
  httpRequest.set("KC-API-KEY", m_apiKey);
  httpRequest.set("KC-API-PASSPHRASE", m_apiPassphrase);
  httpRequest.set("KC-API-KEY-VERSION", "1");
  httpRequest.body() = payload;
  httpRequest.prepare_payload();

#ifndef TESTNET
  std::ostringstream ss;
  ss << httpRequest;
  qDebug() << ss.str().c_str();
#endif
}

void kucoin_futures_plug::performSSLHandshake() {
  beast::get_lowest_layer(m_tcpStream).expires_after(std::chrono::seconds(15));
  auto const host = constants::kc_futures_api_host;
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

kucoin_futures_plug::kucoin_futures_plug(net::io_context &ioContext,
                                 ssl::context &sslContext,
                                 api_data_t const &apiData,
                                 trade_config_data_t *tradeConfig)
  : m_ioContext(ioContext), m_sslContext(sslContext),
    m_tradeConfig(tradeConfig),
    m_apiKey(apiData.futuresApiKey.toStdString()),
    m_apiSecret(apiData.futuresApiSecret.toStdString()),
    m_apiPassphrase(apiData.futuresApiPassphrase.toStdString()),
    m_tcpStream(net::make_strand(ioContext), sslContext),
    m_resolver(ioContext) {}

kucoin_futures_plug::~kucoin_futures_plug() {}

void kucoin_futures_plug::onDataReceived(beast::error_code ec, std::size_t const) {
  if (ec)
    return;

  auto &body = m_httpResponse->body();
  size_t const bodyLength = body.length();

  rapidjson::Document doc;
  doc.Parse(body.c_str(), bodyLength);

  if (!doc.IsObject())
    goto onErrorEnd;

#ifdef _MSC_VER
#undef GetObject
#endif

  try {
    auto const jsonRoot = doc.GetObject();
    auto const codeIter = jsonRoot.FindMember("code");
    if (codeIter == jsonRoot.MemberEnd())
      goto onErrorEnd;

    auto const statusCode = codeIter->value.GetString();

    if (strcmp(statusCode, "429000") == 0) {
      if (m_process == process_e::market_initiated)
        m_process = process_e::monitoring_failed_market;
      return startMonitoringLastOrder();
    } else if (strcmp(statusCode, "100001") == 0) {
      qDebug() << body.c_str();
      m_process = process_e::limit_initiated;
      return initiateResendOrder();
    } else if (strcmp(statusCode, "200000") != 0) {
      goto onErrorEnd;
    }

    bool const successfulMarket =
        m_process == process_e::market_initiated ||
        m_process == process_e::monitoring_successful_request ||
        m_process == process_e::limit_initiated;

    if (successfulMarket) {
      auto const dataIter = jsonRoot.FindMember("data");
      if (dataIter == jsonRoot.MemberEnd())
        goto onErrorEnd;

      JsonObject const &dataObject = dataIter->value.GetObject();
      if (m_process == process_e::market_initiated ||
          m_process == process_e::limit_initiated) {
        m_process = process_e::monitoring_successful_request;

        auto const orderIter = dataObject.FindMember("orderId");
        if (orderIter == dataObject.MemberEnd())
          goto onErrorEnd;
        m_kucoinOrderID = orderIter->value.GetString();
        return startMonitoringLastOrder();
      } else if (m_process == process_e::monitoring_successful_request) {
        auto const clientOidIter = dataObject.FindMember("clientOid");
        if (clientOidIter == dataObject.MemberEnd()) {
          qDebug() << body.c_str();
          Q_ASSERT(false);
          goto noErrorEnd;
        }
        auto const clientOid = clientOidIter->value.GetString();
        Q_ASSERT(strcmp(clientOid, m_userOrderID.c_str()) == 0);

        auto const statusIter = dataObject.FindMember("status");
        if (statusIter == dataObject.MemberEnd()) {
          qDebug() << body.c_str();
          goto noErrorEnd;
        }
        auto const status = statusIter->value.GetString();
        if (strcmp(status, "open") == 0) {
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
          return startMonitoringLastOrder();
        } else if (strcmp(status, "done") == 0) {
          qDebug() << body.c_str();
          auto const sizeIter = dataObject.FindMember("filledSize");
          if (sizeIter != dataObject.MemberEnd())
            m_finalSizePurchased = sizeIter->value.GetInt();
          auto const quantityIter = dataObject.FindMember("filledValue");
          if (quantityIter != dataObject.MemberEnd())
            m_finalQuantityPurchased =
                std::stod(quantityIter->value.GetString());
        }
        goto noErrorEnd;
      } else if (dataObject.HasMember("orderId")) {
        m_process = process_e::monitoring_successful_request;
        auto const orderIter = dataObject.FindMember("orderId");
        if (orderIter == dataObject.MemberEnd())
          goto onErrorEnd;
        m_kucoinOrderID = orderIter->value.GetString();
        return startMonitoringLastOrder();
      }
    }
  } catch (...) {
    goto onErrorEnd;
  }

onErrorEnd:
  qDebug() << "There must have been an error" << body.c_str();
  m_errorString = body.c_str();
  goto noErrorEnd;

noErrorEnd:
  m_tcpStream.async_shutdown([](boost::system::error_code const) {});
}

void kucoin_futures_plug::startMonitoringLastOrder() {
  createMonitoringRequest();
  sendHttpsData();
}

void kucoin_futures_plug::initiateResendOrder() {
  if (++m_numberOfRetries > (int)maxOrderRetries) {
    m_errorString = "Maximum number of retries";
    return;
  }
  createRequestData();
#ifdef _DEBUG
  {
    std::stringstream ss;
    ss << *m_httpRequest;
    qDebug() << ss.str().c_str();
  }
#endif
  sendHttpsData();
}

void kucoin_futures_plug::createMonitoringRequest() {
  using http::field;

  std::string path = "/api/v1/orders/";
  if (!m_kucoinOrderID.isEmpty())
    path += m_kucoinOrderID.toStdString();
  else
    path += "byClientOid?clientOid=" + m_userOrderID;

  // Cause a small delay before grabbing the timer, the delay is needed so
  // kucoin does not return a 429 again when we check the status of the last
  // order
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  auto const unixEpochTime = std::to_string(std::time(nullptr) * 1'000);
  auto const signatureStr =
      base64_encode(hmac256_encode(unixEpochTime + "GET" + path, m_apiSecret));

  auto &httpRequest = m_httpRequest.emplace();
  httpRequest.method(http::verb::get);
  httpRequest.version(11);
  httpRequest.target(path);
  httpRequest.set(field::host, constants::kc_futures_api_host);
  httpRequest.set(field::content_type, "application/json");
  httpRequest.set(field::user_agent, "postman");
  httpRequest.set(field::accept, "*/*");
  httpRequest.set(field::connection, "keep-alive");
  httpRequest.set("KC-API-SIGN", signatureStr);
  httpRequest.set("KC-API-TIMESTAMP", unixEpochTime);
  httpRequest.set("KC-API-KEY", m_apiKey);
  httpRequest.set("KC-API-PASSPHRASE", m_apiPassphrase);
  httpRequest.set("KC-API-KEY-VERSION", "1");
  httpRequest.body() = {};
  httpRequest.prepare_payload();

#ifdef TESTNET
  std::stringstream ss;
  ss << httpRequest;
  qDebug() << ss.str().c_str();
#endif
}

} // namespace details
} // namespace korrelator
