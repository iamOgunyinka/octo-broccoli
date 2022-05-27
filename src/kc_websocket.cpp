#include "kc_websocket.hpp"

#include <QDebug>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <random>
#include <rapidjson/document.h>

#include "utils.hpp"

#ifdef _MSC_VER
#undef GetObject
#endif

namespace korrelator {

static char const *const kc_spot_api_url = "api.kucoin.com";
static char const *const kc_spot_http_request =
    "POST /api/v1/bullet-public HTTP/1.1\r\n"
    "Host: api.kucoin.com\r\n"
    "Accept: */*\r\n"
    "Content-Type: application/json\r\n"
    "User-Agent: postman\r\n\r\n";

static size_t const spot_http_request_len = strlen(kc_spot_http_request);
static char const *const kc_futures_api_url = "api-futures.kucoin.com";
static char const *const kc_futures_http_request =
    "POST /api/v1/bullet-public HTTP/1.1\r\n"
    "Host: api-futures.kucoin.com\r\n"
    "Accept: */*\r\n"
    "Content-Type: application/json\r\n"
    "User-Agent: postman\r\n\r\n";
static size_t const futures_http_request_len = strlen(kc_futures_http_request);

double kuCoinGetCoinPrice(char const *str, size_t const size,
                          bool const isSpot) {
  rapidjson::Document d;
  d.Parse(str, size);

  auto const jsonObject = d.GetObject();
  auto iter = jsonObject.FindMember("data");
  if (iter == jsonObject.end())
    return NAN;
  auto const dataObject = iter->value.GetObject();
  if (isSpot) {
    auto const priceIter = dataObject.FindMember("price");
    if (priceIter == dataObject.MemberEnd() ||
        (priceIter->value.GetType() != rapidjson::Type::kStringType)) {
      assert(false);
      return NAN;
    }
    return std::stod(priceIter->value.GetString());
  } else {
    auto const bestBidIter = dataObject.FindMember("bestBidPrice");
    auto const bestAskIter = dataObject.FindMember("bestAskPrice");
    if (bestBidIter == dataObject.MemberEnd() ||
        bestAskIter == dataObject.MemberEnd() ||
        bestBidIter->value.GetType() != rapidjson::Type::kStringType ||
        bestAskIter->value.GetType() != rapidjson::Type::kStringType) {
      assert(false);
      return NAN;
    }
    auto const bidPrice = std::stod(bestBidIter->value.GetString());
    auto const askPrice = std::stod(bestAskIter->value.GetString());
    return (bidPrice + askPrice) / 2.0;
  }
  return NAN;
}

kucoin_ws::kucoin_ws(net::io_context &ioContext, ssl::context &sslContext,
                     trade_type_e const tradeType)
    : m_ioContext(ioContext)
    , m_sslContext(sslContext)
    , m_tradeType(tradeType)
    , m_isSpotTrade(tradeType == trade_type_e::spot)
{
}

kucoin_ws::~kucoin_ws() {
  m_resolver.reset();
  m_sslWebStream.reset();
  resetBuffer();
  m_response.reset();
  m_instanceServers.clear();
  m_websocketToken.clear();
  m_subscriptionString.clear();
}

void kucoin_ws::restApiInitiateConnection() {

  if (m_requestedToStop)
    return;

  m_websocketToken.clear();
  m_tokensSubscribedFor = false;

  auto const url = m_isSpotTrade ? kc_spot_api_url : kc_futures_api_url;
  m_resolver.emplace(m_ioContext);
  m_resolver->async_resolve(
      url, "https",
      [this](auto const errorCode, resolver::results_type const &results) {
        if (errorCode) {
          qDebug() << errorCode.message().c_str();
          return;
        }
        restApiConnectToResolvedNames(results);
      });
}

void kucoin_ws::restApiConnectToResolvedNames(
    results_type const &resolvedNames) {
  m_resolver.reset();
  m_sslWebStream.emplace(m_ioContext, m_sslContext);
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(30));
  beast::get_lowest_layer(*m_sslWebStream)
      .async_connect(resolvedNames,
                     [this](auto const errorCode,
                            resolver::results_type::endpoint_type const &) {
                       if (errorCode) {
                         qDebug() << errorCode.message().c_str();
                         return;
                       }
                       restApiPerformSSLHandshake();
                     });
}

void kucoin_ws::restApiPerformSSLHandshake() {
  auto const url = m_isSpotTrade ? kc_spot_api_url : kc_futures_api_url;

  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(15));
  if (!SSL_set_tlsext_host_name(m_sslWebStream->next_layer().native_handle(),
                                url)) {
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

void kucoin_ws::restApiSendRequest() {
  char const *const request =
      m_isSpotTrade ? kc_spot_http_request : kc_futures_http_request;
  size_t const request_len =
      m_isSpotTrade ? spot_http_request_len : futures_http_request_len;
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(10));
  m_sslWebStream->next_layer().async_write_some(
      net::const_buffer(request, request_len),
      [this](auto const errorCode, auto const) {
        if (errorCode) {
          qDebug() << errorCode.message().c_str();
          return;
        }

        restApiReceiveResponse();
      });
}

void kucoin_ws::restApiReceiveResponse() {
  resetBuffer();
  m_response.emplace();
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(20));
  beast::http::async_read(m_sslWebStream->next_layer(), m_readWriteBuffer,
                          *m_response,
                          [this](auto const errorCode, auto const) {
                            if (errorCode) {
                              qDebug() << errorCode.message().c_str();
                              return;
                            }

                            restApiInterpretHttpResponse();
                          });
}

void kucoin_ws::restApiInterpretHttpResponse() {
  rapidjson::Document d;
  auto const &response = m_response->body();
  d.Parse(response.c_str(), response.length());

  try {
    auto const &jsonObject = d.GetObject();
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
    auto const &dataObject = dataIter->value.GetObject();
    m_websocketToken = dataObject.FindMember("token")->value.GetString();

    auto const serverInstances =
        dataObject.FindMember("instanceServers")->value.GetArray();
    m_instanceServers.clear();
    m_instanceServers.reserve(serverInstances.Size());

    for (auto const &instanceJson : serverInstances) {
      auto const &instanceObject = instanceJson.GetObject();
      if (auto iter = instanceObject.FindMember("protocol");
          iter != instanceObject.MemberEnd() &&
          iter->value.GetString() == std::string("websocket")) {
        instance_server_data_t data;
        data.endpoint =
            instanceObject.FindMember("endpoint")->value.GetString();
        data.encryptProtocol =
            (int)instanceObject.FindMember("encrypt")->value.GetBool();
        data.pingIntervalMs =
            instanceObject.FindMember("pingInterval")->value.GetInt();
        data.pingTimeoutMs =
            instanceObject.FindMember("pingTimeout")->value.GetInt();
        m_instanceServers.push_back(std::move(data));
      }
    }
  } catch (std::exception const &e) {
    qDebug() << e.what();
    return;
  }

  m_response.reset();
  resetBuffer();

  if (!m_instanceServers.empty() && !m_websocketToken.empty())
    initiateWebsocketConnection();
}

void kucoin_ws::resetBuffer() {
  m_readWriteBuffer.consume(m_readWriteBuffer.size());
  m_readWriteBuffer.clear();
}

void kucoin_ws::addSubscription(QString const &tokenName) {
  if (m_tokenList.isEmpty())
    m_tokenList = tokenName;
  else
    m_tokenList += ("," + tokenName);
}

void kucoin_ws::initiateWebsocketConnection() {
  if (m_instanceServers.empty() || m_websocketToken.empty())
    return;

  // remove all server instances that do not support HTTPS
  m_instanceServers.erase(std::remove_if(m_instanceServers.begin(),
                                         m_instanceServers.end(),
                                         [](instance_server_data_t const &d) {
                                           return d.encryptProtocol == 0;
                                         }),
                          m_instanceServers.end());

  if (m_instanceServers.empty()) {
    qDebug() << "No server instance found that supports encryption";
    return;
  }

  m_uri = korrelator::uri(m_instanceServers.back().endpoint);
  auto const service = m_uri.protocol() != "wss" ? m_uri.protocol() : "443";
  m_resolver.emplace(m_ioContext);
  m_resolver->async_resolve(
      m_uri.host(), service,
      [this](auto const &errorCode, resolver::results_type const &results) {
        if (errorCode) {
          qDebug() << errorCode.message().c_str();
          return;
        }
        websockConnectToResolvedNames(results);
      });
}

void kucoin_ws::websockConnectToResolvedNames(
    resolver::results_type const &resolvedNames) {
  m_resolver.reset();
  m_sslWebStream.emplace(m_ioContext, m_sslContext);
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(30));
  beast::get_lowest_layer(*m_sslWebStream)
      .async_connect(resolvedNames,
                     [this](auto const errorCode,
                            resolver::results_type::endpoint_type const &ep) {
                       if (errorCode) {
                         qDebug() << errorCode.message().c_str();
                         return;
                       }
                       websockPerformSSLHandshake(ep);
                     });
}

void kucoin_ws::websockPerformSSLHandshake(
    resolver::results_type::endpoint_type const &ep) {
  auto const host = m_uri.host() + ":" + std::to_string(ep.port());
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(10));
  if (!SSL_set_tlsext_host_name(m_sslWebStream->next_layer().native_handle(),
                                host.c_str())) {
    auto const errorCode = beast::error_code(
        static_cast<int>(::ERR_get_error()), net::error::get_ssl_category());
    qDebug() << errorCode.message().c_str();
    return;
  }

  negotiateWebsocketConnection();
}

void kucoin_ws::negotiateWebsocketConnection() {
  m_sslWebStream->next_layer().async_handshake(
      ssl::stream_base::client, [this](beast::error_code const &ec) {
        if (ec) {
          qDebug() << ec.message().c_str();
          return;
        }
        beast::get_lowest_layer(*m_sslWebStream).expires_never();
        performWebsocketHandshake();
      });
}

void kucoin_ws::performWebsocketHandshake() {
  auto const &instanceData = m_instanceServers.back();
  auto opt = ws::stream_base::timeout();
  opt.idle_timeout = std::chrono::milliseconds(instanceData.pingTimeoutMs);
  opt.handshake_timeout =
      std::chrono::milliseconds(instanceData.pingIntervalMs);
  opt.keep_alive_pings = true;
  m_sslWebStream->set_option(opt);

  m_sslWebStream->control_callback(
        [this](beast::websocket::frame_type const frameType,
               boost::string_view const &payload) {
    if (frameType == ws::frame_type::close) {
      m_sslWebStream.reset();
      return restApiInitiateConnection();
    } else if (frameType == ws::frame_type::pong) {
      qDebug() << "pong" << QString::fromStdString(payload.to_string());
    } else if (frameType == ws::frame_type::ping) {
      qDebug() << "ping" << QString::fromStdString(payload.to_string());
    }
  });

  auto const path = m_uri.path() + "?token=" + m_websocketToken +
                    "&connectId=" + get_random_string(10);
  m_sslWebStream->async_handshake(m_uri.host(), path,
                                  [this](auto const errorCode) {
                                    if (errorCode) {
                                      qDebug() << errorCode.message().c_str();
                                      return;
                                    }
                                    waitForMessages();
                                  });
}

void kucoin_ws::waitForMessages() {
  resetBuffer();
  m_sslWebStream->async_read(
      m_readWriteBuffer,
      [this](beast::error_code const errorCode, std::size_t const) {
        if (errorCode == net::error::operation_aborted) {
          qDebug() << errorCode.message().c_str();
          return;
        } else if (errorCode) {
          qDebug() << errorCode.message().c_str();
          m_sslWebStream.reset();
          return restApiInitiateConnection();
        }
        interpretGenericMessages();
      });
}

void kucoin_ws::interpretGenericMessages() {
  if (m_requestedToStop)
    return;

  char const *bufferCstr =
      static_cast<char const *>(m_readWriteBuffer.cdata().data());
  size_t const dataLength = m_readWriteBuffer.size();
  auto const optPrice =
      kuCoinGetCoinPrice(bufferCstr, dataLength, m_isSpotTrade);
  if (!isnan(optPrice))
    emit onNewPriceAvailable(m_tokenList, optPrice,
                             exchange_name_e::kucoin,
                             m_tradeType);
  if (!m_tokensSubscribedFor)
    return makeSubscription();
  return waitForMessages();
}

void kucoin_ws::makeSubscription() {
  static char const *const subscriptionFormat = R"({
    "id": %1,
    "type": "subscribe",
    "topic": "/%2/ticker:%3",
    "response": false
  })";

  if (m_subscriptionString.empty()) {
    m_subscriptionString =
        QString(subscriptionFormat)
            .arg(get_random_integer())
            .arg((m_isSpotTrade ? "market" : "contractMarket"),
                 m_tokenList)
            .toStdString();
    qDebug() << m_subscriptionString.c_str();
  }

  m_sslWebStream->async_write(net::buffer(m_subscriptionString),
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

} // namespace korrelator
