#pragma once

#include <QNetworkAccessManager>
#include "tokens.hpp"

namespace korrelator {

class ftx_symbols
{
public:
  ftx_symbols(QNetworkAccessManager& networkManager);
  void getSpotsExchangeInfo(token_list_t*, error_callback_t);
  void getFuturesExchangeInfo(token_list_t*, error_callback_t);
  void getFuturesSymbols(success_callback_t, error_callback_t);
  void getSpotsSymbols(success_callback_t, error_callback_t);

private:
  void sendNetworkRequest(QString const &url, trade_type_e const,
                          success_callback_t, error_callback_t);

  QNetworkAccessManager& m_networkManager;
};

void configureRequestForSSL(QNetworkRequest &request);
}

