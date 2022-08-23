#include "double_trader.hpp"

#include "binance_https_request.hpp"
#include "ftx_https_request.hpp"
#include "kucoin_https_request.hpp"
#include "order_model.hpp"

namespace korrelator {

double_trader_t::double_trader_t(std::function<void()> refreshModelCallback,
                                 std::unique_ptr<order_model> &model, int &maxRetries):
  m_ioContext(getExchangeIOContext()), m_sslContext(getSSLContext()),
  m_maxRetries(maxRetries), m_model(model),
  m_modelRefreshCallback(refreshModelCallback)
{
}

void double_trader_t::operator()(plug_data_t &&tradeMetadata) {
  // only when program is stopped
  if (tradeMetadata.tradeType == trade_type_e::unknown) {
    m_lastQuantity = NAN;
    m_futuresLeverageIsSet = false;
    m_isFirstTrade = true;
    m_lastAction = trade_action_e::nothing;
    m_ioContext.stop();
    return m_ioContext.restart();
  }

  model_data_t* modelData = nullptr;
  auto tradeCopy = tradeMetadata;

  if (tradeCopy.tradeType == trade_type_e::futures)
    tradeCopy.tradeType = trade_type_e::spot;
  else if (tradeCopy.tradeType == trade_type_e::spot)
    tradeCopy.tradeType = trade_type_e::futures;
}

}
