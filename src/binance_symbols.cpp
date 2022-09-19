#include "binance_symbols.hpp"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

static auto const binance_futures_tokens_url =
    QString("https://") + korrelator::constants::binance_http_futures_host +
    "/fapi/v1/ticker/price";
static auto const binance_spot_tokens_url =
    QString("https://") + korrelator::constants::binance_http_spot_host +
    "/api/v3/ticker/price";

namespace korrelator {

binance_symbols::binance_symbols(QNetworkAccessManager& networkManager)
  : m_networkManager(networkManager)
{
}

void binance_symbols::getFuturesSymbols(
    success_callback_t onSuccess, error_callback_t onError) {
  return sendNetworkRequest(binance_futures_tokens_url, trade_type_e::futures,
                            onSuccess, onError);
}

void binance_symbols::getSpotsSymbols(
    success_callback_t onSuccess, error_callback_t onError) {
  return sendNetworkRequest(binance_spot_tokens_url, trade_type_e::spot,
                            onSuccess, onError);
}

void binance_symbols::sendNetworkRequest(
    QString const &url, trade_type_e const tradeType,
    success_callback_t onSuccess, error_callback_t onError) {
  QNetworkRequest request{QUrl{url}};
  request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader,
                    "application/json");
  configureRequestForSSL(request);
  m_networkManager.enableStrictTransportSecurityStore(true);

  auto reply = m_networkManager.get(request);
  QObject::connect( reply, &QNetworkReply::finished, [=] {
    if (reply->error() != QNetworkReply::NoError) {
      onError("Unable to get the list of all token pairs"
              "=> " + reply->errorString());
      return;
    }
    auto const responseString = reply->readAll();
    auto const jsonResponse = QJsonDocument::fromJson(responseString);
    if (jsonResponse.isEmpty()) {
      onError("Unable to read the response sent");
      return;
    }

    QJsonArray list;
    list = jsonResponse.array();
    if (list.isEmpty())
      return onError({});

    korrelator::token_list_t tokenList;
    tokenList.reserve(list.size());

    for (int i = 0; i < list.size(); ++i) {
      auto const tokenObject = list[i].toObject();
      korrelator::token_t t;
      t.realPrice = std::make_shared<double>(
            tokenObject.value("price").toString().toDouble());
      t.symbolName = tokenObject.value("symbol").toString().toLower();
      t.exchange = exchange_name_e::binance;
      t.tradeType = tradeType;
      tokenList.push_back(std::move(t));
    }

    std::sort(tokenList.begin(), tokenList.end(),
              korrelator::token_compare_t{});
    onSuccess(std::move(tokenList), exchange_name_e::binance);
  });

  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}

void binance_symbols::getFuturesExchangeInfo(
    token_list_t *futuresContainerPtr,
    exchange_info_callback_t successCallback,
    error_callback_t onError) {
  auto const fullUrl = QString("https://") +
      korrelator::constants::binance_http_futures_host +
      QString("/fapi/v1/exchangeInfo");
  QNetworkRequest request(fullUrl);
  request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader,
                    "application/json");
  configureRequestForSSL(request);
  m_networkManager.enableStrictTransportSecurityStore(true);

  auto reply = m_networkManager.get(request);
  QObject::connect(reply, &QNetworkReply::finished, [=] {
    if (reply->error() != QNetworkReply::NoError) {
      onError("Unable to get the list of all token pairs"
                            "=> " + reply->errorString());
      return;
    }
    auto const responseString = reply->readAll();
    QJsonObject const rootObject =
        QJsonDocument::fromJson(responseString).object();
    QJsonArray const symbols = rootObject.value("symbols").toArray();
    auto& futuresContainer = *futuresContainerPtr;

    for (int i = 0; i < symbols.size(); ++i) {
      auto const symbolObject = symbols[i].toObject();
      auto const tokenName = symbolObject.value("pair").toString();
      auto const status = symbolObject.value("status").toString();

      auto iter = std::lower_bound(futuresContainer.begin(),
                                   futuresContainer.end(),
                                   tokenName, korrelator::token_compare_t{});
      if (iter == futuresContainer.end() ||
          iter->symbolName.compare(tokenName, Qt::CaseInsensitive) != 0)
        continue;
      if (status.compare("trading", Qt::CaseInsensitive) != 0) {
        futuresContainer.erase(iter);
      } else {
        iter->pricePrecision =
            (uint8_t)symbolObject.value("pricePrecision").toInt(8);
        iter->quantityPrecision =
            (uint8_t)symbolObject.value("quantityPrecision").toInt(8);
        iter->baseAssetPrecision =
            (uint8_t)symbolObject.value("baseAssetPrecision").toInt(8);
        iter->quotePrecision =
            (uint8_t)symbolObject.value("quotePrecision").toInt(8);
        iter->baseCurrency = symbolObject.value("baseAsset").toString();
        iter->quoteCurrency = symbolObject.value("quoteAsset").toString();

        auto const filters = symbolObject.value("filters").toArray();
        for (int x = 0; x < filters.size(); ++x) {
          auto const filterObject = filters[x].toObject();
          auto const filterType = filterObject.value("filterType").toString();
          if (filterType.compare("LOT_SIZE", Qt::CaseInsensitive) == 0) {
            iter->tickSize =
                filterObject.value("stepSize").toString().toDouble();
          } else if (filterType.compare("MIN_NOTIONAL", Qt::CaseInsensitive) ==
                     0) {
            iter->quoteMinSize =
                filterObject.value("minNotional").toString().toDouble();
          }
        }
      }
    }
    if (!symbols.isEmpty())
      successCallback();
  });
  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}

void binance_symbols::getSpotsExchangeInfo(
    token_list_t* spotsContainerPtr,
    exchange_info_callback_t successCallback,
    error_callback_t onError) {
  auto const fullUrl = QString("https://") +
      korrelator::constants::binance_http_spot_host +
      QString("/api/v3/exchangeInfo");
  QNetworkRequest request(fullUrl);
  request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader,
                    "application/json");
  configureRequestForSSL(request);
  m_networkManager.enableStrictTransportSecurityStore(true);

  auto reply = m_networkManager.get(request);
  QObject::connect(reply, &QNetworkReply::finished, [=] {
    if (reply->error() != QNetworkReply::NoError) {
      onError("Unable to get the list of all token pairs"
                            "=> " + reply->errorString());
      return;
    }
    auto const responseString = reply->readAll();
    QJsonObject const rootObject =
        QJsonDocument::fromJson(responseString).object();
    QJsonArray const symbols = rootObject.value("symbols").toArray();

    auto &container = *spotsContainerPtr;
    for (int i = 0; i < symbols.size(); ++i) {
      auto const symbolObject = symbols[i].toObject();
      auto const tokenName = symbolObject.value("symbol").toString();

      auto const status = symbolObject.value("status").toString();
      auto iter = std::lower_bound(container.begin(), container.end(),
                                   tokenName, korrelator::token_compare_t{});
      if (iter == container.end() ||
          iter->symbolName.compare(tokenName, Qt::CaseInsensitive) != 0)
        continue;
      if (status.compare("trading", Qt::CaseInsensitive) != 0) {
        container.erase(iter);
      } else {
        iter->pricePrecision =
            (uint8_t)symbolObject.value("pricePrecision").toInt(8);
        iter->quantityPrecision =
            (uint8_t)symbolObject.value("quantityPrecision").toInt(8);
        iter->baseAssetPrecision =
            (uint8_t)symbolObject.value("baseAssetPrecision").toInt(8);
        iter->quotePrecision =
            (uint8_t)symbolObject.value("quotePrecision").toInt(8);
        iter->baseCurrency = symbolObject.value("baseAsset").toString();
        iter->quoteCurrency = symbolObject.value("quoteAsset").toString();

        auto const filters = symbolObject.value("filters").toArray();
        for (int x = 0; x < filters.size(); ++x) {
          auto const filterObject = filters[x].toObject();
          auto const filterType = filterObject.value("filterType").toString();
          if (filterType.compare("LOT_SIZE", Qt::CaseInsensitive) == 0) {
            iter->tickSize =
                filterObject.value("stepSize").toString().toDouble();
          } else if (filterType.compare("MIN_NOTIONAL", Qt::CaseInsensitive) ==
                     0) {
            iter->quoteMinSize =
                filterObject.value("minNotional").toString().toDouble();
          }
        }
      }
    }
    if (!symbols.isEmpty())
      successCallback();
  });
  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}

}
