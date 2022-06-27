#include "binance_https_request.hpp"

#include "binance_futures_plug.hpp"
#include "binance_spots_plug.hpp"

namespace korrelator {

double format_quantity(double const value, int decimal_places) {
  if (decimal_places == -1)
    decimal_places = 8;
  double const multiplier = std::pow(10.0, decimal_places);
  return std::trunc(value * multiplier) / multiplier;
}

binance_https_plug::binance_https_plug(net::io_context &ioContext,
                                       ssl::context &sslContext,
                                       trade_type_e const tradeType,
                                       api_data_t const &apiData,
                                       trade_config_data_t *tradeConfig)
    : m_tradeType(tradeType) {
  if (trade_type_e::spot == m_tradeType) {
    m_binancePlug.spot = new details::binance_spots_plug(ioContext, sslContext,
                                                         apiData, tradeConfig);
  } else {
    m_binancePlug.futures = new details::binance_futures_plug(
        ioContext, sslContext, apiData, tradeConfig);
  }
}

void binance_https_plug::setLeverage() {
  if (m_tradeType == trade_type_e::futures)
    m_binancePlug.futures->setLeverage();
}

void binance_https_plug::setPrice(double const price) {
  if (m_tradeType == trade_type_e::futures)
    return m_binancePlug.futures->setPrice(price);
  m_binancePlug.spot->setPrice(price);
}

void binance_https_plug::startConnect() {
  if (m_tradeType == trade_type_e::futures)
    return m_binancePlug.futures->startConnect();
  m_binancePlug.spot->startConnect();
}

double binance_https_plug::averagePrice() const {
  if (m_tradeType == trade_type_e::futures)
    return m_binancePlug.futures->averagePrice();
  return m_binancePlug.spot->averagePrice();
}

QString binance_https_plug::errorString() const {
  if (m_tradeType == trade_type_e::futures)
    return m_binancePlug.futures->errorString();
  return m_binancePlug.spot->errorString();
}

binance_https_plug::~binance_https_plug() {
  if (m_tradeType == trade_type_e::futures)
    delete m_binancePlug.futures;
  else
    delete m_binancePlug.spot;
}


net::io_context& getExchangeIOContext() {
  static std::unique_ptr<net::io_context> ioContext =
      std::make_unique<net::io_context>();
  return *ioContext;
}

} // namespace korrelator
