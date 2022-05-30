#include "https_request.hpp"

#include <QDebug>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

#include "constants.hpp"
#include "crypto.hpp"

namespace korrelator {
void kc_https_plug::sendHttpsData() {
  beast::get_lowest_layer(m_tcpStream)
      .expires_after(std::chrono::milliseconds(15'000));
  http::async_write(m_tcpStream, m_httpRequest,
                    [this](auto const &a, auto const &b) { onDataSent(a, b); });
}

void kc_https_plug::onDataSent(beast::error_code ec,
                               std::size_t const bytes_sent) {
  if (ec) {
    qDebug() << "Problem writing\n" << ec.message().c_str();
    return;
  }
  receiveData();
}

void kc_https_plug::receiveData() {
  beast::get_lowest_layer(m_tcpStream)
      .expires_after(std::chrono::milliseconds(15000));
  m_httpResponse.clear();
  http::async_read(
      m_tcpStream, m_readBuffer, m_httpResponse,
      [this](auto const &a, auto const &b) { onDataReceived(a, b); });
}

void kc_https_plug::doConnect() {
  createRequestData();
  using resolver = tcp::resolver;
  auto const host = m_isSpot ? constants::kucoin_https_spot_host
                             : constants::kc_futures_api_url;
  m_resolver.async_resolve(
      host, "https",
      [this](auto const &errorCode, resolver::results_type const &results) {
        if (errorCode) {
          qDebug() << errorCode.message().c_str();
          return;
        }
        onHostResolved(results);
      });
}

void kc_https_plug::startConnect() { doConnect(); }

void kc_https_plug::onHostResolved(tcp::resolver::results_type const &result) {
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

void kc_https_plug::createRequestData() {
  using http::field;

  char const *const path = m_isSpot ? "/api/v1/orders" : "/api/v2/order";
  std::string const unixEpochTime = std::to_string(std::time(nullptr) * 1'000);
  auto const stringToSign = unixEpochTime + "POST" + path;
  auto const signature =
      base64_encode(hmac256_encode(stringToSign, m_apiSecret));
  auto const host = m_isSpot ? constants::kucoin_https_spot_host
                             : constants::kc_futures_api_url;
  m_httpRequest.clear();
  m_httpRequest.method(http::verb::post);
  m_httpRequest.version(11);
  m_httpRequest.target(path);
  m_httpRequest.set(field::host, host);
  m_httpRequest.set(field::content_type, "application/json");
  m_httpRequest.set(field::user_agent, "postman");
  m_httpRequest.set(field::accept, "*/*");
  m_httpRequest.set(field::accept_language, "en-US,en;q=0.5 --compressed");
  m_httpRequest.set("KC-API-SIGN", signature);
  m_httpRequest.set("KC-API-TIMESTAMP", unixEpochTime);
  m_httpRequest.set("KC-API-KEY", m_apiKey);
  m_httpRequest.set("KC-API-PASSPHRASE", m_apiPassphrase);
  m_httpRequest.set("KC-API-KEY-VERSION", "2");

  m_httpRequest.body() = "{}";
  m_httpRequest.prepare_payload();
}

void kc_https_plug::performSSLHandshake() {
  beast::get_lowest_layer(m_tcpStream).expires_after(std::chrono::seconds(15));
  auto const host = m_isSpot ? constants::kucoin_https_spot_host
                             : constants::kc_futures_api_url;
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

kc_https_plug::kc_https_plug(net::io_context &ioContext,
                             ssl::context &sslContext,
                             trade_type_e const tradeType,
                             api_data_t const &apiData)
    : m_isSpot(tradeType == trade_type_e::spot), m_ioContext(ioContext),
      m_sslContext(sslContext),
      m_apiKey(m_isSpot ? apiData.spotApiKey.toStdString()
                        : apiData.futuresApiKey.toStdString()),
      m_apiSecret(m_isSpot ? apiData.spotApiSecret.toStdString()
                           : apiData.futuresApiSecret.toStdString()),
      m_apiPassphrase(m_isSpot ? apiData.spotApiPassphrase.toStdString()
                               : apiData.futuresApiPassphrase.toStdString()),
      m_tcpStream(net::make_strand(ioContext), sslContext),
      m_resolver(ioContext) {}

kc_https_plug::~kc_https_plug() {}

void kc_https_plug::onDataReceived(beast::error_code ec,
                                   std::size_t const bytesReceived) {
  if (ec)
    return;

  qDebug() << ec.message().c_str() << bytesReceived;
  qDebug() << m_httpResponse.body().c_str();
}

} // namespace korrelator
