#include "ftx_futures_plug.hpp"

#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "crypto.hpp"
#include <QDebug>
#include <QMessageAuthenticationCode>
#include <thread>

namespace korrelator {

namespace details {

ftx_futures_plug::ftx_futures_plug(net::io_context &ioContext,
                                   ssl::context &sslContext,
                                   api_data_t const &apiData,
                                   trade_config_data_t *tradeConfig)
    : m_ioContext(ioContext), m_sslContext(sslContext),
      m_tradeConfig(tradeConfig), m_resolver(ioContext),
      m_apiKey(apiData.futuresApiKey), m_apiSecret(apiData.futuresApiSecret) {}

ftx_futures_plug::~ftx_futures_plug() {
  m_readBuffer.reset();
  m_httpRequest.reset();
  m_httpResponse.reset();
  m_tcpStream.reset();
}

void ftx_futures_plug::setAccountLeverage() {
  m_requestStatus = request_status_e::setting_leverage;
}

void ftx_futures_plug::doConnect() {
  using resolver = tcp::resolver;

  if (!createRequestData())
    return;
  m_resolver.async_resolve(
      "ftx.com", "https",
      [this](auto const &errorCode, resolver::results_type const &results) {
        if (errorCode) {
          qDebug() << errorCode.message().c_str();
          return;
        }
        onHostResolved(results);
      });
}

bool ftx_futures_plug::createLeverageRequestData() {
  using http::field;

  auto const path = "/api/account/leverage";
  auto const payload =
      QString("{\"leverage\": %1}").arg(m_tradeConfig->leverage).toUtf8();
  auto const timeNowStr = QByteArray::number(getGMTTimeMs());
  QByteArray const signaturePayload = timeNowStr + "POST" + path + payload;
  auto const signature =
      QMessageAuthenticationCode::hash(signaturePayload, m_apiSecret.toUtf8(),
                                       QCryptographicHash::Sha256)
          .toHex()
          .toStdString();

  auto &httpRequest = m_httpRequest.emplace();
  httpRequest.method(http::verb::post);
  httpRequest.version(11);
  httpRequest.target(path);
  httpRequest.set(field::host, "ftx.com");
  httpRequest.set(field::content_type, "application/json");
  httpRequest.set(field::connection, "keep-alive");
  httpRequest.set("FTX-KEY", m_apiKey.toStdString());
  httpRequest.set("FTX-SIGN", signature);
  httpRequest.set("FTX-TS", timeNowStr.toStdString());
  httpRequest.body() = payload.toStdString();
  httpRequest.prepare_payload();
  return true;
}

bool ftx_futures_plug::createNewOrderRequestData() {
  using http::field;

  bool const isBuying = m_tradeConfig->side == trade_action_e::buy;
  bool const isMarket = m_tradeConfig->marketType == market_type_e::market;

  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);
  writer.StartObject(); // {

  writer.Key("side");
  writer.String(isBuying ? "buy" : "sell");

  writer.Key("market");
  writer.String(m_tradeConfig->symbol.toUpper().toStdString().c_str());

  m_userOrderID = get_random_string(14).c_str();
  writer.Key("clientId");
  writer.String(m_userOrderID.toStdString().c_str());

  double &size = m_tradeConfig->size;
  double &quoteAmount = m_tradeConfig->quoteAmount;

  writer.Key("type");
  if (isMarket) {
    writer.String("market");

    writer.Key("price");
    writer.Null();

    writer.Key("size");
    if (size == 0.0 && quoteAmount != 0.0) {
      if (!normalizeQuoteAmount(m_tradeConfig)) {
        m_errorString = "Available amount is lesser than the minimum";
        return false;
      }

      size = format_quantity(quoteAmount / m_price, m_tradeConfig->quotePrecision);
    }
    writer.Double(size);
  } else {
    writer.String("limit");
    if (size == 0.0 && quoteAmount != 0.0)
      size = quoteAmount / m_price;

    size = format_quantity(size, m_tradeConfig->quantityPrecision);
    writer.Key("size");
    writer.Double(size);

    m_price = format_quantity(m_price, m_tradeConfig->pricePrecision);
    writer.Key("price");
    writer.Double(m_price);
  }

  writer.EndObject(); // }

  char const *const path = "/api/orders";
  auto const payload =
      QString(s.GetString()).replace(",", ", ").replace(":", ": ").toUtf8();
  auto const timeNowStr = QByteArray::number(getGMTTimeMs());
  QByteArray const signaturePayload = timeNowStr + "POST" + path + payload;
  auto const signature =
      QMessageAuthenticationCode::hash(signaturePayload, m_apiSecret.toUtf8(),
                                       QCryptographicHash::Sha256)
          .toHex()
          .toStdString();

  auto &httpRequest = m_httpRequest.emplace();
  httpRequest.method(http::verb::post);
  httpRequest.version(11);
  httpRequest.target(path);
  httpRequest.set(field::host, "ftx.com");
  httpRequest.set(field::content_type, "application/json");
  httpRequest.set(field::connection, "keep-alive");
  httpRequest.set("FTX-KEY", m_apiKey.toStdString());
  httpRequest.set("FTX-SIGN", signature);
  httpRequest.set("FTX-TS", timeNowStr.toStdString());
  httpRequest.body() = payload.toStdString();
  httpRequest.prepare_payload();

#ifdef _DEBUG
  std::ostringstream ss;
  ss << httpRequest;
  qDebug() << ss.str().c_str();
#endif

  m_requestStatus = request_status_e::new_order;
  return true;
}

bool ftx_futures_plug::createRequestData() {
  using http::field;

  if (m_requestStatus == request_status_e::setting_leverage)
    return createLeverageRequestData();
  return createNewOrderRequestData();
}

void ftx_futures_plug::onHostResolved(
    tcp::resolver::results_type const &result) {
  m_tcpStream.emplace(m_ioContext, m_sslContext);
  beast::get_lowest_layer(*m_tcpStream).expires_after(std::chrono::seconds(30));
  beast::get_lowest_layer(*m_tcpStream)
      .async_connect(result, [this](auto const &errorCode, auto const &) {
        if (errorCode) {
          qDebug() << errorCode.message().c_str();
          return;
        }
        performSSLHandshake();
      });
}

void ftx_futures_plug::performSSLHandshake() {
  beast::get_lowest_layer(*m_tcpStream).expires_after(std::chrono::seconds(15));
  if (!SSL_set_tlsext_host_name(m_tcpStream->native_handle(), "ftx.com")) {
    auto const ec = beast::error_code(static_cast<int>(::ERR_get_error()),
                                      net::error::get_ssl_category());
    qDebug() << ec.message().c_str();
    return;
  }

  m_tcpStream->async_handshake(ssl::stream_base::client,
                               [this](beast::error_code const ec) {
                                 if (ec) {
                                   qDebug() << ec.message().c_str();
                                   return;
                                 }
                                 return sendDataToFtx();
                               });
}

void ftx_futures_plug::createErrorResponse() {
  if (m_httpResponse.has_value())
    m_errorString = m_httpResponse->body().c_str();
  qDebug() << "There must have been an error" << m_errorString;
  return disconnectConnection();
}

void ftx_futures_plug::disconnectConnection() {
  qDebug() << "Disconnecting...";
  m_tcpStream->async_shutdown(
      [](boost::system::error_code const) { qDebug() << "Stream closed"; });
}

void ftx_futures_plug::sendDataToFtx() {
  beast::get_lowest_layer(*m_tcpStream).expires_after(std::chrono::seconds(15));
  http::async_write(
      *m_tcpStream, *m_httpRequest, [this](auto const errorCode, size_t const) {
        if (!errorCode)
          return receiveData();
        qDebug() << "Problem writing\n" << errorCode.message().c_str();
      });
}

void ftx_futures_plug::receiveData() {
  m_httpResponse.emplace();
  m_readBuffer.emplace();

  beast::get_lowest_layer(*m_tcpStream)
      .expires_after(std::chrono::milliseconds(15000));
  http::async_read(*m_tcpStream, *m_readBuffer, *m_httpResponse,
                   [this](auto const errorCode, size_t const bytesReceived) {
                     if (errorCode) {
                       qDebug() << errorCode.message().c_str() << bytesReceived;
                       return;
                     }

                     auto &body = m_httpResponse->body();
                     processOrderResponse(body.c_str(), body.length());
                   });
}

void ftx_futures_plug::processOrderResponse(char const *str,
                                            size_t const length) {
  qDebug() << str;

  rapidjson::Document doc;
  try {
    doc.Parse(str, length);

#ifdef _MSC_VER
#undef GetObject
#endif
    auto const rootObject = doc.GetObject();
    auto const successStatusIter = rootObject.FindMember("success");
    if (successStatusIter == rootObject.MemberEnd() ||
        !successStatusIter->value.GetBool())
      return createErrorResponse();
    if (m_requestStatus == request_status_e::setting_leverage) {
      m_requestStatus = request_status_e::new_order;
      (void)createRequestData();
      return sendDataToFtx();
    }

    auto const resultIter = rootObject.FindMember("result");
    if (resultIter == rootObject.MemberEnd())
      return createErrorResponse();

    if (m_requestStatus == request_status_e::new_order ||
        m_requestStatus == request_status_e::check_status) {
      auto const resultObject = resultIter->value.GetObject();
      auto const clientID =
          resultObject.FindMember("clientId")->value.GetString();
      if (m_userOrderID != clientID) {
        qDebug() << "The client ID do not match";
        return createErrorResponse();
      }
      QString const status =
          resultObject.FindMember("status")->value.GetString();
      auto const orderID = resultObject.FindMember("id")->value.GetInt64();
      if (status == "new" || status == "open") {
        m_requestStatus = request_status_e::check_status;
        m_ftxOrderID = orderID;
        return monitorOrderStatus();
      } else if (status.compare("closed", Qt::CaseInsensitive) == 0) {
        m_requestStatus = request_status_e::check_fills;
        if (m_ftxOrderID != orderID)
          return createErrorResponse();
        return monitorOrderStatus();
      }
    } else if (request_status_e::check_fills == m_requestStatus) {
      auto const resultList = resultIter->value.GetArray();
      double totalPrice = 0.0;
      double totalSize = 0.0;

      for (auto const &jsonResult : resultList) {
        auto const itemObject = jsonResult.GetObject();
        auto const price = itemObject.FindMember("price")->value.GetDouble();
        totalSize += itemObject.FindMember("size")->value.GetDouble();
        totalPrice += price;
      }
      m_tradeConfig->quoteAmount = 0.0;
      if (m_tradeConfig->size == 0.0)
        m_tradeConfig->size = totalSize;
      m_averagePrice = totalPrice / (double)resultList.Size();
    }
  } catch (...) {
    qDebug() << "Error parsing JSONResponse:" << str;
    return createErrorResponse();
  }
  disconnectConnection();
}

void ftx_futures_plug::monitorOrderStatus() {
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  createMonitoringRequest();
  sendDataToFtx();
}

void ftx_futures_plug::createMonitoringRequest() {
  using http::field;

  QString path;
  if (m_requestStatus == request_status_e::check_fills) {
    path = "/api/fills?orderId=" + QString::number(m_ftxOrderID);
  } else if (m_requestStatus == request_status_e::check_status) {
    path = "/api/orders/" + QString::number(m_ftxOrderID);
  } else {
    Q_ASSERT(false);
  }

  auto const timeNowStr = QByteArray::number(getGMTTimeMs());
  auto const signaturePayload = timeNowStr + "GET" + path;
  auto const signature = QMessageAuthenticationCode::hash(
                             signaturePayload.toUtf8(), m_apiSecret.toUtf8(),
                             QCryptographicHash::Sha256)
                             .toHex()
                             .toStdString();

  auto &httpRequest = m_httpRequest.emplace();
  httpRequest.method(http::verb::get);
  httpRequest.version(11);
  httpRequest.target(path.toStdString());
  httpRequest.set(field::host, "ftx.com");
  httpRequest.set(field::content_type, "application/json");
  httpRequest.set(field::connection, "keep-alive");
  httpRequest.set("FTX-KEY", m_apiKey.toStdString());
  httpRequest.set("FTX-SIGN", signature);
  httpRequest.set("FTX-TS", timeNowStr.toStdString());
  httpRequest.body() = {};
  httpRequest.prepare_payload();

#ifdef _DEBUG
  std::ostringstream ss;
  ss << httpRequest;
  qDebug() << ss.str().c_str();
#endif
}

} // namespace details
} // namespace korrelator
