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

binance_trader::binance_trader(net::io_context &ioContext,
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

void binance_trader::setLeverage() {
  if (m_tradeType == trade_type_e::futures)
    m_binancePlug.futures->setLeverage();
}

void binance_trader::setPrice(double const price) {
  if (m_tradeType == trade_type_e::futures)
    return m_binancePlug.futures->setPrice(price);
  m_binancePlug.spot->setPrice(price);
}

void binance_trader::startConnect() {
  if (m_tradeType == trade_type_e::futures)
    return m_binancePlug.futures->startConnect();
  m_binancePlug.spot->startConnect();
}

double binance_trader::averagePrice() const {
  if (m_tradeType == trade_type_e::futures)
    return m_binancePlug.futures->averagePrice();
  return m_binancePlug.spot->averagePrice();
}

QString binance_trader::errorString() const {
  if (m_tradeType == trade_type_e::futures)
    return m_binancePlug.futures->errorString();
  return m_binancePlug.spot->errorString();
}

binance_trader::~binance_trader() {
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
