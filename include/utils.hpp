#pragma once

#include <QString>
#include <QMetaType>

namespace korrelator {

struct internal_address_t {
  QString tokenName;
  bool subscribed = false;
};

enum class trade_type_e { spot, futures };
enum class exchange_name_e { binance, kucoin, none };
enum class trade_action_e { buy, sell, do_nothing };
enum tick_line_type_e { normal, ref, all, special };

} // namespace korrelator

Q_DECLARE_METATYPE(korrelator::exchange_name_e);
Q_DECLARE_METATYPE(korrelator::trade_type_e);
