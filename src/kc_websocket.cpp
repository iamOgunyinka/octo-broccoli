#include "kc_websocket.hpp"

#include <QDebug>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <random>
#include <rapidjson/document.h>

#include "constants.hpp"
#include "utils.hpp"

#ifdef _MSC_VER
#undef GetObject
#endif

namespace korrelator {

double kuCoinGetCoinPrice(char const *str, size_t const size,
                          bool const isSpot) {
  rapidjson::Document d;
  d.Parse(str, size);

  auto const jsonObject = d.GetObject();
  auto iter = jsonObject.FindMember("data");
  if (iter == jsonObject.end())
    return -1.0;
  auto const dataObject = iter->value.GetObject();
  if (isSpot) {
    auto const priceIter = dataObject.FindMember("price");
    if (priceIter == dataObject.MemberEnd() ||
        (priceIter->value.GetType() != rapidjson::Type::kStringType)) {
      assert(false);
      return -1.0;
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
      return -1.0;
    }
    auto const bidPrice = std::stod(bestBidIter->value.GetString());
    auto const askPrice = std::stod(bestAskIter->value.GetString());
    return (bidPrice + askPrice) / 2.0;
  }
  return -1.0;
}

kucoin_ws::kucoin_ws(net::io_context &ioContext, ssl::context &sslContext,
                     double &priceResult, trade_type_e const tradeType)
    : m_ioContext(ioContext), m_sslContext(sslContext),
      m_priceResult(priceResult), m_tradeType(tradeType),
      m_isSpotTrade(tradeType == trade_type_e::spot) {}

kucoin_ws::~kucoin_ws() {
  resetPingTimer();
  m_resolver.reset();
  m_sslWebStream.reset();
  m_readWriteBuffer.reset();
  m_response.reset();
  m_instanceServers.clear();
  m_websocketToken.clear();
  m_subscriptionString.clear();
}

void kucoin_ws::restApiInitiateConnection() {

  if (m_requestedToStop)
    return;

  resetPingTimer();
  m_websocketToken.clear();
  m_tokensSubscribedFor = false;

  auto const url = m_isSpotTrade ? constants::kucoin_https_spot_host
                                 : constants::kc_futures_api_host;
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
                            resolver::results_type::endpoint_type const &ep) {
                       if (errorCode) {
                         qDebug() << errorCode.message().c_str();
                         return;
                       }
                       restApiPerformSSLHandshake(ep.port());
                     });
}

void kucoin_ws::restApiPerformSSLHandshake(int const) {
  std::string const url = (m_isSpotTrade ? constants::kucoin_https_spot_host
                                         : constants::kc_futures_api_host);
      // + std::string(":") + std::to_string(port);

  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(15));
  if (!SSL_set_tlsext_host_name(m_sslWebStream->next_layer().native_handle(),
                                url.c_str())) {
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
  char const *const request = m_isSpotTrade
                                  ? constants::kc_spot_http_request
                                  : constants::kc_futures_http_request;
  size_t const request_len = m_isSpotTrade
                                 ? constants::spot_http_request_len
                                 : constants::futures_http_request_len;
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
  m_readWriteBuffer.emplace();
  m_response.emplace();
  beast::get_lowest_layer(*m_sslWebStream)
      .expires_after(std::chrono::seconds(20));
  beast::http::async_read(m_sslWebStream->next_layer(), *m_readWriteBuffer,
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

  if (!m_instanceServers.empty() && !m_websocketToken.empty()) {
    /*for (auto const &serverInstance: m_instanceServers)
      qDebug() << serverInstance.endpoint.c_str()
               << m_websocketToken.c_str(); */
    initiateWebsocketConnection();
  }
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
                            resolver::results_type::endpoint_type const &) {
                       if (errorCode) {
                         qDebug() << errorCode.message().c_str();
                         return;
                       }
                       websockPerformSSLHandshake();
                     });
}

void kucoin_ws::websockPerformSSLHandshake() {
  auto const host = m_uri.host();
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

          if (ec.category() == net::error::get_ssl_category()) {
            qDebug() << "SSL Category error";
          }
          qDebug() << ec.message().c_str();
          return;
        }
        beast::get_lowest_layer(*m_sslWebStream).expires_never();
        performWebsocketHandshake();
      });
}

void kucoin_ws::performWebsocketHandshake() {
  /*
  auto const &instanceData = m_instanceServers.back();
  auto opt = ws::stream_base::timeout();
  opt.idle_timeout = std::chrono::milliseconds(instanceData.pingTimeoutMs);
  opt.handshake_timeout =
      std::chrono::milliseconds(instanceData.pingIntervalMs);
  opt.keep_alive_pings = true;
  m_sslWebStream->set_option(opt);

  m_sslWebStream->control_callback(
      [this](beast::websocket::frame_type const frameType,
             boost::string_view const &) {
        if (frameType == ws::frame_type::close) {
          m_sslWebStream.reset();
          return restApiInitiateConnection();
        }
      });
  */
  auto const path = m_uri.path() + "?token=" + m_websocketToken +
                    "&connectId=" + get_random_string(10);
  m_sslWebStream->async_handshake(m_uri.host(), path,
                                  [this](auto const errorCode) {
                                    if (errorCode) {
                                      qDebug() << errorCode.message().c_str();
                                      return;
                                    }
                                    startPingTimer();
                                    waitForMessages();
                                  });
}

void kucoin_ws::resetPingTimer() {
  if (m_pingTimer) {
    boost::system::error_code ec;
    m_pingTimer->cancel(ec);
    m_pingTimer.reset();
  }
}

void kucoin_ws::onPingTimerTick(boost::system::error_code const &ec) {
  if (ec) {
    qDebug() << "Error" << ec.message().c_str();
    return;
  }

  m_sslWebStream->async_ping({}, [this](boost::system::error_code const &){
    auto const pingIntervalMs = m_instanceServers.back().pingIntervalMs;
    m_pingTimer->expires_from_now(boost::posix_time::milliseconds(pingIntervalMs));
    m_pingTimer->async_wait([this](boost::system::error_code const &errCode){
      return onPingTimerTick(errCode);
    });
  });
}

void kucoin_ws::startPingTimer() {
  resetPingTimer();
  m_pingTimer.emplace(m_ioContext);

  auto const pingIntervalMs = m_instanceServers.back().pingIntervalMs;
  m_pingTimer->expires_from_now(
        boost::posix_time::milliseconds(pingIntervalMs));
  m_pingTimer->async_wait([this] (boost::system::error_code const &ec) {
    onPingTimerTick(ec);
  });
}

void kucoin_ws::waitForMessages() {
  m_readWriteBuffer.emplace();
  m_sslWebStream->async_read(
      *m_readWriteBuffer,
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
      static_cast<char const *>(m_readWriteBuffer->cdata().data());
  size_t const dataLength = m_readWriteBuffer->size();
  auto const optPrice =
      kuCoinGetCoinPrice(bufferCstr, dataLength, m_isSpotTrade);
  if (optPrice != -1.0)
    m_priceResult = optPrice;

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
            .arg((m_isSpotTrade ? "market" : "contractMarket"), m_tokenList)
            .toStdString();
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
  static std::uniform_int_distribution<> uid(1, 20);
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
