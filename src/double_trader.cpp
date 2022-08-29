#include "double_trader.hpp"

#include <QDebug>

#include "binance_https_request.hpp"
#include "ftx_https_request.hpp"
#include "kucoin_https_request.hpp"
#include "order_model.hpp"

namespace korrelator {

QString actionTypeToString(trade_action_e a);

double_trader_t::double_trader_t(std::function<void()> refreshModelCallback,
                                 std::unique_ptr<order_model> &model, int &maxRetries):
  m_ioContext(getExchangeIOContext()), m_sslContext(getSSLContext()),
  m_maxRetries(maxRetries), m_model(model),
  m_modelRefreshCallback(refreshModelCallback)
{
}

void double_trader_t::operator()(
    plug_data_t &&firstMetadata, plug_data_t &&secondMetadata) {
  // only when program is stopped
  if (firstMetadata.tradeType == trade_type_e::unknown) {
    m_lastQuantity = NAN;
    m_futuresLeverageIsSet = false;
    m_isFirstTrade = true;
    m_lastAction = trade_action_e::nothing;
    m_ioContext.stop();
    return m_ioContext.restart();
  }

  connector_t firstConnector;
  connector_t secondConnector;

  initiateTrading(firstMetadata, firstConnector);
  initiateTrading(secondMetadata, secondConnector);

  // run the ioContext on both trades initiated
  m_ioContext.run();

  // if we're here, then both trades are done running.
  cleanupTradingData(firstMetadata, firstConnector);
  cleanupTradingData(secondMetadata, secondConnector);

  // update the table displaying the status of the trades
  m_modelRefreshCallback();

  // set the ioContext back to initial stage
  m_ioContext.stop();
  m_ioContext.restart();
}

void double_trader_t::initiateTrading(plug_data_t const &tradeMetadata,
                                      connector_t &connector) {
  auto const isKuCoin = tradeMetadata.exchange == exchange_name_e::kucoin;
  auto const tradeType = tradeMetadata.tradeType;
  if (isKuCoin) {
    connector.kucoinTrader = new kucoin_trader(
          m_ioContext, m_sslContext, tradeType, tradeMetadata.apiInfo,
          tradeMetadata.tradeConfig, m_maxRetries);
    connector.kucoinTrader->setPrice(tradeMetadata.tokenPrice);
    connector.kucoinTrader->startConnect();
  } else if (tradeMetadata.exchange == exchange_name_e::binance) {
    connector.binanceTrader = new binance_trader(
        m_ioContext, m_sslContext, tradeType, tradeMetadata.apiInfo,
        tradeMetadata.tradeConfig);
    if (!m_futuresLeverageIsSet && tradeType == trade_type_e::futures) {
        m_futuresLeverageIsSet = true;
        connector.binanceTrader->setLeverage();
    }
    connector.binanceTrader->setPrice(tradeMetadata.tokenPrice);
    connector.binanceTrader->startConnect();
  } else if (tradeMetadata.exchange == exchange_name_e::ftx) {
    connector.ftxTrader = new ftx_trader(
        m_ioContext, m_sslContext, tradeType,
        tradeMetadata.apiInfo, tradeMetadata.tradeConfig);
    if (!m_futuresLeverageIsSet && trade_type_e::futures == tradeType) {
        m_futuresLeverageIsSet = true;
        connector.ftxTrader->setAccountLeverage();
    }
    connector.ftxTrader->setPrice(tradeMetadata.tokenPrice);
    connector.ftxTrader->startConnect();
  }
}

void double_trader_t::cleanupTradingData(
    plug_data_t const &tradeMetadata, connector_t &connector) {
  if (tradeMetadata.tradeType == trade_type_e::futures &&
      m_lastAction == trade_action_e::nothing)
  {
    m_lastQuantity = tradeMetadata.tradeConfig->size * 2.0;
  }
  m_lastAction = tradeMetadata.tradeConfig->side;

  double price = 0.0;
  QString errorString;

  if (tradeMetadata.exchange == exchange_name_e::kucoin) {
    auto &kcRequest = *connector.kucoinTrader;
    auto const quantityPurchased = kcRequest.quantityPurchased();
    auto const sizePurchased = kcRequest.sizePurchased();
    if (quantityPurchased != 0.0 && sizePurchased != 0.0) {
      price = (quantityPurchased / sizePurchased) /
        tradeMetadata.tradeConfig->multiplier;
    }
    errorString = kcRequest.errorString();
    delete connector.kucoinTrader;
  } else if (tradeMetadata.exchange == exchange_name_e::binance) {
    auto &binanceRequest = *connector.binanceTrader;
    price = binanceRequest.averagePrice();
    errorString = binanceRequest.errorString();
    delete connector.binanceTrader;
  } else if (tradeMetadata.exchange == exchange_name_e::ftx) {
    price = connector.ftxTrader->getAveragePrice();
    errorString = connector.ftxTrader->errorString();
    delete connector.ftxTrader;
    connector.ftxTrader = nullptr;
  }

  auto modelData = m_model->modelDataFor(
        tradeMetadata.correlatorID,
        korrelator::actionTypeToString(tradeMetadata.tradeConfig->side));

  if (modelData)
    modelData->exchangePrice = price;

  if (!errorString.isEmpty()) {
    if (tradeMetadata.tradeType == trade_type_e::futures) {
      m_lastAction = trade_action_e::nothing;
      m_lastQuantity /= 2.0;
    }
    if (modelData)
      modelData->remark = "Error: " + errorString;
  } else {
    if (modelData)
      modelData->remark = "Success";
  }
}

}
