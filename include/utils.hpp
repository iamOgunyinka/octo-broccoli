#pragma once

#include <QString>
#include <QMetaType>

namespace korrelator {

struct api_data_t {
  QString spotApiKey;
  QString spotApiSecret;
  QString spotApiPassphrase;
  QString futuresApiKey;
  QString futuresApiSecret;
  QString futuresApiPassphrase;
};

struct internal_address_t {
  QString tokenName;
  bool subscribed = false;
};

enum class trade_type_e { spot, futures };
enum class exchange_name_e { binance, kucoin, none };
enum class trade_action_e { buy, sell, do_nothing };
enum tick_line_type_e { normal, ref, all, special };

QString exchangeNameToString(korrelator::exchange_name_e const ex);
korrelator::exchange_name_e stringToExchangeName(QString const &name);
} // namespace korrelator

Q_DECLARE_METATYPE(korrelator::exchange_name_e);
Q_DECLARE_METATYPE(korrelator::trade_type_e);
