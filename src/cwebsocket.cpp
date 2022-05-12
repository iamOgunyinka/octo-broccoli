#include "cwebsocket.hpp"

#include <QWebSocket>
#include <optional>
#include <rapidjson/document.h>

namespace korrelator {

extern QSslConfiguration getSSLConfig();

std::optional<std::pair<QString, double>> getCoinPrice(QString const &t) {
  auto const text = t.toStdString();

  rapidjson::Document d;
  d.Parse(text.c_str(), text.size());

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
      qDebug() << tokenName << amount << type.c_str();
      return std::make_pair(tokenName.toLower(), amount);
    }
  } catch(...) {
    return std::nullopt;
  }

  if (d.HasParseError() || !d.IsObject()) {
    Q_ASSERT(false);
    return std::nullopt;
  }

  return std::nullopt;
}

cwebsocket::cwebsocket() {}

cwebsocket::~cwebsocket() {
  m_stopRequested = true;
  reset();
}

void cwebsocket::startWatch() {
  auto& futures = m_subscribedTokens[static_cast<int>(trade_type_e::futures)];
  auto& spots = m_subscribedTokens[static_cast<int>(trade_type_e::spot)];
  if (futures.empty())
    futures.insert("runeusdt");
  if (spots.empty())
    spots.insert("runeusdt");

  initializeThreadForConnection();
}

void cwebsocket::openConnections() {
  if (m_subscribedTokens[0].empty() || m_subscribedTokens[1].empty()) {
    qDebug() << "Cannot open connections";
    return;
  }

  auto const &spotToken =
      *m_subscribedTokens[static_cast<int>(trade_type_e::spot)].begin();

  auto const &futuresToken =
      *m_subscribedTokens[static_cast<int>(trade_type_e::futures)].begin();

  auto spotUrl_ = "wss://stream.binance.com:9443/stream?streams=" + spotToken + "@aggTrade";
  auto futuresUrl_ = "wss://fstream.binance.com/stream?streams=" + futuresToken + "@aggTrade";
  QMetaObject::invokeMethod(
      this,
      [this, spotUrl = std::move(spotUrl_),
       futuresUrl = std::move(futuresUrl_)] {
        m_spotWebsocket->open(QUrl(spotUrl));
        m_futuresWebsocket->open(QUrl(futuresUrl));
      },
      Qt::AutoConnection);
}

void cwebsocket::reestablishConnections() {
  m_connectionRegained = false;

  if (m_stopRequested)
    return;

  QThread::currentThread()->sleep(2);
  openConnections();
}

void cwebsocket::onSpotConnectionEstablished() {
  m_isStarted = true;

  int i = 200;
  for (auto const &spotToken :
       m_subscribedTokens[static_cast<int>(trade_type_e::spot)]) {
    auto const spotMessage = QString(R"(
    {
      "method": "SUBSCRIBE",
      "params":
      [
        "%1@aggTrade"
      ],
      "id": %2
    })").arg(spotToken).arg(++i);
    m_spotWebsocket->sendTextMessage(spotMessage);
  }
}

void cwebsocket::onFuturesConnectionEstablished() {
  int i = 20;
  for (auto const &futuresToken :
       m_subscribedTokens[static_cast<int>(trade_type_e::futures)]) {
    auto const depthMessage = QString(R"(
    {
      "method": "SUBSCRIBE",
      "params":
      [
        "%1@aggTrade"
      ],
      "id": %2
    })").arg(futuresToken).arg(++i);
    m_futuresWebsocket->sendTextMessage(depthMessage);
  }

  if (!m_connectionRegained) {
    m_connectionRegained = true;
    emit connectionRegained();
  }
}

void cwebsocket::initializeThreadForConnection() {
  reset();

  m_futuresWebsocket = std::make_unique<QWebSocket>();
  m_spotWebsocket = std::make_unique<QWebSocket>();
  m_worker = std::make_unique<Worker>([this] { openConnections(); });
  m_thread.reset(new QThread);

  m_futuresWebsocket->setSslConfiguration(getSSLConfig());
  m_spotWebsocket->setSslConfiguration(getSSLConfig());

  QObject::connect(m_spotWebsocket.get(), &QWebSocket::connected, this,
                   [this] { onSpotConnectionEstablished(); });
  QObject::connect(m_futuresWebsocket.get(), &QWebSocket::connected, this,
                   [this] { onFuturesConnectionEstablished(); });
  QObject::connect(m_spotWebsocket.get(), &QWebSocket::disconnected, this,
                   [this] { reestablishConnections(); });
  QObject::connect(m_futuresWebsocket.get(), &QWebSocket::disconnected, this,
                   [this] { reestablishConnections(); });
  QObject::connect(m_futuresWebsocket.get(), &QWebSocket::textMessageReceived,
                   m_worker.get(),
                   [this](QString const &t) { onFuturesMessageReceived(t); });
  QObject::connect(m_spotWebsocket.get(), &QWebSocket::textMessageReceived,
                   m_worker.get(),
                   [this](QString const &t) { onSpotMessageReceived(t); });

  QObject::connect(
      m_spotWebsocket.get(),
      static_cast<void (QWebSocket::*)(QAbstractSocket::SocketError)>(
          &QWebSocket::error),
      this, [this](QAbstractSocket::SocketError const e) {
        emit connectionLost();
        qDebug() << e;
      });

  QObject::connect(
      m_futuresWebsocket.get(),
      static_cast<void (QWebSocket::*)(QAbstractSocket::SocketError)>(
          &QWebSocket::error),
      this, [this](QAbstractSocket::SocketError const e) {
        emit connectionLost();
        qDebug() << e;
      });

  QObject::connect(m_thread.get(), &QThread::started, m_worker.get(),
                   &Worker::startWork);

  m_worker->moveToThread(m_thread.get());
  m_thread->start();
}

void cwebsocket::reset() {
  if (m_futuresWebsocket) {
    m_futuresWebsocket->close(QWebSocketProtocol::CloseCodeNormal);
    m_futuresWebsocket->disconnect();
  }

  if (m_spotWebsocket) {
    m_spotWebsocket->close(QWebSocketProtocol::CloseCodeNormal);
    m_spotWebsocket->disconnect();
  }

  m_futuresWebsocket.reset();
  m_spotWebsocket.reset();
  m_worker.reset();
  m_thread.reset();
  m_isStarted = false;
}

void cwebsocket::onSpotMessageReceived(QString const &t) {
  auto const tokenPricePair = getCoinPrice(t);
  if (!tokenPricePair)
    return;
  auto const &[tokenName, amount] = *tokenPricePair;
  emit newPriceReceived(tokenName, amount, (int)trade_type_e::spot);
}

void cwebsocket::onFuturesMessageReceived(QString const &t) {
  auto const tokenPricePair = getCoinPrice(t);
  if (!tokenPricePair)
    return;
  auto const &[tokenName, amount] = *tokenPricePair;
  emit newPriceReceived(tokenName, amount, (int)trade_type_e::futures);
}

} // namespace korrelator
