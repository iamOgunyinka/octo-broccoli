#include "ftx_websocket.hpp"

#include <QDebug>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace korrelator {
ftx_websocket::ftx_websocket(net::io_context &ioContext,
                             net::ssl::context &sslContext,
                             double &priceResult, trade_type_e const tt)
    : m_ioContext(ioContext), m_sslContext(sslContext),
      m_priceResult(priceResult), m_isSpot(tt == trade_type_e::spot)
{
}

ftx_websocket::~ftx_websocket() { m_webStream.reset(); }

void ftx_websocket::startFetching() {
  if (m_requestedToStop)
    return;

  m_step = step_e::unsubscribed;
  m_resolver.emplace(m_ioContext);
  m_resolver->async_resolve(
      "ftx.com", "443",
      [this](auto const error_code, results_type const &results) {
        if (error_code) {
          qDebug() << error_code.message().c_str();
          return;
        }
        websockConnectToResolvedNames(results);
      });
}

void ftx_websocket::websockConnectToResolvedNames(
    resolver_result_type const &resolvedNames) {
  m_resolver.reset();
  m_webStream.emplace(m_ioContext, m_sslContext);
  beast::get_lowest_layer(*m_webStream).expires_after(std::chrono::seconds(30));
  beast::get_lowest_layer(*m_webStream)
      .async_connect(
          resolvedNames,
          [this](auto const &errorCode,
                 resolver_result_type::endpoint_type const &) {
            if (errorCode) {
              qDebug() << errorCode.message().c_str();
              return;
            }
            websockPerformSslHandshake();
          });
}

void ftx_websocket::websockPerformSslHandshake() {
  // Set a timeout on the operation
  beast::get_lowest_layer(*m_webStream).expires_after(std::chrono::seconds(30));

  // Set SNI Hostname (many hosts need this to handshake successfully)
  if (!SSL_set_tlsext_host_name(m_webStream->next_layer().native_handle(),
                                "ftx.com")) {
    auto const ec = beast::error_code(static_cast<int>(::ERR_get_error()),
                                      net::error::get_ssl_category());
    qDebug() << ec.message().c_str();
    return;
  }
  negotiateWebsocketConnection();
}

void ftx_websocket::negotiateWebsocketConnection() {
  m_webStream->next_layer().async_handshake(
      net::ssl::stream_base::client, [this](beast::error_code const ec) {
        if (ec) {
          qDebug() << ec.message().c_str();
          return;
        }
        beast::get_lowest_layer(*m_webStream).expires_never();
        performWebsocketHandshake();
      });
}

void ftx_websocket::performWebsocketHandshake() {
  auto const path = "/ws/";
  m_webStream->control_callback([this](auto const frame_type, auto const &) {
    if (frame_type == beast::websocket::frame_type::close) {
      m_webStream.reset();
      return startFetching();
    }
  });

  m_webStream->async_handshake("ftx.com", path,
                               [this](beast::error_code const ec) {
                                 if (ec) {
                                   qDebug() << ec.message().c_str();
                                   return;
                                 }

                                 performSubscriptionToChannel();
                               });
}

void ftx_websocket::waitForMessages() {
  m_readBuffer.emplace();
  m_webStream->async_read(
        *m_readBuffer,
        [this](beast::error_code const error_code,
        std::size_t const) {
    if (error_code == net::error::operation_aborted) {
      qDebug() << error_code.message().c_str();
      return;
    } else if (error_code) {
      qDebug() << error_code.message().c_str();
      m_webStream.reset();
      return startFetching();
    }
    interpretGenericMessages();
  });
}

void ftx_websocket::interpretGenericMessages() {
  switch(m_step){
  case step_e::unsubscribed:
    return performSubscriptionToChannel();
  case step_e::subscribed:
    return readSubscriptionResponse();
  case step_e::ticker_data:
  default:
    return readTickerResponse();
  }
}

#ifdef _MSC_VER
#undef GetObject
#endif

void ftx_websocket::readTickerResponse() {
  char const *bufferCstr =
      static_cast<char const *>(m_readBuffer->cdata().data());
  size_t const length = m_readBuffer->size();
  rapidjson::Document doc;
  try {
    doc.Parse(bufferCstr, length);
    auto const jsonObject = doc.GetObject();
    auto dataIter = jsonObject.FindMember("data");
    if (dataIter == jsonObject.MemberEnd())
      return waitForMessages();
    auto const dataObject = dataIter->value.GetObject();
    if (m_isSpot) {
      m_priceResult = (dataObject.FindMember("last")->value.GetDouble());
    } else {
      m_priceResult = (dataObject.FindMember("ask")->value.GetDouble() +
                       dataObject.FindMember("bid")->value.GetDouble() ) / 2.0;
    }
    qDebug() << m_priceResult;
  } catch(...) {

  }

  waitForMessages();
}

void ftx_websocket::readSubscriptionResponse() {
  char const *bufferCstr =
      static_cast<char const *>(m_readBuffer->cdata().data());

  qDebug() << bufferCstr;

  m_step = step_e::ticker_data;
  return waitForMessages();
}

void ftx_websocket::performSubscriptionToChannel() {
  rapidjson::StringBuffer s;
  rapidjson::Writer<rapidjson::StringBuffer> writer(s);
  writer.StartObject(); // {

  writer.Key("op");
  writer.String("subscribe");

  writer.Key("channel");
  writer.String("ticker");

  writer.Key("market");
  writer.String(m_tokenInfo.tokenName.toUpper().toStdString().c_str());

  writer.EndObject(); // }
  m_writerBuffer = s.GetString();

  qDebug() << m_writerBuffer.c_str();
  m_webStream->async_write(
        net::buffer(m_writerBuffer),
        [this](auto const errorCode, size_t const)
  {
    if (errorCode) {
      qDebug() << errorCode.message().c_str();
      return;
    }
    m_writerBuffer.clear();
    m_step = step_e::subscribed;
    waitForMessages();
  });
}


} // namespace korrelator
