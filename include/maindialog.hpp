#pragma once

#include <QDialog>
#include <QListWidgetItem>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QTimer>
#include <memory>
#include <optional>

#include "order_model.hpp"
#include "settingsdialog.hpp"
#include "sthread.hpp"
#include "tokens.hpp"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainDialog;
}
QT_END_NAMESPACE

class QCPGraph;
class QCPLayoutGrid;

namespace korrelator {

class websocket_manager;

struct graph_updater_t {
  worker_ptr worker = nullptr;
  cthread_ptr thread = nullptr;
};

struct price_updater_t {
  worker_ptr worker = nullptr;
  cthread_ptr thread = nullptr;
};

struct ref_calculation_data_t {
  bool isResettingRef = false;
  bool eachTickNormalize = false;
  double minValue = std::numeric_limits<double>::max();
  double maxValue = -minValue;
};

enum class ticker_reset_type_e { non_ref_symbols, ref_symbols, both };

struct watchable_data_t {
  korrelator::token_list_t spots;
  korrelator::token_list_t futures;
};

} // namespace korrelator

using korrelator::exchange_name_e;
using korrelator::trade_type_e;

class MainDialog : public QDialog {
  Q_OBJECT

  using callback_t =
      std::function<void(korrelator::token_list_t &&, exchange_name_e)>;
  using list_iterator = korrelator::token_list_t::iterator;

signals:
  void newOrderDetected(korrelator::cross_over_data_t, korrelator::model_data_t,
                        korrelator::exchange_name_e const,
                        korrelator::trade_type_e const);

public:
  MainDialog(QWidget *parent = nullptr);
  ~MainDialog();

private:
  void onSettingsDialogClicked();
  void registerCustomTypes();
  void getExchangeInfo(exchange_name_e const, trade_type_e const);
  void getKuCoinExchangeInfo(trade_type_e const);
  void getSpotsTokens(exchange_name_e const, callback_t = nullptr);
  void getFuturesTokens(exchange_name_e const, callback_t = nullptr);
  void sendNetworkRequest(QUrl const &url, callback_t, trade_type_e const,
                          exchange_name_e const);
  void newItemAdded(QString const &token, trade_type_e const,
                    exchange_name_e const);
  void tokenRemoved(QString const &text);
  void onTimerTick();
  void onOKButtonClicked();
  void stopGraphPlotting();
  void saveTokensToFile();
  void readTokensFromFile();
  void readOrdersConfigFromFile();
  void addNewItemToTokenMap(QString const &name, trade_type_e const,
                            exchange_name_e const);
  void enableUIComponents(bool const);
  void resetGraphComponents();
  void setupGraphData();
  void resetTickerData(bool const resetRefs, bool const resetSymbols);
  void startWebsocket();
  void getInitialTokenPrices();
  void populateUIComponents();
  void connectAllUISignals();
  bool validateUserInput();
  void generateJsonFile(korrelator::model_data_t const &);
  void onApplyButtonClicked();
  int  getTimerTickMilliseconds() const;
  double getIntegralValue(QLineEdit *lineEdit);
  double getMaxPlotsInVisibleRegion() const;
  void updateGraphData(double const key, bool const);
  void setupOrderTableModel();
  void updateTradeConfigurationPrecisions();
  void onNewOrderDetected(korrelator::cross_over_data_t,
                          korrelator::model_data_t,
                          exchange_name_e const,
                          trade_type_e const);
  void calculatePriceNormalization();
  Qt::Alignment getLegendAlignment() const;
  list_iterator find(korrelator::token_list_t &container, QString const &,
                     trade_type_e const, exchange_name_e const);
  list_iterator find(korrelator::token_list_t &container, QString const &);
  korrelator::trade_action_e lineCrossedOver(double const prevRef,
                                             double const currRef,
                                             double const prevValue,
                                             double const currValue);
  korrelator::ref_calculation_data_t updateRefGraph(double const keyStart,
                                                    double const keyEnd,
                                                    bool const updateGraph);

  void sendExchangeRequest(korrelator::model_data_t const &,
                           exchange_name_e const, trade_type_e const tradeType,
                           korrelator::cross_over_data_t const &);
  static void tradeBinanceToken();
  static void tradeKuCoinToken();

private:
  Ui::MainDialog *ui;

  QNetworkAccessManager m_networkManager;
  std::unique_ptr<korrelator::websocket_manager> m_websocket = nullptr;
  std::unique_ptr<korrelator::order_model> m_model = nullptr;

  QMap<int, korrelator::watchable_data_t> m_watchables;
  SettingsDialog::api_data_map_t m_apiTradeApiMap;
  korrelator::token_list_t m_tokens;
  korrelator::token_list_t m_refs;
  std::vector<korrelator::trade_config_data_t> m_tradeConfigDataList;
  korrelator::graph_updater_t m_graphUpdater;
  korrelator::price_updater_t m_priceUpdater;
  std::unique_ptr<QCPLayoutGrid> m_legendLayout = nullptr;
  QTimer m_timerPlot;
  korrelator::trade_action_e m_lastTradeAction;
  double m_threshold = 0.0;
  double m_specialRef = std::numeric_limits<double>::max();
  double m_resetPercentage = m_specialRef;
  bool m_findingSpecialRef = false;
  bool m_doingManualReset = false;
  bool m_programIsRunning = false;
  bool m_findingUmbral = false; // umbral is spanish word for threshold
  bool m_hasReferences = false;
};

Q_DECLARE_METATYPE(korrelator::cross_over_data_t);
