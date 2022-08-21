#include "single_trader.hpp"

#include "binance_https_request.hpp"
#include "ftx_https_request.hpp"
#include "kucoin_https_request.hpp"
#include "order_model.hpp"

namespace korrelator {

  SingleTrader::SingleTrader(std::function<void()> refreshModelCallback,
                             std::unique_ptr<order_model> &model, int &maxRetries):
    m_ioContext(getExchangeIOContext()), m_sslContext(getSSLContext()),
    m_maxRetries(maxRetries), m_model(model),
    m_modelRefreshCallback(refreshModelCallback)
  {
  }

  void SingleTrader::operator()(plug_data_t &&tradeMetadata) {
    model_data_t* modelData = nullptr;

    if (m_model)
      modelData = m_model->front();

    // only when program is stopped
    if (tradeMetadata.tradeType == trade_type_e::unknown) {
      m_lastQuantity = NAN;
      m_futuresLeverageIsSet = false;
      m_isFirstTrade = true;
      m_lastAction = trade_action_e::nothing;
      m_ioContext.stop();
      return m_ioContext.restart();
    }

    if (m_isFirstTrade) {
      m_isFirstTrade = false;
      if (tradeMetadata.tradeType == trade_type_e::spot &&
          tradeMetadata.tradeConfig->side == trade_action_e::sell) {
        if (modelData)
          modelData->remark = "[Order Ignored] First spot trade cannot be "
                              "a SELL";
        return m_modelRefreshCallback();
      }
    }

    if (m_lastAction != trade_action_e::nothing &&
      tradeMetadata.tradeType == trade_type_e::futures) {
      tradeMetadata.tradeConfig->size = m_lastQuantity;
    }

    auto const isKuCoin = tradeMetadata.exchange == exchange_name_e::kucoin;
    connector_t connector;
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

    m_ioContext.run();

    if (tradeMetadata.tradeType == trade_type_e::futures &&
        m_lastAction == trade_action_e::nothing)
    {
      m_lastQuantity = tradeMetadata.tradeConfig->size * 2.0;
    }
    m_lastAction = tradeMetadata.tradeConfig->side;

    double price = 0.0;
    QString errorString;

    if (isKuCoin) {
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

    m_modelRefreshCallback();
    m_ioContext.stop();
    m_ioContext.restart();
  }
}
