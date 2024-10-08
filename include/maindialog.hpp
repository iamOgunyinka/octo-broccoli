#pragma once

#include <QDialog>
#include <QListWidget>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
#include <optional>
#include <filesystem>

#include "order_model.hpp"
#include "settingsdialog.hpp"
#include "sthread.hpp"
#include "tokens.hpp"
#include "container.hpp"
#include "plug_data.hpp"

#define CMAX_DOUBLE_VALUE std::numeric_limits<double>::max()

QT_BEGIN_NAMESPACE
namespace Ui {
class MainDialog;
}
QT_END_NAMESPACE

class QCPGraph;
class QCPLayoutGrid;
class QCustomPlot;

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
  double minValue = CMAX_DOUBLE_VALUE;
  double maxValue = - (CMAX_DOUBLE_VALUE);
};

struct watchable_data_t {
  korrelator::token_list_t spots;
  korrelator::token_list_t futures;
};

enum class order_origin_e {
  from_price_normalization,
  from_price_average,
  from_both,
  from_none
};

class binance_symbols;
class kucoin_symbols;

struct symbol_fetcher_t {
  std::unique_ptr<binance_symbols> binance;
  std::unique_ptr<kucoin_symbols> kucoin;
  symbol_fetcher_t();
  ~symbol_fetcher_t();
};

struct rot_metadata_t {
  double restartOnTickEntry = 0.0;
  double percentageEntry = 0.0;
  double specialEntry = 0.0;

  double afterDivisionPercentageEntry = 0.0;
  double afterDivisionSpecialEntry = 0.0;
};

struct rot_t {
  std::optional<rot_metadata_t> normalLines;
  std::optional<rot_metadata_t> refLines;
  std::optional<rot_metadata_t> special;
};

} // namespace korrelator

using korrelator::exchange_name_e;
using korrelator::trade_type_e;

class MainDialog final: public QDialog {
  Q_OBJECT

  using callback_t =
      std::function<void(korrelator::token_list_t &&, exchange_name_e)>;
  using list_iterator = korrelator::token_list_t::iterator;

signals:
  void newOrderDetected(korrelator::cross_over_data_t, korrelator::model_data_t,
                        korrelator::exchange_name_e const,
                        korrelator::trade_type_e const);

  void newPriceDeltaOrderDetected(
      korrelator::cross_over_data_t, korrelator::model_data_t,
      korrelator::exchange_name_e const, korrelator::trade_type_e const);

public:
  MainDialog(bool& warnOnExit, std::filesystem::path const configDirectory,
             QWidget *parent = nullptr);
  ~MainDialog();
  void openPreferenceWindow() { onSettingsDialogClicked(); }
  void reloadTradeConfig() { readTradesConfigFromFile(); }

  void closeEvent(QCloseEvent*) override;
  void reject() override {} // ignore all ESC press

  void refreshModel(){
    if (m_model)
      m_model->refreshModel();
  }

private:
  void onSettingsDialogClicked();
  void registerCustomTypes();
  void getExchangeInfo(exchange_name_e const, trade_type_e const);
  void getSpotsTokens(exchange_name_e const, callback_t = nullptr);
  void getFuturesTokens(exchange_name_e const, callback_t = nullptr);
  void newItemAdded(QString const &token, trade_type_e const,
                    exchange_name_e const);
  void tokenRemoved(QString const &text);
  void onNormalizedGraphTimerTick(bool const minMaxNeedsUpdate);
  void onPriceDeltaGraphTimerTick(bool const minMaxNeedsUpdate);
  void takeBackToFactoryReset();
  void onOKButtonClicked();
  void onStartVerificationSuccessful();
  void stopGraphPlotting(bool const requestConfirmation);
  void saveAppConfigToFile();
  void readAppConfigFromFile();
  void updateKuCoinTradeConfiguration();
  void readTradesConfigFromFile();
  bool setRestartTickRowValues(std::optional<korrelator::rot_metadata_t> &);
  void addNewItemToTokenMap(QString const &name, trade_type_e const,
                            exchange_name_e const);
  void enableUIComponents(bool const);
  void resetGraphComponents();
  void setupNormalizedGraphData();
  void setupPriceDeltaGraphData();
  void resetTickerData(bool const resetRefs, bool const resetSymbols);
  void startWebsocket();
  void priceLaunchImpl();
  void getInitialTokenPrices();
  void populateUIComponents();
  void connectAllUISignals();
  void connectRestartTickSignal();
  bool validateUserInput();
  void generateJsonFile(korrelator::model_data_t const &);
  void onApplyButtonClicked();
  int  getTimerTickMilliseconds() const;
  std::optional<double> getIntegralValue(QLineEdit *lineEdit);
  double getMaxPlotsInVisibleRegion() const;
  void updateGraphData(double const key, bool const);
  void setupOrderTableModel();
  void calculateAveragePriceDifference();
  void updateTradeConfigurationPrecisions();
  void onNewOrderDetected(korrelator::cross_over_data_t,
                          korrelator::model_data_t,
                          exchange_name_e const,
                          trade_type_e const,
                          korrelator::order_origin_e const origin);
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

  void sendExchangeRequest(korrelator::model_data_t &,
                           exchange_name_e const, trade_type_e const tradeType,
                           korrelator::trade_action_e const, double const,
                           korrelator::order_origin_e const origin);
  korrelator::trade_config_data_t* getTradeInfo(
      exchange_name_e const exchange, trade_type_e const tradeType,
      korrelator::trade_action_e const action,
      korrelator::order_origin_e const tradeOrigin,
      QString const &symbol);
  bool onSingleTradeInfoGenerated(korrelator::trade_config_data_t*,
                                  korrelator::api_data_t const &apiInfo,
                                  double const openPrice);
  void onDoubleTradeInfoGenerated(
      korrelator::order_origin_e const tradeOrigin,
      korrelator::trade_config_data_t *tradeConfigPtr,
                                  korrelator::api_data_t const &apiInfo,
                                  double const openPrice);
  void makePriceAverageOrder(korrelator::trade_action_e const tradeAction,
                             korrelator::trade_type_e const tradeType);
  void onExportButtonClicked();
  void OnTradeAverageRadioToggled(bool const);
  void OnTradeBothAverageNormalToggled(bool const);
  void OnTradeNormalizedPriceToggled(bool const);
  void OnMaxVisibleTimeTimedOut();
  void ConnectAllTradeRadioSignals(bool const);
  void updatePlottingKey();

  static void tradeExchangeTokens(
      std::function<void()> refreshModel,
      korrelator::waitable_container_t<korrelator::plug_data_t>&,
      std::unique_ptr<korrelator::order_model>&, int& maxRetries,
      int& expectedTradeCount);

private:
  using trade_config_list_t = std::vector<korrelator::trade_config_data_t>;
  struct average_order_data_t {
    trade_config_list_t dataList;
    korrelator::trade_action_e futuresLastAction;
    korrelator::trade_action_e spotsLastAction;
  };

  struct normalized_order_data_t {
    trade_config_list_t dataList;
    korrelator::trade_action_e lastTradeAction;
  };

  struct plot_graph_data_t {
    std::mutex mutex;
    QTimer timer;
  };

  Ui::MainDialog *ui;
  QListWidget* m_currentListWidget;
  QTimer *m_averagePriceDifferenceTimer = nullptr;
  QNetworkAccessManager m_networkManager;
  std::unique_ptr<korrelator::websocket_manager> m_websocket;
  std::unique_ptr<korrelator::order_model> m_model = nullptr;
  QMap<int, korrelator::watchable_data_t> m_watchables;
  SettingsDialog::api_data_map_t m_apiTradeApiMap;
  korrelator::token_list_t m_tokens;
  korrelator::token_list_t m_refs;
  korrelator::token_list_t m_priceDeltas;
  korrelator::graph_updater_t m_graphUpdater;
  korrelator::price_updater_t m_priceUpdater;
  korrelator::symbol_fetcher_t m_symbolUpdater;
  std::unique_ptr<QCPLayoutGrid> m_legendLayout;
  korrelator::waitable_container_t<korrelator::plug_data_t> m_tokenPlugs;
  plot_graph_data_t m_graphPlotter;
  QTimer m_timerPlot;
  QTimer m_maxVisibleTimeTimer;
  QElapsedTimer m_elapsedTime;
  korrelator::rot_t m_restartTickValues;
  std::filesystem::path const m_configDirectory;
  std::optional<normalized_order_data_t> m_normalizationOrderData = std::nullopt;
  std::optional<average_order_data_t> m_priceAverageOrderData = std::nullopt;
  double m_lastGraphPoint = 0.0;
  double m_threshold = 0.0;
  double m_maxVisiblePlot = 100.0;
  double m_lastKeyUsed = 0.0;
  double m_lastPriceAverage = 0.0;
  double m_maxAverageThreshold = 0.0;
  double m_averageUp = 0.0;
  double m_averageDown = 0.0;

  int m_maxOrderRetries = 10;
  int m_expectedTradeCount = 1; // max 2
  korrelator::order_origin_e m_orderOrigin = korrelator::order_origin_e::from_none;

  bool m_doingAutoLDClosure = false; // automatic "line distance" (LD) closure
  bool m_doingManualLDClosure = false; // manualInterval LD closure
  bool m_programIsRunning = false;
  bool m_firstRun = true;
  bool m_findingUmbral = false; // umbral is spanish word for threshold
  bool m_hasReferences = false;
  bool m_tradeOpened = false;
  bool m_calculatingNormalPrice = true;
  bool m_calculatingPriceAverage = false;
  bool& m_warnOnExit;
};

Q_DECLARE_METATYPE(korrelator::cross_over_data_t);
