#include "kucoin_spots_plug.hpp"

#include <QDebug>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

#include "constants.hpp"
#include "crypto.hpp"
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <thread>

extern double maxOrderRetries;

namespace korrelator {

bool normalizeQuoteAmount(trade_config_data_t* tradeConfig) {
  double &quoteAmount = tradeConfig->quoteAmount;

  if (tradeConfig->originalQuoteAmount > quoteAmount) {
    if (tradeConfig->quoteBalance > 0.0) {
      auto const amountNeeded =
          tradeConfig->originalQuoteAmount - quoteAmount;
      if (tradeConfig->quoteBalance > amountNeeded) {
        quoteAmount = tradeConfig->originalQuoteAmount;
        tradeConfig->quoteBalance -= amountNeeded;
      } else {
        quoteAmount += tradeConfig->quoteBalance;
        tradeConfig->quoteBalance = 0.0;
      }
    }
  } else if (tradeConfig->originalQuoteAmount < quoteAmount) {
    tradeConfig->quoteBalance +=
        (quoteAmount - tradeConfig->originalQuoteAmount);
    quoteAmount = tradeConfig->originalQuoteAmount;
  }
  auto const newQuoteAmount =
      format_quantity(quoteAmount, tradeConfig->quotePrecision);
  if (newQuoteAmount < quoteAmount)
    tradeConfig->quoteBalance += (quoteAmount - newQuoteAmount);
  quoteAmount = newQuoteAmount;
  return quoteAmount >= tradeConfig->quoteMinSize;
}

namespace details {

void kucoin_spots_plug::sendHttpsData() {
  beast::get_lowest_layer(m_tcpStream)
      .expires_after(std::chrono::milliseconds(15'000));
  http::async_write(
        m_tcpStream, *m_httpRequest,
        [this](auto const &a, auto const &b) { onDataSent(a, b); });
}

void kucoin_spots_plug::onDataSent(beast::error_code ec, std::size_t const) {
  if (ec) {
    qDebug() << "Problem writing\n" << ec.message().c_str();
    return;
  }
  receiveData();
}

void kucoin_spots_plug::receiveData() {
  m_httpResponse.emplace();
  m_readBuffer.emplace();

  beast::get_lowest_layer(m_tcpStream)
      .expires_after(std::chrono::milliseconds(15'000));
  http::async_read(
      m_tcpStream, *m_readBuffer, *m_httpResponse,
      [this](auto const &a, auto const &b) { onDataReceived(a, b); });
}

void kucoin_spots_plug::doConnect() {
  using resolver = tcp::resolver;

  m_process = process_e::market_initiated;
  if (!createRequestData())
    return;

  m_resolver.async_resolve(
      constants::kucoin_https_spot_host, "https",
      [this](auto const &errorCode, resolver::results_type const &results) {
        if (errorCode) {
          qDebug() << errorCode.message().c_str();
          return;
        }
        onHostResolved(results);
      });
}

void kucoin_spots_plug::startConnect() { doConnect(); }

void kucoin_spots_plug::onHostResolved(tcp::resolver::results_type const &result) {
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

void kucoin_spots_plug::processRemainingDataPage() {

}

bool kucoin_spots_plug::createRequestData() {
  using http::field;

  char const *const path = "/api/v1/orders";

  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);
  writer.StartObject(); // {

  auto const userOrderID = get_random_string(28);
  writer.Key("clientOid");             // key
  writer.String(userOrderID.c_str()); // value

  writer.Key("side");
  writer.String(m_tradeConfig->side == trade_action_e::buy ? "buy" : "sell");

  writer.Key("symbol");
  writer.String(m_tradeConfig->symbol.toUpper().toStdString().c_str());

  auto const marketType =
      korrelator::marketTypeToString(m_tradeConfig->marketType);

  writer.Key("type");
  writer.String(marketType.toStdString().c_str());

  writer.Key("tradeType");
  writer.String("TRADE");

  bool const isMarketType = m_tradeConfig->marketType == market_type_e::market;
  double &size = m_tradeConfig->size;
  double &quoteAmount = m_tradeConfig->quoteAmount;
  bool const hasQuoteAmount = quoteAmount != 0.0;
  bool const hasSizeDefined = size != 0.0;

  if (isMarketType) {
    if (hasQuoteAmount) {
      if (!normalizeQuoteAmount(m_tradeConfig)) {
        m_errorString = "Available amount is lesser than the minimum";
        return false;
      }

      quoteAmount = format_quantity(quoteAmount, m_tradeConfig->quotePrecision);
      writer.Key("funds");
      auto quoteAmountStr =
          QString::number(quoteAmount, 'f', m_tradeConfig->quotePrecision);
      writer.String(quoteAmountStr.toStdString().c_str());
    } else if (hasSizeDefined) {
      size = format_quantity(size, m_tradeConfig->quantityPrecision);
      writer.Key("size");
      auto const sizeStr =
          QString::number(size, 'f', m_tradeConfig->quantityPrecision);
      writer.String(sizeStr.toStdString().c_str());
    } else if (hasSizeDefined && hasQuoteAmount) {
      throw std::runtime_error("this should never happen");
    }
  } else {
    writer.Key("timeInForce");
    writer.String("GTC");

    m_price = format_quantity(m_price, 6);
    writer.Key("price");
    writer.String(QString::number(m_price).toStdString().c_str());

    writer.Key("size");
    writer.String(std::to_string(size).c_str());
  }

  writer.EndObject(); // }

  auto const payload = s.GetString();
  std::string const unixEpochTime = std::to_string(std::time(nullptr) * 1'000);
  auto const stringToSign = unixEpochTime + "POST" + path + payload;
  auto const signature =
      base64_encode(hmac256_encode(stringToSign, m_apiSecret));

  auto &httpRequest = m_httpRequest.emplace();
  httpRequest.method(http::verb::post);
  httpRequest.version(11);
  httpRequest.target(path);
  httpRequest.set(field::host, constants::kucoin_https_spot_host);
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

  qDebug() << payload;
#ifdef TESTNET
  std::ostringstream ss;
  ss << httpRequest;
  qDebug() << ss.str().c_str();
#endif

  return true;
}

void kucoin_spots_plug::performSSLHandshake() {
  beast::get_lowest_layer(m_tcpStream).expires_after(std::chrono::seconds(15));
  auto const host = constants::kucoin_https_spot_host;
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

kucoin_spots_plug::kucoin_spots_plug(net::io_context &ioContext,
                             ssl::context &sslContext,
                             api_data_t const &apiData,
                             trade_config_data_t *tradeConfig)
    : m_tradeAction(tradeConfig->side), m_ioContext(ioContext),
      m_sslContext(sslContext), m_tradeConfig(tradeConfig),
      m_apiKey(apiData.spotApiKey.toStdString()),
      m_apiSecret(apiData.spotApiSecret.toStdString()),
      m_apiPassphrase(apiData.spotApiPassphrase.toStdString()),
      m_tcpStream(ioContext, sslContext),
      m_resolver(ioContext) {}

kucoin_spots_plug::~kucoin_spots_plug() {

}

void kucoin_spots_plug::onDataReceived(beast::error_code ec, std::size_t const) {
  if (ec) {
    qDebug() << ec.message().c_str();
    return;
  }

  auto &body = m_httpResponse->body();
  size_t const bodyLength = body.length();

  rapidjson::Document doc;
  doc.Parse(body.c_str(), bodyLength);

  if (!doc.IsObject())
    return reportError();

  qDebug() << body.c_str();

#ifdef _MSC_VER
#undef GetObject
#endif

  try {
    auto const jsonRoot = doc.GetObject();
    auto const codeIter = jsonRoot.FindMember("code");
    if (codeIter == jsonRoot.MemberEnd())
      return reportError();

    auto const statusCode = codeIter->value.GetString();

    if (strcmp(statusCode, "429000") == 0) {
      if (m_process == process_e::market_initiated)
        m_process = process_e::monitoring_failed_market;
      return startMonitoringLastOrder();
    } else if (strcmp(statusCode, "100001") == 0) {
      qDebug() << body.c_str();
      m_process = process_e::limit_initiated;
      return initiateLimitOrder();
    } else if (strcmp(statusCode, "200000") != 0) {
      return reportError();
    }

    bool const successfulMarket =
        m_process == process_e::market_initiated ||
        m_process == process_e::monitoring_successful_request ||
        m_process == process_e::limit_initiated;

    if (successfulMarket) {
      auto const dataIter = jsonRoot.FindMember("data");
      if (dataIter == jsonRoot.MemberEnd())
        return reportError();

      JsonObject const &dataObject = dataIter->value.GetObject();
      if (m_process == process_e::market_initiated ||
          m_process == process_e::limit_initiated) {
        m_process = process_e::monitoring_successful_request;

        auto const orderIter = dataObject.FindMember("orderId");
        if (orderIter == dataObject.MemberEnd())
          return reportError();
        m_kucoinOrderID = orderIter->value.GetString();
        return startMonitoringLastOrder();
      } else if (dataObject.HasMember("orderId")) {
        m_process = process_e::monitoring_successful_request;
        m_kucoinOrderID = dataObject.FindMember("orderId")->value.GetString();

        return startMonitoringLastOrder();
      } else if (m_process == process_e::monitoring_successful_request) {
        return parseSuccessfulResponse(dataObject);
      }
    }
  } catch (std::exception const & e){
    qDebug() << e.what();
  } catch (...) {
  }
  return reportError();
}

void kucoin_spots_plug::reportError(QString const & errorString) {
  if (!errorString.isEmpty())
    m_errorString = errorString;
  else
    m_errorString = m_httpResponse->body().c_str();
  qDebug() << "There must have been an error" << m_errorString;
  severConnection();
}

void kucoin_spots_plug::severConnection() {
  m_tcpStream.async_shutdown([](boost::system::error_code const) {});
}

void kucoin_spots_plug::parseSuccessfulResponse(JsonObject const &dataObject ) {
  auto const itemsIter = dataObject.FindMember("items");
  if (itemsIter == dataObject.MemberEnd())
    return reportError();
  double totalCommission = 0.0;
  double totalFunds = 0.0;
  double totalSize = 0.0;

  auto const tradeItems = itemsIter->value.GetArray();
  for (auto const &itemObject: tradeItems) {
    auto const tradeItemObject = itemObject.GetObject();
    auto const orderIdIter = tradeItemObject.FindMember("orderId");
    if (orderIdIter == tradeItemObject.MemberEnd() ||
        !orderIdIter->value.IsString() ||
        m_kucoinOrderID.compare(
        orderIdIter->value.GetString(), Qt::CaseInsensitive) != 0)
      continue;
    auto const priceIter = tradeItemObject.FindMember("price");
    auto const sizeIter = tradeItemObject.FindMember("size");
    auto const fundsIter = tradeItemObject.FindMember("funds");
    auto const feeCurrencyIter = tradeItemObject.FindMember("feeCurrency");
    auto const feeIter = tradeItemObject.FindMember("fee");
    if (any_of(tradeItemObject, priceIter, sizeIter, fundsIter,
               feeCurrencyIter, feeIter)) {
      continue;
    }

    m_averagePrice += std::stod(priceIter->value.GetString());
    totalSize += std::stod(sizeIter->value.GetString());
    totalFunds += std::stod(fundsIter->value.GetString());
    auto const feeCurrency = feeCurrencyIter->value.GetString();
    if (m_tradeConfig->baseCurrency.compare(feeCurrency, Qt::CaseInsensitive) == 0) {
      totalCommission += std::stod(feeIter->value.GetString());
    }
  }

  int totalPage = 1;
  if (auto iter = dataObject.FindMember("totalPage"); iter != dataObject.MemberEnd())
    totalPage = iter->value.GetInt();

  if (totalPage > 1)
    return processRemainingDataPage();

  if (tradeItems.Size() != 0)
    m_averagePrice /= double(tradeItems.Size());

  auto otherSide = m_tradeConfig->oppositeSide;
  if (m_tradeConfig->side == trade_action_e::buy) {
    otherSide->size = totalSize - totalCommission;
    otherSide->quoteAmount = 0.0;
  } else {
    otherSide->quoteAmount = totalFunds - totalCommission;
    otherSide->size = 0.0;
  }

  return severConnection();
}

void kucoin_spots_plug::startMonitoringLastOrder() {
  createMonitoringRequest();
  sendHttpsData();
}

void kucoin_spots_plug::initiateLimitOrder() {
  if (++m_numberOfRetries > (int)maxOrderRetries) {
    m_errorString = "Maximum number of retries";
    return;
  }

  if (!createRequestData())
    return;

#ifdef _DEBUG
  {
    std::stringstream ss;
    ss << *m_httpRequest;
    qDebug() << ss.str().c_str();
  }
#endif
  sendHttpsData();
}

void kucoin_spots_plug::createMonitoringRequest() {
  using http::field;

  auto const host = constants::kucoin_https_spot_host;
  std::string const path = "/api/v1/fills?" + m_kucoinOrderID.toStdString();

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
  httpRequest.set(field::host, host);
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
}

} // namespace korrelator
