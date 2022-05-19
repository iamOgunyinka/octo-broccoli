#include "websocket_base.hpp"
#include <QDebug>

#include <rapidjson/document.h>
#include <random>

#ifdef _MSC_VER
#undef GetObject
#endif

namespace korrelator {
static char const* const kucoin_spot_api_url = "api.kucoin.com";
static char const* const kucoin_spot_http_request =
    "POST /api/v1/bullet-public HTTP/1.1\r\n"
    "Host: api.kucoin.com\r\n"
    "Accept: */*\r\n"
    "Content-Type: application/json\r\n"
    "User-Agent: postman\r\n\r\n";
static size_t const http_request_len = strlen(kucoin_spot_http_request);
static char const * const data_topic = "/market/ticker:";
static size_t const data_topic_len = strlen(data_topic);

std::optional<std::pair<QString, double>> kuCoinGetCoinPrice(
    char const* str, size_t const size) {
  rapidjson::Document d;
  d.Parse(str, size);

  try {
    auto const jsonObject = d.GetObject();
    auto const topicIter = jsonObject.FindMember("topic");
    if (topicIter == jsonObject.MemberEnd() || !topicIter->value.IsString())
      return std::nullopt;

    QString const topic = topicIter->value.GetString();
    int const indexOfTopic = topic.indexOf(data_topic);
    if (indexOfTopic == -1)
      return std::nullopt;

    auto const tokenName = topic.mid(data_topic_len);

    auto iter = jsonObject.FindMember("data");
    if (iter == jsonObject.end())
      return std::nullopt;
    auto const dataObject = iter->value.GetObject();
    auto const priceIter = dataObject.FindMember("price");
    if (priceIter == dataObject.MemberEnd() || !priceIter->value.IsString()) {
      Q_ASSERT(false);
      return std::nullopt;
    }

    double const price = std::stod(priceIter->value.GetString());
    return std::make_pair(tokenName, price);
  } catch(std::exception const & e) {
    return std::nullopt;
  }
  return std::nullopt;
}

kc_websocket::kc_websocket(net::io_context& ioContext,
                           ssl::context& sslContext)
  : m_ioContext(ioContext)
  , m_sslContext(sslContext)
{
}

kc_websocket::~kc_websocket() {
  m_resolver.reset();
  m_sslWebStream.reset();
  m_readWriteBuffer.reset();
  m_response.reset();
  m_instanceServers.clear();
  m_websocketToken.clear();
  m_subscriptionString.clear();
  qDebug() << "KuCoin destroyed";
}

void kc_websocket::restApiInitiateConnection() {

  if (m_requestedToStop)
    return;

  m_websocketToken.clear();
  m_tokensSubscribedFor = false;

  m_resolver.emplace(m_ioContext);
  m_resolver->async_resolve(
        kucoin_spot_api_url, "https", [this](auto const errorCode,
        resolver::results_type const& results) {
    if (errorCode) {
      qDebug() << errorCode.message().c_str();
      return;
    }
    restApiConnectToResolvedNames(results);
  });
}

void kc_websocket::restApiConnectToResolvedNames(
    results_type const& resolvedNames)
{
  m_resolver.reset();
  m_sslWebStream.emplace(m_ioContext, m_sslContext);
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(30));
  beast::get_lowest_layer(*m_sslWebStream).async_connect(
        resolvedNames,
        [this](auto const errorCode,
        resolver::results_type::endpoint_type const&) {
    if (errorCode) {
      qDebug() << errorCode.message().c_str();
      return;
    }
    restApiPerformSSLHandshake();
  });
}

void kc_websocket::restApiPerformSSLHandshake() {
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(15));
  if (!SSL_set_tlsext_host_name(m_sslWebStream->next_layer().native_handle(),
                                kucoin_spot_api_url)) {
    auto const ec = beast::error_code(static_cast<int>(::ERR_get_error()),
                                      net::error::get_ssl_category());
    qDebug() << ec.message().c_str();
    return;
  }

  m_sslWebStream->next_layer().async_handshake(
        ssl::stream_base::client, [this](beast::error_code const ec) {
    if (ec) {
      qDebug() << ec.message().c_str();
      return;
    }
    return restApiSendRequest();
  });
}

void kc_websocket::restApiSendRequest() {
  beast::get_lowest_layer(*m_sslWebStream).expires_after(std::chrono::seconds(10));
  m_sslWebStream->next_layer().async_write_some(
        net::const_buffer(kucoin_spot_http_request, http_request_len),
        [this](auto const errorCode, auto const sizeWritten) {
    if (errorCode) {
      qDebug() << errorCode.message().c_str();
      return;
    }
    qDebug() << "Size written:" << sizeWritten;
    restApiReceiveResponse();
  });
}

void kc_websocket::restApiReceiveResponse() {
  m_readWriteBuffer.emplace();
  m_response.emplace();
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(20));
  beast::http::async_read(m_sslWebStream->next_layer(), *m_readWriteBuffer,
                          *m_response,
                          [this](auto const errorCode, auto const sz)
  {
    if (errorCode) {
      qDebug() << errorCode.message().c_str();
      return;
    }
    qDebug() << "SizeReceived:" << sz;
    restApiInterpretHttpResponse();
  });
}

void kc_websocket::restApiInterpretHttpResponse() {
  rapidjson::Document d;
  auto const& response = m_response->body();
  d.Parse(response.c_str(), response.length());

  try {
    auto const& jsonObject = d.GetObject();
    auto const responseCodeIter = jsonObject.FindMember("code");
    if (responseCodeIter == jsonObject.MemberEnd() ||
        responseCodeIter->value.GetString() != std::string("200000")) {
      qDebug() << response.c_str();
      return;
    }
    auto const dataIter = jsonObject.FindMember("data");
    if (dataIter == jsonObject.MemberEnd() || !dataIter->value.IsObject()) {
      qDebug() << "Could not find 'data'" << response.c_str();
      return;
    }
    auto const& dataObject = dataIter->value.GetObject();
    m_websocketToken = dataObject.FindMember("token")->value.GetString();

    auto const serverInstances = dataObject.FindMember("instanceServers")
        ->value.GetArray();
    m_instanceServers.clear();
    m_instanceServers.reserve(serverInstances.Size());

    for (auto const& instanceJson : serverInstances) {
      auto const& instanceObject = instanceJson.GetObject();
      if (auto iter = instanceObject.FindMember("protocol");
          iter != instanceObject.MemberEnd() &&
          iter->value.GetString() == std::string("websocket"))
      {
        instance_server_data_t data;
        data.endpoint = instanceObject.FindMember("endpoint")->value
            .GetString();
        data.encryptProtocol = (int)instanceObject.FindMember("encrypt")->
            value.GetBool();
        data.pingIntervalMs = instanceObject.FindMember("pingInterval")->
            value.GetInt();
        data.pingTimeoutMs = instanceObject.FindMember("pingTimeout")->
            value.GetInt();
        m_instanceServers.push_back(std::move(data));
      }
    }
  }
  catch (std::exception const& e) {
    qDebug() << e.what();
    return;
  }

  m_response.reset();
  m_readWriteBuffer.reset();

  if (!m_instanceServers.empty() && !m_websocketToken.empty())
    initiateWebsocketConnection();
}

void kc_websocket::addSubscription(std::string const &tokenName) {
  if (m_tokenList.empty())
    m_tokenList = tokenName;
  else
    m_tokenList += ("," + tokenName);
}

void kc_websocket::initiateWebsocketConnection() {
  if (m_instanceServers.empty() || m_websocketToken.empty())
    return;

  // remove all server instances that do not support HTTPS
  m_instanceServers.erase(
        std::remove_if(m_instanceServers.begin(), m_instanceServers.end(),
                       [](instance_server_data_t const& d)
        {
          return d.encryptProtocol == 0;
        }), m_instanceServers.end());

  if (m_instanceServers.empty()) {
    qDebug() << "No server instance found that supports encryption";
    return;
  }

  m_uri = korrelator::uri(m_instanceServers.back().endpoint);
  auto const service = m_uri.protocol() != "wss" ? m_uri.protocol(): "443";
  m_resolver.emplace(m_ioContext);
  m_resolver->async_resolve(m_uri.host(), service,
                            [this](auto const& errorCode,
                            resolver::results_type const& results)
  {
    if (errorCode) {
      qDebug() << errorCode.message().c_str();
      return;
    }
    websockConnectToResolvedNames(results);
  });
}

void kc_websocket::websockConnectToResolvedNames(
    resolver::results_type const& resolvedNames)
{
  m_resolver.reset();
  m_sslWebStream.emplace(m_ioContext, m_sslContext);
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(30));
  beast::get_lowest_layer(*m_sslWebStream)
      .async_connect(resolvedNames,
                     [this](auto const errorCode,
                     resolver::results_type::endpoint_type const& ep)
  {
    if (errorCode) {
      qDebug() << errorCode.message().c_str();
      return;
    }
    websockPerformSSLHandshake(ep);
  });
}

void kc_websocket::websockPerformSSLHandshake(
    resolver::results_type::endpoint_type const& ep)
{
  auto const host = m_uri.host() + ":" + std::to_string(ep.port());
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(10));
  if (!SSL_set_tlsext_host_name(m_sslWebStream->next_layer().native_handle(),
                                host.c_str()))
  {
    auto const errorCode = beast::error_code(
          static_cast<int>(::ERR_get_error()), net::error::get_ssl_category());
    qDebug() << errorCode.message().c_str();
    return;
  }

  negotiateWebsocketConnection();
}

void kc_websocket::negotiateWebsocketConnection() {
  m_sslWebStream->next_layer().async_handshake(
        ssl::stream_base::client, [this](beast::error_code const& ec)
  {
    if (ec) {
      qDebug() << ec.message().c_str();
      return;
    }
    beast::get_lowest_layer(*m_sslWebStream).expires_never();
    performWebsocketHandshake();
  });
}

void kc_websocket::performWebsocketHandshake() {
  auto const& instanceData = m_instanceServers.back();
  auto opt = ws::stream_base::timeout();
  opt.idle_timeout = std::chrono::milliseconds(instanceData.pingTimeoutMs);
  opt.handshake_timeout = std::chrono::milliseconds(instanceData.pingIntervalMs);
  opt.keep_alive_pings = true;
  m_sslWebStream->set_option(opt);

  m_sslWebStream->control_callback([this](auto const frameType, auto const&)
  {
    if (frameType == ws::frame_type::close) {
      m_sslWebStream.reset();
      return restApiInitiateConnection();
    }
    else if (frameType == ws::frame_type::ping
             || frameType == ws::frame_type::pong) {
      qDebug() << (((int)frameType) == 1 ? "ping" : "pong");
    }
  });

  auto const path = m_uri.path() + "?token=" + m_websocketToken +
      "&[connectId=" + get_random_string(10) + "]";
  qDebug() << "Websocket path:" << path.c_str();

  m_sslWebStream->async_handshake(
        m_uri.host(), path, [this](auto const errorCode)
  {
    if (errorCode) {
      qDebug() << errorCode.message().c_str();
      return;
    }
    waitForMessages();
  });
}

void kc_websocket::waitForMessages() {
  m_readWriteBuffer.emplace();
  m_sslWebStream->async_read(
        *m_readWriteBuffer,
        [this](beast::error_code const errorCode, std::size_t const)
  {
    if (errorCode == net::error::operation_aborted) {
      qDebug() << errorCode.message().c_str();
      return;
    }
    else if (errorCode) {
      qDebug() << errorCode.message().c_str();
      m_sslWebStream.reset();
      return restApiInitiateConnection();
    }
    interpretGenericMessages();
  });
}

void kc_websocket::interpretGenericMessages() {
  if (m_requestedToStop)
    return;

  char const* bufferCstr = static_cast<char const*>(
        m_readWriteBuffer->cdata().data());
  size_t const dataLength = m_readWriteBuffer->size();
  auto const optMessage = kuCoinGetCoinPrice(bufferCstr, dataLength);
  if (optMessage) {
    qDebug() << optMessage->first << optMessage->second;
  }

  if (!m_tokensSubscribedFor)
    return makeSubscription();
  return waitForMessages();
}

void kc_websocket::makeSubscription() {
  if (m_subscriptionString.empty()) {
    m_subscriptionString = QString(R"(
    {
      "id": %1,
      "type": "subscribe",
      "topic": "/market/ticker:%2",
      "response": false
    })").arg(get_random_integer()).arg(m_tokenList.c_str()).toStdString();
    m_tokenList.clear();
  }

  m_sslWebStream->async_write(
        net::buffer(m_subscriptionString),
        [this](auto const errCode, size_t const) {
    if (errCode) {
      qDebug() << errCode.message().c_str();
      return;
    }
    m_tokensSubscribedFor = true;
    waitForMessages();
  });
}

std::size_t get_random_integer() {
  static std::random_device rd{};
  static std::mt19937 gen{rd()};
  static std::uniform_int_distribution<> uid(1, 100);
  return uid(gen);
}

char get_random_char() {
  static std::random_device rd{};
  static std::mt19937 gen{rd()};
  static std::uniform_int_distribution<> uid(0, 52);
  static char const *allAlphas =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
  return allAlphas[uid(gen)];
}

std::string get_random_string(std::size_t const length) {
  std::string result{};
  result.reserve(length);
  for (std::size_t i = 0; i != length; ++i) {
    result.push_back(get_random_char());
  }
  return result;
}

}
