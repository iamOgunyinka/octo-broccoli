#include "websocket_manager.hpp"

#include "binance_websocket.hpp"
#include "kc_websocket.hpp"

namespace korrelator {

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

websocket_manager::websocket_manager() : m_sslContext(getSSLContext()) {
  getRawIOContext().reset();
  m_ioContext = getIOContext();
}

websocket_manager::~websocket_manager() {
  m_ioContext->stop();

  for (auto &sock : m_sockets) {
    std::visit(
        [](auto &&v) {
          v->requestStop();
          delete v;
        },
        sock);
  }

  m_sockets.clear();
}

void websocket_manager::addSubscription(QString const &tokenName,
                                        trade_type_e const tradeType,
                                        exchange_name_e const exchange,
                                        double &result) {
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

  if (exchange == exchange_name_e::binance) {
    auto sock = new binance_ws(*m_ioContext, m_sslContext, result, tradeType);
    sock->addSubscription(tokenName.toLower());
    m_sockets.push_back(std::move(sock));
  } else if (exchange == exchange_name_e::kucoin) {
#ifdef TESTNET
    static std::unique_ptr<net::ssl::context> sslContext = nullptr;
    if (!sslContext) {
      sslContext =
          std::make_unique<net::ssl::context>(net::ssl::context::sslv23_client);
      sslContext->set_default_verify_paths();
      sslContext->set_verify_mode(boost::asio::ssl::verify_none);
    }
#endif
    auto sock = new kucoin_ws(*m_ioContext,
#ifndef TESTNET
                              m_sslContext,
#else
                              *sslContext,
#endif
                              result, tradeType);
    sock->addSubscription(tokenName.toUpper());
    m_sockets.push_back(std::move(sock));
  }
}

void websocket_manager::startWatch() {
  m_checker.clear();

  for (auto &sock : m_sockets) {
    std::visit(
        [this](auto &&v) mutable {
          std::thread([this, &v]() mutable {
            v->startFetching();
            m_ioContext->run();
          }).detach();
        },
        sock);
  }
}

} // namespace korrelator
