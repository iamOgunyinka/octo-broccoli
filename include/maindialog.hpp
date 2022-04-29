#pragma once

#include <QDialog>
#include <QNetworkAccessManager>
#include <QListWidgetItem>
#include <QTimer>
#include <memory>

#include "cwebsocket.hpp"

QT_BEGIN_NAMESPACE
namespace Ui { class MainDialog; }
QT_END_NAMESPACE

namespace brocolli {
struct token_t {
  double minPrice = 0.0;
  double maxPrice = 0.0;
  double price = 0.0;
  QListWidgetItem* item = nullptr;
  QString tokenName;
};

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

private:
  Ui::MainDialog *ui;

  QNetworkAccessManager m_networkManager;
  brocolli::cwebsocket_ptr m_websocket = nullptr;
  brocolli::token_list_t m_spotData;
  brocolli::token_list_t m_futuresData;
  QTimer m_timerPlot;
};
