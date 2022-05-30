#pragma once

#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/empty_body.hpp>

#include <string>
#include "utils.hpp"

namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;
namespace ssl = boost::asio::ssl;

namespace korrelator
{
  using tcp = boost::asio::ip::tcp;

  class kc_https_plug {
    bool const m_isSpot;
    net::io_context &m_ioContext;
    ssl::context& m_sslContext;
    std::string const m_apiKey;
    std::string const m_apiSecret;
    std::string const m_apiPassphrase;
    beast::ssl_stream<beast::tcp_stream> m_tcpStream;
    tcp::resolver m_resolver;
    beast::flat_buffer m_readBuffer;
    http::response<http::string_body> m_httpResponse;
    http::request<http::string_body> m_httpRequest;
  private:
    void createRequestData();
    void performSSLHandshake();
    void onHandshook(beast::error_code ec );
    void onHostResolved(tcp::resolver::results_type const &);
    void sendHttpsData();
    void onDataSent(beast::error_code, std::size_t const);
    void receiveData();
    void onDataReceived(beast::error_code, std::size_t const);
    void doConnect();

  public:
    kc_https_plug(net::io_context&, ssl::context&,
                  trade_type_e const tradeType,
                  api_data_t const &apiData);

    ~kc_https_plug();
    void startConnect();
  };
}
