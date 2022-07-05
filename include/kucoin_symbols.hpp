#pragma once

#include <QNetworkAccessManager>
#include "tokens.hpp"

namespace korrelator {

class kucoin_symbols {
public:
  kucoin_symbols(QNetworkAccessManager&);

  void getFuturesSymbols(success_callback_t, error_callback_t);
  void getSpotsSymbols(success_callback_t, error_callback_t);
  void getSpotsExchangeInfo(token_list_t*, error_callback_t);
  void getFuturesExchangeInfo(token_list_t*, error_callback_t){}
private:

  void sendNetworkRequest(QString const &url, trade_type_e const tradeType,
                          success_callback_t, error_callback_t);

  QNetworkAccessManager & m_networkManager;
};

void configureRequestForSSL(QNetworkRequest &request);

}
