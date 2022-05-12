#pragma once

#include "sthread.hpp"
#include <array>
#include <set>

class QWebSocket;

namespace korrelator {

enum class trade_type_e { spot, futures };

class cwebsocket : public QObject {
  Q_OBJECT

signals:
  void connectionLost();
  void connectionRegained();
  void newPriceReceived(QString const &, double const, int const);

public:
  cwebsocket();
  ~cwebsocket();

  void addSubscription(QString const &token, trade_type_e const tt) {
    m_subscribedTokens[static_cast<int>(tt)].insert(token.toLower());
  }
  void unsubscribe(QString const &token, trade_type_e const tt);
  void startWatch();

private:
  void initializeThreadForConnection();
  void openConnections();

  void onSpotConnectionEstablished();
  void onFuturesConnectionEstablished();
  void reestablishConnections();

  void reset();
  void onFuturesMessageReceived(QString const &t);
  void onSpotMessageReceived(QString const &t);

private:
  cthread_ptr m_thread = nullptr;
  worker_ptr m_worker = nullptr;
  std::unique_ptr<QWebSocket> m_spotWebsocket = nullptr;
  std::unique_ptr<QWebSocket> m_futuresWebsocket = nullptr;
  std::array<std::set<QString>, 2> m_subscribedTokens;
  bool m_isStarted = false;
  bool m_stopRequested = false;
  bool m_connectionRegained = false;
};

using cwebsocket_ptr = std::unique_ptr<cwebsocket>;

} // namespace korrelator
