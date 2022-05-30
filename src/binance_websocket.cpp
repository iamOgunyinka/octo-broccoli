#include "binance_websocket.hpp"

#include <QDebug>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

#include <rapidjson/document.h>

namespace korrelator {

binance_ws::~binance_ws() {
  m_resolver.reset();
  m_sslWebStream.reset();
  m_readBuffer.reset();
  m_writeBuffer.clear();
}

void binance_ws::startFetching() {
  if (m_requestedToStop)
    return;

  m_resolver.emplace(m_ioContext);
  m_resolver->async_resolve(
      m_host, m_port,
      [self = shared_from_this()](auto const error_code,
                                  results_type const &results) {
        if (error_code) {
          qDebug() << error_code.message().c_str();
          return;
        }
        self->websockConnectToResolvedNames(results);
      });
}

void binance_ws::websockConnectToResolvedNames(
    resolver_result_type const &resolvedNames) {
  m_resolver.reset();
  m_sslWebStream.emplace(m_ioContext, m_sslContext);
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(30));
  beast::get_lowest_layer(*m_sslWebStream)
      .async_connect(
          resolvedNames,
          [self = shared_from_this()](
              auto const &errorCode,
              resolver_result_type::endpoint_type const &connectedName) {
            if (errorCode) {
              qDebug() << errorCode.message().c_str();
              return;
            }
            self->websockPerformSslHandshake(connectedName);
          });
}

void binance_ws::websockPerformSslHandshake(
    resolver_result_type::endpoint_type const &endpoint) {
  auto const host = m_host + ":" + std::to_string(endpoint.port());

  // Set a timeout on the operation
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(30));

  // Set SNI Hostname (many hosts need this to handshake successfully)
  if (!SSL_set_tlsext_host_name(m_sslWebStream->next_layer().native_handle(),
                                host.c_str())) {
    auto const ec = beast::error_code(static_cast<int>(::ERR_get_error()),
                                      net::error::get_ssl_category());
    qDebug() << ec.message().c_str();
    return;
  }
  negotiateWebsocketConnection();
}

void binance_ws::negotiateWebsocketConnection() {
  m_sslWebStream->next_layer().async_handshake(
      net::ssl::stream_base::client,
      [self = shared_from_this()](beast::error_code const ec) {
        if (ec) {
          qDebug() << ec.message().c_str();
          return;
        }
        beast::get_lowest_layer(*self->m_sslWebStream).expires_never();
        self->performWebsocketHandshake();
      });
}

void binance_ws::performWebsocketHandshake() {
  auto const urlPath = "/stream?streams=" + m_tokenName.tokenName + "@aggTrade";

  auto opt = beast::websocket::stream_base::timeout();
  opt.idle_timeout = std::chrono::seconds(50);
  opt.handshake_timeout = std::chrono::seconds(20);
  opt.keep_alive_pings = true;
  m_sslWebStream->set_option(opt);

  m_sslWebStream->control_callback(
      [self = shared_from_this()](auto const frame_type, auto const &) {
        if (frame_type == beast::websocket::frame_type::close) {
          self->m_sslWebStream.reset();
          return self->startFetching();
        }
      });

  m_sslWebStream->async_handshake(
      m_host, urlPath.toStdString(),
      [self = shared_from_this()](beast::error_code const ec) {
        if (ec) {
          qDebug() << ec.message().c_str();
          return;
        }

        self->waitForMessages();
      });
}

void binance_ws::waitForMessages() {
  m_readBuffer.emplace();
  m_sslWebStream->async_read(
      *m_readBuffer,
      [self = shared_from_this()](beast::error_code const error_code,
                                  std::size_t const) {
        if (error_code == net::error::operation_aborted) {
          qDebug() << error_code.message().c_str();
          return;
        } else if (error_code) {
          qDebug() << error_code.message().c_str();
          self->m_sslWebStream.reset();
          return self->startFetching();
        }
        self->interpretGenericMessages();
      });
}

void binance_ws::interpretGenericMessages() {
  if (m_requestedToStop)
    return;

  char const *bufferCstr =
      static_cast<char const *>(m_readBuffer->cdata().data());
  auto const optPrice = binanceGetCoinPrice(bufferCstr, m_readBuffer->size());
  if (optPrice != -1.0) {
    m_priceResult = optPrice;

    qDebug() << "Binance" << m_tokenName.tokenName << m_priceResult;
  }

  if (!m_tokenName.subscribed)
    return makeSubscription();
  waitForMessages();
}

void binance_ws::makeSubscription() {
  m_writeBuffer = QString(R"(
    {
      "method": "SUBSCRIBE",
      "params":
      [
        "%1@ticker",
        "%1@aggTrade"
      ],
      "id": 10
    })")
                      .arg(m_tokenName.tokenName)
                      .toStdString();

  m_sslWebStream->async_write(
      net::buffer(m_writeBuffer),
      [self = shared_from_this()](auto const errCode, size_t const) {
        if (errCode) {
          qDebug() << errCode.message().c_str();
          return;
        }
        self->m_writeBuffer.clear();
        self->m_tokenName.subscribed = true;
        self->waitForMessages();
      });
}

#ifdef _MSC_VER
#undef GetObject
#endif

double binanceGetCoinPrice(char const *str, size_t const size) {
  rapidjson::Document d;
  d.Parse(str, size);

  try {
    auto const jsonObject = d.GetObject();
    auto iter = jsonObject.FindMember("data");
    if (iter == jsonObject.end())
      return -1.0;
    auto const dataObject = iter->value.GetObject();
    auto const typeIter = dataObject.FindMember("e");
    if (typeIter == dataObject.MemberEnd()) {
      Q_ASSERT(false);
      return -1.0;
    }

    std::string const type = typeIter->value.GetString();
    bool const is24HrTicker =
        type.length() == 10 && type[0] == '2' && type.back() == 'r';
    bool const isAggregateTrade = type.length() == 8 && type[0] == 'a' &&
                                  type[3] == 'T' && type.back() == 'e';
    if (is24HrTicker || isAggregateTrade) {
      char const *amountStr = isAggregateTrade ? "p" : "c";
      auto const amount =
          std::atof(dataObject.FindMember(amountStr)->value.GetString());
      return amount;
    }
  } catch (...) {
    return -1.0;
  }
  return -1.0;
}

} // namespace korrelator
