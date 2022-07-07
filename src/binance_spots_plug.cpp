#include "binance_spots_plug.hpp"

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

namespace details {
void binance_spots_plug::sendHttpsData() {
  beast::get_lowest_layer(*m_tcpStream)
      .expires_after(std::chrono::milliseconds(15'000));
  http::async_write(*m_tcpStream, *m_httpRequest,
                    [this](auto const &a, auto const &b) { onDataSent(a, b); });
}

void binance_spots_plug::onDataSent(beast::error_code ec, std::size_t const) {
  if (ec) {
    qDebug() << "Problem writing\n" << ec.message().c_str();
    return;
  }
  receiveData();
}

void binance_spots_plug::receiveData() {
  m_httpResponse.emplace();
  m_readBuffer.emplace();

  beast::get_lowest_layer(*m_tcpStream)
      .expires_after(std::chrono::milliseconds(15000));
  http::async_read(
      *m_tcpStream, *m_readBuffer, *m_httpResponse,
      [this](auto const &a, auto const &b) { onDataReceived(a, b); });
}

void binance_spots_plug::doConnect() {
  using resolver = tcp::resolver;

  if (!createRequestData())
    return;
  m_resolver.async_resolve(
      constants::binance_http_spot_host, "https",
      [this](auto const &errorCode, resolver::results_type const &results) {
        if (errorCode) {
          qDebug() << errorCode.message().c_str();
          return;
        }
        onHostResolved(results);
      });
}

void binance_spots_plug::startConnect() { doConnect(); }

void binance_spots_plug::onHostResolved(
    tcp::resolver::results_type const &result) {
  m_tcpStream.emplace(m_ioContext, m_sslContext);
  beast::get_lowest_layer(*m_tcpStream).expires_after(std::chrono::seconds(30));
  beast::get_lowest_layer(*m_tcpStream)
      .async_connect(result, [this](auto const &errorCode, auto const &result) {
        if (errorCode) {
          qDebug() << errorCode.message().c_str();
          return;
        }
        performSSLHandshake(result);
      });
}

bool binance_spots_plug::createRequestData() {
  using http::field;

  QString query("symbol=" + m_tradeConfig->symbol.toUpper());
  query += "&side=";
  query += (m_tradeConfig->side == trade_action_e::buy ? "BUY" : "SELL");
  query += "&newOrderRespType=FULL";

  auto const marketType =
      korrelator::marketTypeToString(m_tradeConfig->marketType);
  query += "&type=" + marketType.toUpper();

  double &size = m_tradeConfig->size;
  double &quoteAmount = m_tradeConfig->quoteAmount;

  if (m_tradeConfig->marketType == korrelator::market_type_e::market) {
    if (quoteAmount != 0.0) { // usually in the case of a BUY
      if (!normalizeQuoteAmount(m_tradeConfig)) {
        m_errorString = "Available amount is lesser than the minimum";
        return false;
      }
      query += "&quoteOrderQty=";
      query += QString::number(quoteAmount, 'f', m_tradeConfig->quotePrecision);
    } else if (size != 0.0) { // usually in the case of a SELL
      size += m_tradeConfig->baseBalance;
      m_tradeConfig->baseBalance = 0.0;

      double const newTempSize =
          static_cast<double>(int(size / m_tradeConfig->tickSize)) *
          m_tradeConfig->tickSize;

      if ((newTempSize * m_price) < m_tradeConfig->quoteMinSize) {
        m_errorString = "MIN_NOTIONAL";
        return false;
      }

      if (newTempSize < size)
        m_tradeConfig->baseBalance = size - newTempSize;
      size = format_quantity(newTempSize, m_tradeConfig->quantityPrecision);
      query += "&quantity=";
      query += QString::number(size, 'f', m_tradeConfig->quantityPrecision);
    }
  } else {
    query += ("&timeInForce=GTC"); // Good Till Canceled
    if (size == 0.0 && quoteAmount != 0.0)
      size = quoteAmount / m_price;

    size = format_quantity(size, m_tradeConfig->quantityPrecision);
    query += ("&quantity=");
    query += QString::number(size, 'f', m_tradeConfig->quantityPrecision);

    m_price = format_quantity(m_price, m_tradeConfig->pricePrecision);
    query += ("&price=");
    query += QString::number(m_price, 'f', m_tradeConfig->pricePrecision);
  }

  query +=
      QString("&recvWindow=5000&timestamp=") + QString::number(getGMTTimeMs());

  auto const signature =
      hmac256_encode(query.toStdString(), m_apiSecret.toStdString(), true);
  query +=
      QString("&signature=") + reinterpret_cast<char const *>(signature.data());

  auto &httpRequest = m_httpRequest.emplace();
  httpRequest.method(http::verb::post);
  httpRequest.version(11);
  httpRequest.target("/api/v3/order" + std::string("?") + query.toStdString());
  httpRequest.set(field::host, constants::binance_http_spot_host);
  httpRequest.set(field::content_type, "application/json");
  httpRequest.set(field::user_agent, "postman");
  httpRequest.set(field::accept, "*/*");
  httpRequest.set(field::connection, "keep-alive");
  httpRequest.set("X-MBX-APIKEY", m_apiKey.toStdString());
  return true;
}

void binance_spots_plug::performSSLHandshake(
    tcp::resolver::results_type::endpoint_type const &ip) {
  beast::get_lowest_layer(*m_tcpStream).expires_after(std::chrono::seconds(15));
  auto const host = constants::binance_http_spot_host + std::string(":") +
                    std::to_string(ip.port());
  if (!SSL_set_tlsext_host_name(m_tcpStream->native_handle(), host.c_str())) {
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
                                 return sendHttpsData();
                               });
}

binance_spots_plug::binance_spots_plug(net::io_context &ioContext,
                                       ssl::context &sslContext,
                                       api_data_t const &apiData,
                                       trade_config_data_t *tradeConfig)
    : m_tradeAction(tradeConfig->side), m_ioContext(ioContext),
      m_sslContext(sslContext), m_tradeConfig(tradeConfig),
      m_apiKey(apiData.spotApiKey), m_apiSecret(apiData.spotApiSecret),
      m_resolver(ioContext), m_tcpStream(std::nullopt) {}

binance_spots_plug::~binance_spots_plug() {
  m_readBuffer.reset();
  m_httpRequest.reset();
  m_httpResponse.reset();
  m_tcpStream.reset();
}

void binance_spots_plug::onDataReceived(beast::error_code ec,
                                        std::size_t const bytesReceived) {
  if (ec) {
    qDebug() << ec.message().c_str() << bytesReceived;
    return;
  }

  auto &body = m_httpResponse->body();
  processOrderResponse(body.c_str(), body.length());
}

void binance_spots_plug::processOrderResponse(char const *str,
                                              size_t const length) {
  rapidjson::Document doc;
  doc.Parse(str, length);
  if (!doc.IsObject())
    return createErrorResponse();

  qDebug() << str;

#ifdef _MSC_VER
#undef GetObject
#endif

  try {
    auto const jsonRoot = doc.GetObject();
    auto const statusIter = jsonRoot.FindMember("status");
    auto const assignedOrderIDIter = jsonRoot.FindMember("clientOrderId");
    if (statusIter == jsonRoot.MemberEnd() ||
        assignedOrderIDIter == jsonRoot.MemberEnd())
      return createErrorResponse();
    QString const status = statusIter->value.GetString();
    m_userOrderID = assignedOrderIDIter->value.GetString();
    bool const isFullyFilled =
        status.compare("filled", Qt::CaseInsensitive) == 0;
    if (status.compare("new", Qt::CaseInsensitive) == 0) {
      auto const binanceOrderIDIter = jsonRoot.FindMember("orderId");
      if (binanceOrderIDIter != jsonRoot.MemberEnd() &&
          (binanceOrderIDIter->value.IsInt() ||
           binanceOrderIDIter->value.IsInt64())) {
        if (binanceOrderIDIter->value.IsInt64())
          m_binanceOrderID = binanceOrderIDIter->value.GetInt64();
        else if (binanceOrderIDIter->value.IsInt())
          m_binanceOrderID = (int64_t)binanceOrderIDIter->value.GetInt();
      }

      return startMonitoringNewOrder();
    } else if (isFullyFilled ||
               status.compare("partially_filled", Qt::CaseInsensitive) == 0) {
      auto const fillsIter = jsonRoot.FindMember("fills");
      auto const &baseCurrency = m_tradeConfig->baseCurrency;
      bool const isBuy = m_tradeConfig->side == trade_action_e::buy;
      double totalCommission = 0.0;

      if (fillsIter != jsonRoot.MemberEnd()) {
        auto const fills = fillsIter->value.GetArray();

        for (auto iter = fills.Begin(); iter != fills.End(); ++iter) {
          auto const fillsObject = iter->GetObject();
          auto const priceIter = fillsObject.FindMember("price");
          auto const qtyIter = fillsObject.FindMember("qty");
          auto const tradeIDIter = fillsObject.FindMember("tradeId");
          auto const commissionAssetIter =
              fillsObject.FindMember("commissionAsset");
          auto const commissionIter = fillsObject.FindMember("commission");

          if (any_of(fillsObject, qtyIter, tradeIDIter, priceIter,
                     commissionIter, commissionAssetIter)) {
            continue;
          }

          auto const newInsert =
              m_fillsTradeIds.insert(tradeIDIter->value.GetInt64());
          if (newInsert.second) { // insertion was successful
            m_averagePriceExecuted += std::stod(priceIter->value.GetString());
            m_finalSizePurchased += std::stod(qtyIter->value.GetString());
            if (baseCurrency.compare(commissionAssetIter->value.GetString(),
                                     Qt::CaseInsensitive) == 0) {
              totalCommission += std::stod(commissionIter->value.GetString());
            }
          }
        }
      }

      if (isFullyFilled) {
        auto otherSide = m_tradeConfig->oppositeSide;
        if (isBuy) {
          m_finalSizePurchased -= totalCommission;
          otherSide->size = m_finalSizePurchased;
          otherSide->quoteAmount = 0.0;
        } else {
          auto const cummulativeQuoteQtyIter =
              jsonRoot.FindMember("cummulativeQuoteQty");
          if (cummulativeQuoteQtyIter != jsonRoot.MemberEnd()) {
            otherSide->quoteAmount =
                std::stod(cummulativeQuoteQtyIter->value.GetString());
            otherSide->quoteAmount -= totalCommission;
          }
          otherSide->size = 0.0;
        }
      }

      if (status.compare("partially_filled", Qt::CaseInsensitive) == 0)
        return startMonitoringNewOrder();
      return disconnectConnection();
    } else if (status.compare("expired", Qt::CaseInsensitive) == 0) {
      if (!createRequestData())
        return createErrorResponse();
      return sendHttpsData();
    }
    return startMonitoringNewOrder();
  } catch (std::exception const &e) {
    qDebug() << e.what();
    return createErrorResponse();
  } catch (...) {
    return createErrorResponse();
  }
}

double binance_spots_plug::averagePrice() const {
  if (!m_fillsTradeIds.empty()) {
    return m_averagePriceExecuted / m_fillsTradeIds.size();
  }
  return m_averagePriceExecuted;
}

void binance_spots_plug::createErrorResponse() {
  if (m_httpResponse.has_value())
    m_errorString = m_httpResponse->body().c_str();
  qDebug() << "There must have been an error" << m_errorString;
  return disconnectConnection();
}

void binance_spots_plug::disconnectConnection() {
  m_tcpStream->async_shutdown(
      [](boost::system::error_code const) { qDebug() << "Stream closed"; });
}

void binance_spots_plug::startMonitoringNewOrder() {
  // allow for enough time before querying the exchange
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  createMonitoringRequest();
  sendHttpsData();
}

void binance_spots_plug::createMonitoringRequest() {
  using http::field;

  QString urlQuery("symbol=" + m_tradeConfig->symbol.toUpper());
  if (m_binanceOrderID != -1)
    urlQuery += "&orderId=" + QString::number(m_binanceOrderID);
  else
    urlQuery += "&origClientOrderId=" + m_userOrderID;

  urlQuery += "&timestamp=" + QString::number(getGMTTimeMs());
  auto const signature =
      hmac256_encode(urlQuery.toStdString(), m_apiSecret.toStdString(), true);
  urlQuery +=
      QString("&signature=") + reinterpret_cast<char const *>(signature.data());

  std::string const path = "/api/v3/order";
  auto &httpRequest = m_httpRequest.emplace();
  httpRequest.method(http::verb::get);
  httpRequest.version(11);
  httpRequest.target(path + "?" + urlQuery.toStdString());
  httpRequest.set(field::host, constants::binance_http_spot_host);
  httpRequest.set(field::content_type, "application/json");
  httpRequest.set(field::user_agent, "postman");
  httpRequest.set(field::accept, "*/*");
  httpRequest.set(field::connection, "keep-alive");
  httpRequest.set("X-MBX-APIKEY", m_apiKey.toStdString());
  httpRequest.prepare_payload();

  qDebug() << urlQuery;
}

} // namespace details

} // namespace korrelator
