#pragma once

#include <QNetworkAccessManager>
#include "constants.hpp"
#include "tokens.hpp"

namespace korrelator {

class binance_symbols
{
  using exchange_info_callback_t = std::function<void()>;
public:
  binance_symbols(QNetworkAccessManager& networkManager);

  void getSpotsExchangeInfo(token_list_t*, exchange_info_callback_t, error_callback_t);
  void getFuturesExchangeInfo(token_list_t*, exchange_info_callback_t, error_callback_t);
  void getFuturesSymbols(success_callback_t, error_callback_t);
  void getSpotsSymbols(success_callback_t, error_callback_t);

private:
  void sendNetworkRequest(QString const &url, trade_type_e const tradeType,
                          success_callback_t, error_callback_t);

  QNetworkAccessManager& m_networkManager;
};

void configureRequestForSSL(QNetworkRequest &request);
}
