#include "cwebsocket.hpp"
#include <QDebug>
#include <QThread>

#include "kc_websocket.hpp"
#include <rapidjson/document.h>

namespace korrelator {

namespace detail {
char const *const custom_socket::futures_url = "fstream.binance.com";
char const *const custom_socket::spot_url = "stream.binance.com";
char const *const custom_socket::spot_port = "9443";
char const *const custom_socket::futures_port = "443";

custom_socket::~custom_socket() {
  m_resolver.reset();
  m_sslWebStream.reset();
  m_readBuffer.reset();
  m_writeBuffer.clear();
}

void custom_socket::startFetching() {
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

void custom_socket::websockConnectToResolvedNames(
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

void custom_socket::websockPerformSslHandshake(
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

void custom_socket::negotiateWebsocketConnection() {
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

void custom_socket::performWebsocketHandshake() {
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

void custom_socket::waitForMessages() {
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

void custom_socket::interpretGenericMessages() {
  if (m_requestedToStop)
    return;

  char const *bufferCstr =
      static_cast<char const *>(m_readBuffer->cdata().data());
  auto const optPrice = binanceGetCoinPrice(bufferCstr, m_readBuffer->size());
  if (!isnan(optPrice))
    emit onNewPriceAvailable(m_tokenName.tokenName, optPrice,
                             exchange_name_e::binance, m_tradeType);

  if (!m_tokenName.subscribed)
    return makeSubscription();
  waitForMessages();
}

void custom_socket::makeSubscription() {
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

void custom_socket::requestStop() { m_requestedToStop = true; }

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
      return NAN;
    auto const dataObject = iter->value.GetObject();
    auto const typeIter = dataObject.FindMember("e");
    if (typeIter == dataObject.MemberEnd()) {
      Q_ASSERT(false);
      return NAN;
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
    return NAN;
  }
  return NAN;
}

std::unique_ptr<net::io_context> &getRawIOContext() {
  static std::unique_ptr<net::io_context> ioContext = nullptr;
  return ioContext;
}

net::io_context *getIOContext() {
  auto &ioContext = getRawIOContext();
  if (!ioContext) {
    ioContext =
        std::make_unique<net::io_context>(std::thread::hardware_concurrency());
  }
  return ioContext.get();
}

net::ssl::context &getSSLContext() {
  static std::unique_ptr<net::ssl::context> ssl_context = nullptr;
  if (!ssl_context) {
    ssl_context =
        std::make_unique<net::ssl::context>(net::ssl::context::tlsv12_client);
    ssl_context->set_default_verify_paths();
    ssl_context->set_verify_mode(boost::asio::ssl::verify_none);
  }
  return *ssl_context;
}

} // namespace detail

cwebsocket::cwebsocket() : m_sslContext(detail::getSSLContext()) {
  detail::getRawIOContext().reset();
  m_ioContext = detail::getIOContext();
}

cwebsocket::~cwebsocket() {
  m_ioContext->stop();

  for (auto &sock : m_sockets) {
    std::visit([](auto && v) {
      v->requestStop();
      delete v;
    }, sock);
  }

  m_sockets.clear();
}

void cwebsocket::addSubscription(QString const &tokenName,
                                 trade_type_e const tradeType,
                                 exchange_name_e const exchange) {
  auto iter = m_checker.find(exchange);
  if (iter != m_checker.end()) {
    auto iter2 = std::find_if(
        iter->second.begin(), iter->second.end(),
        [tradeType, tokenName](auto const &a) {
          return a.tokenName.compare(tokenName, Qt::CaseInsensitive) == 0 &&
                 tradeType == a.tradeType;
        });
    if (iter2 != iter->second.end())
      return;
    iter->second.push_back({tradeType, tokenName});
  } else {
    m_checker[exchange].push_back({tradeType, tokenName});
  }

  auto callback = [this](QString const &tokenName, double const price,
                         exchange_name_e const exchange,
                         trade_type_e const tradeType) {
    emit onNewPriceAvailable(tokenName, price, exchange, tradeType);
  };

  if (exchange == exchange_name_e::binance) {
    auto sock = new detail::custom_socket(
        *m_ioContext, m_sslContext, tradeType);
    sock->addSubscription(tokenName.toLower());
    QObject::connect(sock, &detail::custom_socket::onNewPriceAvailable,
                     this, callback);
    m_sockets.push_back(std::move(sock));
  } else if (exchange == exchange_name_e::kucoin) {
    auto sock = new kc_websocket(*m_ioContext, m_sslContext, tradeType);
    QObject::connect(sock, &kc_websocket::onNewPriceAvailable, this,
                     callback);
    sock->addSubscription(tokenName.toUpper());
    m_sockets.push_back(std::move(sock));
  }
}

void cwebsocket::startWatch() {
  m_checker.clear();

  for (auto &sock : m_sockets) {
    std::visit([this](auto && v) mutable {
      std::thread([this, &v]() mutable {
        v->startFetching();
        m_ioContext->run();
      }).detach();
    }, sock);
  }
}

} // namespace korrelator
