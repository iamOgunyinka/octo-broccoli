#include "cwebsocket.hpp"
#include <QDebug>
#include <QThread>

#include <rapidjson/document.h>

namespace korrelator {

namespace detail {
char const * const custom_socket::futures_url =
    "fstream.binance.com";
char const * const custom_socket::spot_url =
    "stream.binance.com";
char const * const custom_socket::spot_port = "9443";
char const * const custom_socket::futures_port = "443";


custom_socket::~custom_socket() {
  m_resolver.reset();
  m_sslWebStream.reset();
  m_readBuffer.reset();
  m_writeBuffer.clear();
  qDebug() << "Destroyed";
}

void custom_socket::startConnection() {

  if (m_requestedToStop)
    return;

  m_resolver.emplace(m_ioContext);
  m_resolver->async_resolve(m_host, m_port,
        [self = shared_from_this()](auto const error_code, results_type const &results) {
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
      .async_connect(resolvedNames,
                     [self = shared_from_this()](auto const &errorCode,
                     resolver_result_type::endpoint_type const &connectedName)
  {
    if (errorCode) {
      qDebug() << errorCode.message().c_str();
      return;
    }
    self->websockPerformSslHandshake(connectedName);
  });
}

void custom_socket::websockPerformSslHandshake(
    resolver_result_type::endpoint_type const &endpoint)
{
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
        [self = shared_from_this()](beast::error_code const ec)
  {
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
      return self->startConnection();
    }
  });

  m_sslWebStream->async_handshake(
        m_host, urlPath, [self = shared_from_this()](beast::error_code const ec) {
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
        [self = shared_from_this()](beast::error_code const error_code, std::size_t const) {
         if (error_code == net::error::operation_aborted) {
           qDebug() << error_code.message().c_str();
           return;
         } else if (error_code) {
           qDebug() << error_code.message().c_str();
           self->m_sslWebStream.reset();
           return self->startConnection();
         }
         self->interpretGenericMessages();
       });
 }

void custom_socket::interpretGenericMessages() {
  if (m_requestedToStop)
    return;

  char const *buffer_cstr = static_cast<char const *>(
        m_readBuffer->cdata().data());
  auto const optMessage = binanceGetCoinPrice(buffer_cstr, m_readBuffer->size());
  if (optMessage) {
    auto& value = *optMessage;
    m_onNewPriceCallback(value.first, value.second, m_tradeType);
  }

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
    })").arg(m_tokenName.tokenName.c_str()).toStdString();

  m_sslWebStream->async_write(
        net::buffer(m_writeBuffer),
        [self = shared_from_this()](auto const errCode, size_t const)
  {
    if (errCode) {
      qDebug() << errCode.message().c_str();
      return;
    }
    self->m_writeBuffer.clear();
    self->m_tokenName.subscribed = true;
    self->waitForMessages();
  });
}

void custom_socket::requestStop() {
  m_requestedToStop = true;
}

#ifdef _MSC_VER
#undef GetObject
#endif

std::optional<std::pair<QString, double>> binanceGetCoinPrice(
    char const* str, size_t const size) {
  rapidjson::Document d;
  d.Parse(str, size);

  try {
    auto const jsonObject = d.GetObject();
    auto iter = jsonObject.FindMember("data");
    if (iter == jsonObject.end())
      return std::nullopt;
    auto const dataObject = iter->value.GetObject();
    auto const typeIter = dataObject.FindMember("e");
    if (typeIter == dataObject.MemberEnd()) {
      Q_ASSERT(false);
      return std::nullopt;
    }

    std::string const type = typeIter->value.GetString();
    bool const is24HrTicker = type.length() == 10 && type[0] == '2' &&
        type.back() == 'r';
    bool const isAggregateTrade = type.length() == 8 && type[0] == 'a' &&
        type[3] == 'T' && type.back() == 'e';

    if (is24HrTicker || isAggregateTrade) {
      char const * amountStr = isAggregateTrade ? "p" : "c";
      QString const tokenName = dataObject.FindMember("s")->value.GetString();
      auto const amount =
          std::atof(dataObject.FindMember(amountStr)->value.GetString());
      return std::make_pair(tokenName.toLower(), amount);
    }
  } catch(...) {
    return std::nullopt;
  }
  return std::nullopt;
}

std::unique_ptr<net::io_context>& getRawIOContext() {
  static std::unique_ptr<net::io_context> ioContext = nullptr;
  return ioContext;
}

net::io_context* getIOContext() {
  auto& ioContext = getRawIOContext();
  if (!ioContext) {
    ioContext = std::make_unique<net::io_context>(
    std::thread::hardware_concurrency());
  }
  return ioContext.get();
}


net::ssl::context& getSSLContext() {
  static std::unique_ptr<net::ssl::context> ssl_context = nullptr;
  if (!ssl_context) {
    ssl_context = std::make_unique<net::ssl::context>(net::ssl::context::tlsv12_client);
    ssl_context->set_default_verify_paths();
    ssl_context->set_verify_mode(boost::asio::ssl::verify_none);
  }
  return *ssl_context;
}

} // namespace detail

cwebsocket::cwebsocket(price_callback priceCallback)
  : m_sslContext(detail::getSSLContext())
  , m_onNewCallback(std::move(priceCallback))
{
  detail::getRawIOContext().reset();
  m_ioContext = detail::getIOContext();
}

cwebsocket::~cwebsocket() {
  m_ioContext->stop();

  for(auto& sock: m_sockets)
    sock->requestStop();

  m_sockets.clear();
}

void cwebsocket::addSubscription(QString const &tokenName, trade_type_e const tt)
{
  auto iter = m_checker.find(tokenName);
  if (iter != m_checker.end() && iter->second == tt)
    return;
  m_checker[tokenName] = tt;
  m_sockets.push_back(
        std::make_shared<detail::custom_socket>(
          *m_ioContext, m_sslContext, tokenName.toLower().toStdString(),
          m_onNewCallback, tt));
}

void cwebsocket::startWatch() {
  m_checker.clear();

  for (auto& sock: m_sockets) {
    std::thread([&sock, this]{
      sock->startConnection();
      m_ioContext->run();
    }).detach();
  }
}

} // namespace korrelator
