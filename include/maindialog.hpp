#pragma once

#include <QDialog>
#include <QListWidgetItem>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QTimer>
#include <memory>
#include <optional>

#include "cwebsocket.hpp"
#include "order_model.hpp"
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
  void newOrderDetected(korrelator::cross_over_data_t const &,
                        korrelator::model_data_t const &);

public:
  MainDialog(QWidget *parent = nullptr);
  ~MainDialog();

private:
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
  void generateJsonFile(korrelator::cross_over_data_t const &,
                        korrelator::model_data_t const &);
  void onApplyButtonClicked();
  int getTimerTickMilliseconds() const;
  double getIntegralValue(QLineEdit *lineEdit);
  double getMaxPlotsInVisibleRegion() const;
  void updateGraphData(double const key, bool const);
  void setupOrderTableModel();
  void onNewPriceReceived(QString const &, double const price,
                          exchange_name_e const exchange, trade_type_e const tt);
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

private:
  Ui::MainDialog *ui;

  QNetworkAccessManager m_networkManager;
  korrelator::cwebsocket_ptr m_websocket = nullptr;
  std::unique_ptr<korrelator::order_model> m_model = nullptr;

  QMap<int, korrelator::watchable_data_t> m_watchables;
  korrelator::token_list_t m_tokens;
  korrelator::token_list_t m_refs;

  korrelator::token_list_t::iterator m_refIterator;
  std::mutex m_mutex;

  korrelator::graph_updater_t m_graphUpdater;
  korrelator::price_updater_t m_priceUpdater;
  std::unique_ptr<QCPLayoutGrid> m_legendLayout = nullptr;
  QTimer m_timerPlot;
  korrelator::exchange_name_e m_currentExchange;
  double m_threshold = 0.0;

  double m_specialRef = std::numeric_limits<double>::max();
  bool m_findingSpecialRef = false;

  bool m_programIsRunning = false;
  bool m_findingUmbral = false; // umbral is spanish word for threshold
  bool m_hasReferences = false;
};

Q_DECLARE_METATYPE(korrelator::cross_over_data_t);
