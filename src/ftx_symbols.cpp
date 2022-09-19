#include "ftx_symbols.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

static char const *const ftx_spot_url = "https://ftx.com/api/markets";
static char const *const ftx_futures_url = "https://ftx.com/api/futures";

namespace korrelator {

ftx_symbols::ftx_symbols(QNetworkAccessManager &networkManager)
    : m_networkManager(networkManager) {}

trade_type_e getTradeType(QString const &type) {
  if (type.compare("spot", Qt::CaseInsensitive) == 0)
    return trade_type_e::spot;
  else if (type.contains("future", Qt::CaseInsensitive))
    return trade_type_e::futures;
  return trade_type_e::unknown;
}

void ftx_symbols::getSpotsSymbols(success_callback_t onSuccess,
                                  error_callback_t onError) {
  return sendNetworkRequest(ftx_spot_url, trade_type_e::spot, onSuccess,
                            onError);
}

void ftx_symbols::getFuturesSymbols(success_callback_t onSuccess,
                                    error_callback_t onError) {
  QNetworkRequest request{QUrl{ftx_futures_url}};
  request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader,
                    "application/json");

  configureRequestForSSL(request);
  m_networkManager.enableStrictTransportSecurityStore(true);

  auto reply = m_networkManager.get(request);
  QObject::connect(reply, &QNetworkReply::finished, [=] {
    if (reply->error() != QNetworkReply::NoError) {
      return onError("Unable to get the list of all token pairs => " +
                     reply->errorString());
    }
    auto const responseString = reply->readAll();
    auto const jsonObject = QJsonDocument::fromJson(responseString).object();
    if (jsonObject.isEmpty()) {
      return onError("Unable to read the response sent");
    }
    QJsonArray const list = jsonObject.value("result").toArray();
    if (list.isEmpty())
      return onError({});

    korrelator::token_list_t tokenList;
    tokenList.reserve(list.size());

    for (int i = 0; i < list.size(); ++i) {
      QJsonObject const tokenObject = list[i].toObject();
      if (!tokenObject.value("enabled").toBool())
        continue;

      korrelator::token_t t;
      t.symbolName = tokenObject.value("name").toString().toLower();
      t.realPrice = std::make_shared<double>(tokenObject.value("last").toDouble());
      t.baseMinSize = tokenObject.value("lowerBound").toDouble();
      t.baseCurrency = tokenObject.value("underlying").toString();
      t.exchange = exchange_name_e::ftx;
      t.tradeType = trade_type_e::futures;
      tokenList.push_back(std::move(t));
    }

    std::sort(tokenList.begin(), tokenList.end(),
              korrelator::token_compare_t{});
    Q_ASSERT(onSuccess != nullptr);
    onSuccess(std::move(tokenList), exchange_name_e::ftx);
  });

  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}

void ftx_symbols::sendNetworkRequest(QString const &url,
                                     trade_type_e const expectedTradeType,
                                     success_callback_t onSuccess,
                                     error_callback_t onError) {
  QNetworkRequest request{QUrl{url}};
  request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader,
                    "application/json");

  configureRequestForSSL(request);
  m_networkManager.enableStrictTransportSecurityStore(true);

  auto reply = m_networkManager.get(request);
  QObject::connect(reply, &QNetworkReply::finished, [=] {
    if (reply->error() != QNetworkReply::NoError) {
      return onError("Unable to get the list of all token pairs => " +
                     reply->errorString());
    }
    auto const responseString = reply->readAll();
    auto const jsonObject = QJsonDocument::fromJson(responseString).object();
    if (jsonObject.isEmpty()) {
      return onError("Unable to read the response sent");
    }
    QJsonArray const list = jsonObject.value("result").toArray();
    if (list.isEmpty())
      return onError({});

    korrelator::token_list_t tokenList;
    tokenList.reserve(list.size());

    for (int i = 0; i < list.size(); ++i) {
      QJsonObject const tokenObject = list[i].toObject();
      if (!tokenObject.value("enabled").toBool())
        continue;

      auto const type = tokenObject.value("type").toString();
      trade_type_e const tradeType = getTradeType(type);
      if (tradeType != trade_type_e::spot)
        continue;

      korrelator::token_t t;
      t.symbolName = tokenObject.value("name").toString().toLower();
      t.realPrice = std::make_shared<double>(tokenObject.value("price").toDouble());
      t.baseCurrency = tokenObject.value("baseCurrency").toString();
      t.quoteCurrency = tokenObject.value("quoteCurrency").toString();
      t.multiplier = tokenObject.value("priceIncrement").toDouble();
      t.tickSize = tokenObject.value("sizeIncrement").toDouble();
      t.exchange = exchange_name_e::ftx;
      t.tradeType = expectedTradeType;
      tokenList.push_back(std::move(t));
    }

    std::sort(tokenList.begin(), tokenList.end(),
              korrelator::token_compare_t{});
    Q_ASSERT(onSuccess != nullptr);
    onSuccess(std::move(tokenList), exchange_name_e::ftx);
  });

  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}

void ftx_symbols::getSpotsExchangeInfo(token_list_t *,
                                       error_callback_t) {}

void ftx_symbols::getFuturesExchangeInfo(token_list_t *,
                                         error_callback_t) {}

} // namespace korrelator
