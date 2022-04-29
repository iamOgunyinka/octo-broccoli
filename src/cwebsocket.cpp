#include "cwebsocket.hpp"

#include <QWebSocket>
#include <optional>
#include <rapidjson/document.h>

namespace brocolli {

extern QSslConfiguration getSSLConfig();

std::optional<std::pair<QString, double>> getCoinPrice(QString const &t) {
  auto const text = t.toStdString();
  rapidjson::Document d;

  d.Parse(text.c_str(), text.size());
  if (d.HasParseError() || !d.IsObject()) {
    Q_ASSERT(false);
    return std::nullopt;
  }

  auto const jsonObject = d.GetObject();
  auto const unsubIter = jsonObject.FindMember("method");
  auto const isUnsubscribe =
      unsubIter != jsonObject.end() &&
      unsubIter->value.GetString() == std::string("UNSUBSCRIBE");
  if (jsonObject.HasMember("result") || isUnsubscribe) // return normally
    return std::nullopt;

  auto const typeIter = jsonObject.FindMember("e");
  if (typeIter == jsonObject.MemberEnd()) {
    Q_ASSERT(false);
    return std::nullopt;
  }

  std::string const type = typeIter->value.GetString();

  // "24hrTicker"
  if (type.length() == 10 && type[0] == '2' && type.back() == 'r') {
    QString const tokenName = jsonObject.FindMember("s")->value.GetString();
    auto const amount =
        std::atof(jsonObject.FindMember("c")->value.GetString());
    return std::make_pair(tokenName.toLower(), amount);
  }
  qDebug() << "Error";
  return std::nullopt;
}

cwebsocket::cwebsocket() {}

cwebsocket::~cwebsocket() {
  m_stopRequested = true;
  reset();
}

void cwebsocket::removeDuds() {
  if (m_dudIsActiveInFutures) {
    m_dudIsActiveInFutures = false;
    unsubscribe("btcusdt", trade_type_e::futures);
  }
  if (m_dudIsActiveInSpot) {
    m_dudIsActiveInSpot = false;
    unsubscribe("btcusdt", trade_type_e::spot);
  }
}

void cwebsocket::subscribe(QString const &tokenName, trade_type_e const tt) {
  auto const [iter, successful] =
      m_subscribedTokens[static_cast<int>(tt)].insert(tokenName.toLower());

  if (!successful)
    return;

  auto& futures = m_subscribedTokens[static_cast<int>(trade_type_e::futures)];
  auto& spots = m_subscribedTokens[static_cast<int>(trade_type_e::spot)];
  if (tt == trade_type_e::spot && futures.empty()) {
    futures.insert("btcusdt");
  } else if (tt == trade_type_e::futures && spots.empty()) {
    spots.insert("btcusdt");
  }

  bool const workerIsActive = m_worker != nullptr && m_thread != nullptr;
  if (!workerIsActive)
    return initializeThreadForConnection();
  else if (tt == trade_type_e::spot)
    sendSpotTokenSubscription(*iter);
  else if (tt == trade_type_e::futures)
    sendFuturesTokenSubscription(*iter);
}

void cwebsocket::unsubscribe(QString const &tokenName, trade_type_e const tt) {
  auto &container = m_subscribedTokens[static_cast<int>(tt)];
  auto const &tokenNameLower = tokenName.toLower();
  auto const iter = container.find(tokenNameLower);
  if (iter == container.end() || m_thread == nullptr)
    return;

  container.erase(iter);
  if (container.empty())
    return;

  auto const unsubscribeMessage = QString(R"(
  {
    "method": "UNSUBSCRIBE",
    "params":
    [
      "%1@ticker"
    ],
    "id": 10
  })").arg(tokenNameLower);

  QMetaObject::invokeMethod(
      this,
      [this, unsubscribeMessage, tt] {
        if (tt == trade_type_e::spot)
          m_spotWebsocket->sendTextMessage(unsubscribeMessage);
        else
          m_futuresWebsocket->sendTextMessage(unsubscribeMessage);
      },
      Qt::AutoConnection);
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

  auto spotUrl_ = "wss://stream.binance.com:9443/ws/" + spotToken + "@ticker";
  auto futuresUrl_ = "wss://fstream.binance.com/ws/" + futuresToken + "@ticker";
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

  for (auto const &spotToken :
       m_subscribedTokens[static_cast<int>(trade_type_e::spot)]) {
    auto const spotMessage = QString(R"(
    {
      "method": "SUBSCRIBE",
      "params":
      [
        "%1@ticker"
      ],
      "id": 17
    })").arg(spotToken);
    m_spotWebsocket->sendTextMessage(spotMessage);
  }
}

void cwebsocket::onFuturesConnectionEstablished() {
  for (auto const &futuresToken :
       m_subscribedTokens[static_cast<int>(trade_type_e::futures)]) {
    auto const depthMessage = QString(R"(
    {
      "method": "SUBSCRIBE",
      "params":
      [
        "%1@ticker"
      ],
      "id": 20
    })").arg(futuresToken);
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

void cwebsocket::sendSpotTokenSubscription(QString const &tokenName) {
  QMetaObject::invokeMethod(
      this,
      [this, tokenName] {
        auto const message = QString(R"(
    {
      "method": "SUBSCRIBE",
      "params":
      [
        "%1@ticker"
      ],
      "id": 11
    })").arg(tokenName);
        m_spotWebsocket->sendTextMessage(message);
      },
      Qt::AutoConnection);
}

void cwebsocket::sendFuturesTokenSubscription(QString const &tokenName) {
  QMetaObject::invokeMethod(
      this,
      [this, tokenName] {
        auto const message = QString(R"(
    {
      "method": "SUBSCRIBE",
      "params":
      [
        "%1@ticker"
      ],
      "id": 6
    })").arg(tokenName);
        m_futuresWebsocket->sendTextMessage(message);
      },
      Qt::AutoConnection);
}

void cwebsocket::onSpotMessageReceived(QString const &t) {
  auto const tokenPricePair = getCoinPrice(t);
  if (!tokenPricePair)
    return;
  auto const &[tokenName, amount] = *tokenPricePair;
  emit newSpotPriceReceived(tokenName, amount);
}

void cwebsocket::onFuturesMessageReceived(QString const &t) {
  auto const tokenPricePair = getCoinPrice(t);
  if (!tokenPricePair)
    return;
  auto const &[tokenName, amount] = *tokenPricePair;
  emit newFuturesPriceReceived(tokenName, amount);
}

} // namespace brocolli
