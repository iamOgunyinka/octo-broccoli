#pragma once

#include <QString>
#include <QMetaType>

namespace korrelator {

enum class trade_type_e { spot, futures, unknown };
enum class exchange_name_e { binance, kucoin, none };
enum class trade_action_e { buy, sell, do_nothing };
enum class market_type_e { market, limit, unknown };
enum tick_line_type_e { normal, ref, all, special };

struct api_data_t {
  QString spotApiKey;
  QString spotApiSecret;
  QString spotApiPassphrase;
  QString futuresApiKey;
  QString futuresApiSecret;
  QString futuresApiPassphrase;
};

struct trade_config_data_t {
  QString symbol;
  QString marketType;
  double baseAmount = 0.0;
  double size = 0.0;
  double leverage = 0.0;
  int8_t pricePrecision = -1;
  int8_t quantityPrecision = pricePrecision;
  int8_t baseAssetPrecision = pricePrecision;
  int8_t quotePrecision = pricePrecision;
  trade_action_e side = trade_action_e::do_nothing;
  trade_type_e tradeType = trade_type_e::unknown;
  exchange_name_e exchange = exchange_name_e::none;

  trade_config_data_t* contemporary = nullptr;
};

struct internal_address_t {
  QString tokenName;
  bool subscribed = false;
};

QString exchangeNameToString(korrelator::exchange_name_e const ex);
exchange_name_e stringToExchangeName(QString const &name);
market_type_e stringToMarketType(QString const &marketName);
QString marketTypeToString(market_type_e const);
char get_random_char();
std::string get_random_string(std::size_t);
std::size_t get_random_integer();
// QString roundToPrecision(double const value, int const precision);

} // namespace korrelator

Q_DECLARE_METATYPE(korrelator::exchange_name_e);
Q_DECLARE_METATYPE(korrelator::trade_type_e);
