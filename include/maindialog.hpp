#pragma once

#include <QDialog>
#include <QNetworkAccessManager>
#include <QListWidgetItem>
#include <QTimer>
#include <QMetaType>
#include <memory>
#include <optional>

#include "cwebsocket.hpp"
#include "order_model.hpp"
#include "sthread.hpp"

QT_BEGIN_NAMESPACE
namespace Ui { class MainDialog; }
QT_END_NAMESPACE

class QCPGraph;
class QCPLayoutGrid;

namespace korrelator {

enum trade_action_e {
  buy, sell, do_nothing
};

struct cross_over_data_t {
  double price = 0.0;
  trade_action_e action = trade_action_e::do_nothing;
  QString time;
  cross_over_data_t() = default;
};

class token_t {
public:

  double minPrice = std::numeric_limits<double>::max();
  double maxPrice = -1 * std::numeric_limits<double>::max();
  double prevNormalizedPrice = std::numeric_limits<double>::max();
  double normalizedPrice = 0.0;
  double realPrice = 0.0;
  std::optional<cross_over_data_t> crossOver;
  QString tokenName;
  QString legendName;
  mutable QCPGraph* graph = nullptr;
  bool calculatingNewMinMax = true;
  bool crossedOver = false;
  trade_type_e tradeType;

  void reset();
};

using token_map_t = std::map<QString, korrelator::token_t>;

struct token_compare_t {
  bool operator()(QString const &tokenName, token_t const & t) const {
    return tokenName < t.tokenName;
  }
  bool operator()(token_t const & t, QString const &tokenName) const {
    return t.tokenName < tokenName;
  }
  bool operator()(token_t const & a, token_t const &b) const {
    return std::tie(a.tokenName, a.tradeType) <
        std::tie(b.tokenName, b.tradeType);
  }
};

struct graph_updater_t {
  worker_ptr worker = nullptr;
  cthread_ptr thread = nullptr;
};

struct price_updater_t {
  worker_ptr worker = nullptr;
  cthread_ptr thread = nullptr;
};

using token_list_t = std::vector<token_t>;

enum class ticker_reset_type_e {
  non_ref_symbols,
  ref_symbols,
  both
};
}

class MainDialog : public QDialog
{
  Q_OBJECT

  using callback_t = std::function<void(korrelator::token_list_t &&)>;
  using list_iterator = korrelator::token_list_t::iterator;

signals:
  void newOrderDetected(korrelator::cross_over_data_t const &,
                        korrelator::model_data_t const &);

public:
  MainDialog(QWidget *parent = nullptr);
  ~MainDialog();

private:
  void getSpotsTokens(callback_t = nullptr);
  void getFuturesTokens(callback_t = nullptr);
  void sendNetworkRequest(QUrl const &url, callback_t);
  void newItemAdded(QString const &token, korrelator::trade_type_e const);
  void tokenRemoved(QString const &text);
  void onTimerTick();
  void onOKButtonClicked();
  void stopGraphPlotting();
  void saveTokensToFile();
  void readTokensFromFile();
  void addNewItemToTokenMap(QString const &name, korrelator::trade_type_e const);
  void attemptFileRead();
  void enableUIComponents(bool const);
  void resetGraphComponents();
  void setupGraphData();
  void resetTickerData();
  void startWebsocket();
  void getInitialTokenPrices();
  void onNewPriceReceived(QString const &, double const price,
                          korrelator::trade_type_e const tt);
  void generateJsonFile(korrelator::cross_over_data_t const &,
                        korrelator::model_data_t const &);
  int  getTimerTickMilliseconds() const;
  double getIntegralValue(QLineEdit* lineEdit);
  double getMaxPlotsInVisibleRegion() const;
  void updateGraphData(double const key, bool const);
  Qt::Alignment getLegendAlignment() const;
  list_iterator find(korrelator::token_list_t& container, QString const &,
                     korrelator::trade_type_e const);
  list_iterator find(korrelator::token_list_t& container, QString const &);
  korrelator::trade_action_e lineCrossedOver(
      double const prevRef, double const currRef,
      double const prevValue, double const currValue);
  QString actionTypeToString(korrelator::trade_action_e a) const {
    if (a == korrelator::trade_action_e::buy)
      return "BUY";
    return "SELL";
  }

private:
  Ui::MainDialog *ui;

  QNetworkAccessManager m_networkManager;
  korrelator::cwebsocket_ptr m_websocket = nullptr;
  std::unique_ptr<korrelator::order_model> m_model = nullptr;
  korrelator::token_list_t m_tokens;
  korrelator::token_list_t m_refs;
  korrelator::token_list_t::iterator m_refIterator;
  std::mutex m_mutex;

  korrelator::graph_updater_t m_graphUpdater;
  korrelator::price_updater_t m_priceUpdater;
  std::unique_ptr<QCPLayoutGrid> m_legendLayout = nullptr;
  QTimer m_timerPlot;
  double m_tickerResetNumber;
  double m_threshold = 0.0;
  bool m_programIsRunning = false;
  bool m_isResettingTickers = false;
  bool m_findingUmbral = false; // umbral is spanish word for threshold
  bool m_hasReferences = false;
};

Q_DECLARE_METATYPE(korrelator::cross_over_data_t);
