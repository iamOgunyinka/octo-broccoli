#include "kucoin_symbols.hpp"
#include "constants.hpp"

#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

static auto const kucoin_spot_tokens_url =
    QString("https://") + korrelator::constants::kucoin_https_spot_host +
    "/api/v1/market/allTickers";
static auto const kucoin_futures_tokens_url =
    QString("https://") + korrelator::constants::kc_futures_api_host +
    "/api/v1/contracts/active";

namespace korrelator {

kucoin_symbols::kucoin_symbols(QNetworkAccessManager& networkManager)
  : m_networkManager(networkManager)
{
}

void kucoin_symbols::getFuturesSymbols(
    success_callback_t onSuccess, error_callback_t onError) {
  return sendNetworkRequest(kucoin_futures_tokens_url, trade_type_e::futures,
                            onSuccess, onError);
}

void kucoin_symbols::getSpotsSymbols(
    success_callback_t onSuccess, error_callback_t onError) {
  return sendNetworkRequest(kucoin_spot_tokens_url, trade_type_e::spot,
                            onSuccess, onError);
}

void kucoin_symbols::getSpotsExchangeInfo(
    token_list_t* spotsContainerPtr, error_callback_t onError) {
  auto const fullUrl = QString("https://") +
      korrelator::constants::kucoin_https_spot_host + QString("/api/v1/symbols");
  QNetworkRequest request(fullUrl);
  request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader,
                    "application/json");
  configureRequestForSSL(request);
  m_networkManager.enableStrictTransportSecurityStore(true);

  auto reply = m_networkManager.get(request);
  QObject::connect(reply, &QNetworkReply::finished, [=] {
    if (reply->error() != QNetworkReply::NoError) {
      onError("Unable to get the list of all token pairs=>" +
              reply->errorString());
      return;
    }

    auto const responseString = reply->readAll();
    auto const rootObject = QJsonDocument::fromJson(responseString).object();
    if (auto codeIter = rootObject.find("code");
        codeIter == rootObject.end() || codeIter->toString() != "200000")
      return;
    auto const dataList = rootObject.value("data").toArray();
    if (dataList.isEmpty())
      return;

    auto &container = *spotsContainerPtr;
    for (int i = 0; i < dataList.size(); ++i) {
      auto const itemObject = dataList[i].toObject();
      auto const symbol = itemObject.value("symbol").toString();

      auto iter = std::find_if(container.begin(), container.end(),
                               [symbol](korrelator::token_t const &a) {
                                 return a.symbolName.compare(
                                            symbol, Qt::CaseInsensitive) == 0;
                               });
      if (iter != container.end()) {
        iter->baseMinSize =
            itemObject.value("baseMinSize").toString().toDouble();
        iter->quoteMinSize =
            itemObject.value("quoteMinSize").toString().toDouble();
        iter->quoteCurrency = itemObject.value("quoteCurrency").toString();
        iter->baseCurrency = itemObject.value("baseCurrency").toString();

        auto const baseIncrement = itemObject.value("baseIncrement").toString();
        if (auto const pointIndex = baseIncrement.indexOf('.');
            pointIndex != -1)
          iter->baseAssetPrecision = baseIncrement.length() - (pointIndex + 1);
        auto const quoteIncrement =
            itemObject.value("quoteIncrement").toString();
        if (auto const pointIndex = quoteIncrement.indexOf('.');
            pointIndex != -1)
          iter->quotePrecision = quoteIncrement.length() - (pointIndex + 1);
      }
    }
  });

  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}

void kucoin_symbols::sendNetworkRequest(
    QString const &url, trade_type_e const tradeType,
    success_callback_t onSuccessCallback, error_callback_t onError) {
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader,
                    "application/json");
  korrelator::configureRequestForSSL(request);
  m_networkManager.enableStrictTransportSecurityStore(true);

  auto reply = m_networkManager.get(request);
  QObject::connect(
      reply, &QNetworkReply::finished, [=, cb = std::move(onSuccessCallback)] {
        if (reply->error() != QNetworkReply::NoError) {
          return onError("Unable to get the list of all token pairs => "
                         + reply->errorString());
        }
        auto const responseString = reply->readAll();
        auto const jsonResponse = QJsonDocument::fromJson(responseString);
        if (jsonResponse.isEmpty()) {
          return onError("Unable to read the response sent");
        }

        korrelator::token_list_t tokenList;
        QJsonArray list;
        QString key = "last";
        QString const key2 = "lastTradePrice";
        auto const rootObject = jsonResponse.object();
        auto const jsonData = rootObject.value("data");
        if (jsonData.isObject())
          list = jsonData.toObject().value("ticker").toArray();
        else if (jsonData.isArray())
          list = jsonData.toArray();

        if (list.isEmpty())
          return cb({}, exchange_name_e::kucoin);

        tokenList.reserve(list.size());
        for (int i = 0; i < list.size(); ++i) {
          auto const tokenObject = list[i].toObject();
          korrelator::token_t t;
          t.symbolName = tokenObject.value("symbol").toString().toLower();
          t.realPrice = std::make_shared<double>();

          if (tokenObject.contains("baseCurrency"))
            t.baseCurrency = tokenObject.value("baseCurrency").toString();

          if (tokenObject.contains("quoteCurrency"))
            t.quoteCurrency = tokenObject.value("quoteCurrency").toString();

          if (tokenObject.contains("quoteMinSize"))
            t.quoteMinSize = tokenObject.value("quoteMinSize").toDouble();

          if (!tokenObject.contains(key)) {
            if (auto const f = tokenObject.value(key2); f.isString())
              *t.realPrice = f.toString().toDouble();
            else
              *t.realPrice = f.toDouble();

            if (tokenObject.contains("multiplier"))
              t.multiplier = tokenObject.value("multiplier").toDouble();
            if (tokenObject.contains("tickSize"))
              t.tickSize = tokenObject.value("tickSize").toDouble();
            if (tokenObject.contains("quoteMinSize"))
              t.quoteMinSize = tokenObject.value("quoteMinSize").toDouble();
          } else {
            *t.realPrice = tokenObject.value(key).toString().toDouble();
          }

          t.exchange = exchange_name_e::kucoin;
          t.tradeType = tradeType;
          tokenList.push_back(std::move(t));
        }

        std::sort(tokenList.begin(), tokenList.end(),
                  korrelator::token_compare_t{});
        cb(std::move(tokenList), exchange_name_e::kucoin);
      });

  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}

}
