#include "ftx_https_request.hpp"

#include "ftx_futures_plug.hpp"
#include "ftx_spots_plug.hpp"

namespace korrelator {

ftx_trader::ftx_trader(net::io_context &ioContext, ssl::context &sslContext,
                       trade_type_e const tradeType, api_data_t const &apiData,
                       trade_config_data_t *tradeConfig)
    : m_tradeType(tradeType) {
  if (trade_type_e::spot == m_tradeType) {
    m_exchangePlug.spot = new details::ftx_spots_plug(ioContext, sslContext,
                                                      apiData, tradeConfig);
  } else {
    m_exchangePlug.futures = new details::ftx_futures_plug(
        ioContext, sslContext, apiData, tradeConfig);
  }
}

void ftx_trader::setPrice(double const price) {
  if (m_tradeType == trade_type_e::futures)
    return m_exchangePlug.futures->setPrice(price);
  m_exchangePlug.spot->setPrice(price);
}

void ftx_trader::setAccountLeverage() {
  if (m_tradeType == trade_type_e::futures)
    return m_exchangePlug.futures->setAccountLeverage();
}

void ftx_trader::startConnect() {
  if (m_tradeType == trade_type_e::futures)
    return m_exchangePlug.futures->startConnect();
  m_exchangePlug.spot->startConnect();
}

double ftx_trader::getAveragePrice() const {
  if (m_tradeType == trade_type_e::futures)
    return m_exchangePlug.futures->getAveragePrice();
  return m_exchangePlug.spot->getAveragePrice();
}

QString ftx_trader::errorString() const {
  if (m_tradeType == trade_type_e::futures)
    return m_exchangePlug.futures->errorString();
  return m_exchangePlug.spot->errorString();
}

ftx_trader::~ftx_trader() {
  if (m_tradeType == trade_type_e::futures)
    delete m_exchangePlug.futures;
  else
    delete m_exchangePlug.spot;
}

} // namespace korrelator
