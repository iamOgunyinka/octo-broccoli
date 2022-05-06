#pragma once

#include <QDialog>
#include <QNetworkAccessManager>
#include <QListWidgetItem>
#include <QTimer>
#include <memory>

#include "cwebsocket.hpp"
#include "sthread.hpp"

QT_BEGIN_NAMESPACE
namespace Ui { class MainDialog; }
QT_END_NAMESPACE

class QCPGraph;
class QCPLayoutGrid;

namespace brocolli {
struct token_t {
  double minPrice = -1.0;
  double maxPrice = -1.0;
  double normalizedPrice = 0.0;
  QString tokenName;
  QString legendName;
  QListWidgetItem* item = nullptr;
  QCPGraph* graph = nullptr;
};

using token_map_t = std::map<QString, brocolli::token_t>;

struct token_compare_t {
  bool operator()(QString const &tokenName, token_t const & t) const {
    return tokenName < t.tokenName;
  }
  bool operator()(token_t const & t, QString const &tokenName) const {
    return t.tokenName < tokenName;
  }
  bool operator()(token_t const & a, token_t const &b) const {
    return a.tokenName < b.tokenName;
  }
};

using token_list_t = std::vector<token_t>;

}

class MainDialog : public QDialog
{
  Q_OBJECT

public:
  MainDialog(QWidget *parent = nullptr);
  ~MainDialog();

private:
  void getSpotsTokens();
  void getFuturesTokens();
  void sendNetworkRequest(
      QUrl const &url,
      std::function<void(brocolli::token_list_t &&)>);
  void newItemAdded(QString const &token, brocolli::trade_type_e const);
  void tokenRemoved(QString const &text);
  void realTimePlot();
  void onOKButtonClicked();
  void stopGraphPlotting();
  void saveTokensToFile();
  void readTokensFromFile();
  void addNewItemToTokenMap(QString const &name, brocolli::trade_type_e const);
  void attemptFileRead();
  void enableUIComponents(bool const);
  void resetGraphComponents();
  void setupGraphData();
  void onNewPriceReceived(QString const &, double const price,
                          int const tt);
  int  getTimerTickMilliseconds() const;
  double getMaxPlotsInVisibleRegion() const;
  Qt::Alignment getLegendAlignment() const;

private:
  Ui::MainDialog *ui;

  QNetworkAccessManager m_networkManager;
  brocolli::cwebsocket_ptr m_websocket = nullptr;
  brocolli::token_map_t m_tokens;
  brocolli::token_map_t m_refs;
  std::mutex m_mutex;

  brocolli::worker_ptr m_worker = nullptr;
  brocolli::cthread_ptr m_thread = nullptr;
  std::unique_ptr<QCPLayoutGrid> m_legendLayout = nullptr;
  QTimer m_timerPlot;
  bool m_programIsRunning = false;
};
