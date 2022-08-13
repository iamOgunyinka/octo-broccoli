#include "kucoin_https_request.hpp"

#include "kucoin_futures_plug.hpp"
#include "kucoin_spots_plug.hpp"

namespace korrelator {

kucoin_trader::kucoin_trader(net::io_context &ioContext,
                                     ssl::context &sslContext,
                                     trade_type_e const tradeType,
                                     api_data_t const &apiData,
                                     trade_config_data_t *tradeConfig)
    : m_tradeType(tradeType) {
  if (trade_type_e::spot == m_tradeType) {
    m_exchangePlug.spot = new details::kucoin_spots_plug(ioContext, sslContext,
                                                         apiData, tradeConfig);
  } else {
    m_exchangePlug.futures = new details::kucoin_futures_plug(
        ioContext, sslContext, apiData, tradeConfig);
  }
}

void kucoin_trader::setPrice(double const price) {
  if (m_tradeType == trade_type_e::futures)
    return m_exchangePlug.futures->setPrice(price);
  m_exchangePlug.spot->setPrice(price);
}

void kucoin_trader::startConnect() {
  if (m_tradeType == trade_type_e::futures)
    return m_exchangePlug.futures->startConnect();
  m_exchangePlug.spot->startConnect();
}

double kucoin_trader::quantityPurchased() const {
  if (m_tradeType == trade_type_e::futures)
    return m_exchangePlug.futures->quantityPurchased();
  return m_exchangePlug.spot->quantityPurchased();
}

double kucoin_trader::sizePurchased() const {
  if (m_tradeType == trade_type_e::futures)
    return m_exchangePlug.futures->sizePurchased();
  return m_exchangePlug.spot->sizePurchased();
}

QString kucoin_trader::errorString() const {
  if (m_tradeType == trade_type_e::futures)
    return m_exchangePlug.futures->errorString();
  return m_exchangePlug.spot->errorString();
}

kucoin_trader::~kucoin_trader() {
  if (m_tradeType == trade_type_e::futures)
    delete m_exchangePlug.futures;
  else
    delete m_exchangePlug.spot;
}

} // namespace korrelator
