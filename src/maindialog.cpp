#include "maindialog.hpp"

#include "container.hpp"
#include "ui_maindialog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFileDialog>

#include "qcustomplot.h"

static char const *const futures_tokens_url =
    "https://fapi.binance.com/fapi/v1/ticker/price";
static char const *const spots_tokens_url =
    "https://api.binance.com/api/v3/ticker/price";

namespace brocolli {

QSslConfiguration getSSLConfig() {
  auto ssl_config = QSslConfiguration::defaultConfiguration();
  ssl_config.setProtocol(QSsl::TlsV1_2OrLater);
  return ssl_config;
}

void configureRequestForSSL(QNetworkRequest &request) {
  request.setSslConfiguration(getSSLConfig());
}

} // namespace brocolli

MainDialog::MainDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::MainDialog) {
  ui->setupUi(this);
  setWindowIcon(qApp->style()->standardPixmap(QStyle::SP_DesktopIcon));
  setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint |
                 Qt::WindowMaximizeButtonHint);
  getSpotsTokens();
  getFuturesTokens();

  QObject::connect(ui->spotNextButton, &QToolButton::clicked, this, [this] {
    addNewItemToTokenMap(ui->spotCombo->currentText(),
                         brocolli::trade_type_e::spot);
    saveTokensToFile();
  });

  QObject::connect(ui->futuresNextButton, &QToolButton::clicked, this, [this] {
    addNewItemToTokenMap(ui->futuresCombo->currentText(),
                         brocolli::trade_type_e::futures);
    saveTokensToFile();
  });

  QObject::connect(ui->spotPrevButton, &QToolButton::clicked, this, [this] {
    auto currentRow = ui->tokenListWidget->currentRow();
    if (currentRow < 0 || currentRow >= ui->tokenListWidget->count())
      return;
    auto item = ui->tokenListWidget->takeItem(currentRow);
    tokenRemoved(item->text());
    delete item;
    delete ui->priceListWidget->takeItem(currentRow);
    saveTokensToFile();
  });
  QObject::connect(ui->startButton, &QPushButton::clicked, this, [this] {
    startGraphPlotting();
  });
}

MainDialog::~MainDialog() {
  stopGraphPlotting();
  m_websocket.reset();
  saveTokensToFile();

  delete ui;
}

void MainDialog::stopGraphPlotting() {
  m_timerPlot.stop();
  m_programIsRunning = false;
  ui->futuresNextButton->setEnabled(true);
  ui->spotNextButton->setEnabled(true);
  ui->spotPrevButton->setEnabled(true);
  ui->startButton->setText("Start");

  for (auto&v: m_tokens)
    v.second.minPrice = v.second.maxPrice = -1.0;
}

void MainDialog::newItemAdded(QString const &tokenName,
                              brocolli::trade_type_e const tt) {
  if (!m_websocket) {
    m_websocket = std::make_unique<brocolli::cwebsocket>();
    QObject::connect(m_websocket.get(),
                     &brocolli::cwebsocket::newPriceReceived, this,
                     [this](QString const &tokenName, double price, int const t)
    {
      auto const token = QString::number(t) + tokenName;
      auto iter = m_tokens.find(token);
      if (m_tokens.end() != iter) {
        std::lock_guard<std::mutex> lock_g{m_mutex};
        iter->second.item->setText(QString::number(price, 'f', 9));
        auto& value = iter->second;
        if (value.minPrice < 0.0) {
          value.minPrice = price * 0.75;
          value.maxPrice = price * 1.25;
        }

        value.normalizedPrice = (price - value.minPrice) /
            (value.maxPrice - value.minPrice);

        value.maxPrice = std::max(value.maxPrice, price);
        value.minPrice = std::min(value.minPrice, price);
      }
    });
  }

  auto const specialTokenName = QString::number((int)tt) + tokenName.toLower();
  if (auto iter = m_tokens.find(specialTokenName); iter == m_tokens.end()) {
    auto&info = m_tokens[specialTokenName];
    info.tokenName = specialTokenName;
    info.item = ui->priceListWidget->item(ui->priceListWidget->count() - 1);
    info.minPrice = info.maxPrice = -1.0;
  }

  m_websocket->subscribe(tokenName, tt);
}

QPair<QString, brocolli::trade_type_e>
tokenNameFromWidgetName(QString const &specialTokenName) {
  if (specialTokenName.lastIndexOf(" (SPOT)") != -1) {
    return {specialTokenName.chopped(7),
          brocolli::trade_type_e::spot};
  }
  return { specialTokenName.chopped(10),
        brocolli::trade_type_e::futures};
}

void MainDialog::tokenRemoved(QString const &text) {
  QString specialTokenName;
  auto const &d = tokenNameFromWidgetName(text);
  auto const tokenName = d.first.toLower();
  auto const tradeType = d.second;

  if (brocolli::trade_type_e::spot == tradeType)
    specialTokenName = QString::number((int)tradeType) + tokenName;
  else
    specialTokenName = QString::number((int)tradeType) + tokenName;

  auto iter = m_tokens.find(specialTokenName);
  Q_ASSERT(iter != m_tokens.end());
  if (iter != m_tokens.end()) {
    m_tokens.erase(iter);
    if (m_websocket)
      m_websocket->unsubscribe(tokenName, tradeType);
  }

  if (m_websocket && ui->tokenListWidget->count() == 0)
    m_websocket.reset();
}

void MainDialog::getSpotsTokens() {
  auto callback = [this](brocolli::token_list_t &&list) {
    ui->spotCombo->clear();
    for (auto const &d : list)
      ui->spotCombo->addItem(d.tokenName.toUpper());
    attemptFileRead();
  };
  auto const url = QUrl(spots_tokens_url);
  sendNetworkRequest(url, callback);
}

void MainDialog::getFuturesTokens() {
  auto const url = QUrl(futures_tokens_url);
  auto callback = [this](brocolli::token_list_t &&list) {
    ui->futuresCombo->clear();
    for (auto const &d : list)
      ui->futuresCombo->addItem(d.tokenName.toUpper());
    attemptFileRead();
  };
  sendNetworkRequest(url, callback);

}

void MainDialog::attemptFileRead() {
  if((ui->futuresCombo->count() != 0) && (ui->spotCombo->count() != 0))
    readTokensFromFile();
}

void MainDialog::saveTokensToFile() {
  if (ui->priceListWidget->count())
    return;
  QFile file{"brocolli.json"};
  if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate))
    return;

  QJsonArray jsonList;
  for (int i = 0; i < ui->tokenListWidget->count(); ++i) {
    QListWidgetItem *item = ui->tokenListWidget->item(i);
    QJsonObject obj;
    if (auto const &d = tokenNameFromWidgetName(item->text());
        d.first.isEmpty() )
    {
      obj["symbol"] = d.first.toLower();
      obj["market"] = d.second == brocolli::trade_type_e::spot ?
            "spot": "futures";
      obj["ref"] = false;
      jsonList.append(obj);
    }
  }
  file.write(QJsonDocument(jsonList).toJson());
}

void MainDialog::readTokensFromFile() {
  QJsonArray jsonList;
  {
    QFile file{"brocolli.json"};
    if (!file.open(QIODevice::ReadOnly))
      return;
    jsonList = QJsonDocument::fromJson(
          file.readAll()).array();
  }

  if (jsonList.isEmpty())
    return;
  for (int i = 0; i < jsonList.size(); ++i) {
    QJsonObject const obj = jsonList[i].toObject();
    auto const tokenName = obj["symbol"].toString().toUpper();
    auto const tradeType = obj["market"].toString().toLower() == "spot"
        ? brocolli::trade_type_e::spot : brocolli::trade_type_e::futures;
    addNewItemToTokenMap(tokenName, tradeType);
  }
}

void MainDialog::addNewItemToTokenMap(QString const &tokenName,
                                      brocolli::trade_type_e const tt) {
  auto const text = tokenName.toUpper() +
      (tt == brocolli::trade_type_e::spot ? " (SPOT)": " (FUTURES)");
  for (int i = 0; i < ui->tokenListWidget->count(); ++i) {
    QListWidgetItem *item = ui->tokenListWidget->item(i);
    if (text == item->text())
      return;
  }
  ui->tokenListWidget->addItem(text);
  ui->priceListWidget->addItem("");
  newItemAdded(tokenName.toLower(), tt);
}

void MainDialog::sendNetworkRequest(
    QUrl const &url,
    std::function<void(brocolli::token_list_t &&)> onSuccessCallback) {
  QNetworkRequest request(url);
  brocolli::configureRequestForSSL(request);
  m_networkManager.enableStrictTransportSecurityStore(true);

  auto reply = m_networkManager.get(request);
  QObject::connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, cb = std::move(onSuccessCallback)] {
        if (reply->error() != QNetworkReply::NoError) {
          QMessageBox::critical(this, "Error",
                                "Unable to get the list of "
                                "all token pairs => " +
                                    reply->errorString());
          return;
        }
        auto const responseString = reply->readAll();
        auto const jsonResponse = QJsonDocument::fromJson(responseString);
        if (jsonResponse.isEmpty()) {
          QMessageBox::critical(this, "Error",
                                "Unable to read the response sent");
          return;
        }
        auto const list = jsonResponse.array();
        brocolli::token_list_t tokenList;
        tokenList.reserve(list.size());

        for (auto const &token : list) {
          auto const tokenObject = token.toObject();
          brocolli::token_t t;
          t.tokenName = tokenObject.value("symbol").toString().toLower();
          tokenList.push_back(std::move(t));
        }

        std::sort(tokenList.begin(), tokenList.end(),
                  brocolli::token_compare_t{});
        cb(std::move(tokenList));
      });

  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}

void MainDialog::startGraphPlotting() {
  if (m_programIsRunning)
    return stopGraphPlotting();

  if (ui->priceListWidget->count() < 1) {
    QMessageBox::critical(this, "Error", "You need to add at least one token");
    return;
  }

  static Qt::GlobalColor const colors[] = {
    Qt::red, Qt::green, Qt::blue, Qt::yellow, Qt::magenta,
    Qt::cyan, Qt::black, Qt::darkGray, Qt::darkGreen,
    Qt::darkBlue, Qt::darkCyan, Qt::darkMagenta, Qt::darkYellow
  };

  for (auto &[_, value]: m_tokens)
    value.graph = nullptr;

  ui->customPlot->clearGraphs();
  ui->customPlot->clearPlottables();

  m_programIsRunning = true;
  ui->startButton->setText("Stop");

  ui->customPlot->legend->clearItems();
  auto legendName = [](QString const &key) ->QString {
    return (key.mid(1).toUpper() + (key[0] == '0' ? " (SPOT)": " (FUTURES)"));
  };
  /* Add graph and set the curve lines color */
  {
    int i = 0;
    for (auto& [key, value]: m_tokens) {
      value.graph = ui->customPlot->addGraph();
      auto const color = colors[i % (sizeof(colors)/sizeof(colors[0]))];
      value.graph->setPen(QPen(color));
      value.graph->setAntialiasedFill(true);
      value.graph->setName(legendName(key));
      value.graph->addToLegend();
      value.graph->setLineStyle(QCPGraph::lsLine);
      ++i;
    }
  }

  /* Configure x-Axis as time in secs */
  QSharedPointer<QCPAxisTickerTime> timeTicker(new QCPAxisTickerTime);
  timeTicker->setTimeFormat("%m:%s");
  timeTicker->setTickCount(10);

  ui->customPlot->xAxis->setTicker(timeTicker);
  ui->customPlot->axisRect()->setupFullAxesBox();
  ui->customPlot->setAutoAddPlottableToLegend(true);
  ui->customPlot->legend->setVisible(true);
  ui->customPlot->axisRect()->insetLayout()->setInsetAlignment(
        0, Qt::AlignBottom|Qt::AlignLeft);
  ui->customPlot->legend->setBrush(QColor(255, 255, 255, 0));

  /* Configure x and y-Axis to display Labels */
  ui->customPlot->xAxis->setTickLabelFont(QFont(QFont().family(),8));
  ui->customPlot->yAxis->setTickLabelFont(QFont(QFont().family(),8));
  ui->customPlot->xAxis->setLabel("Time(s)");
  ui->customPlot->yAxis->setLabel("Prices");
  ui->customPlot->legend->setBorderPen(Qt::NoPen);

  /* Make top and right axis visible, but without ticks and label */
  ui->customPlot->xAxis2->setVisible(false);
  ui->customPlot->xAxis2->setTicks(false);
  ui->customPlot->yAxis2->setVisible(false);
  ui->customPlot->yAxis->setVisible(true);
  ui->customPlot->yAxis->ticker()->setTickCount(10);
  ui->customPlot->setStyleSheet(("background:hsva(255, 255, 255, 0%);"));
  ui->customPlot->setInteractions(
        QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
  ui->customPlot->xAxis->setTickLabelSide(QCPAxis::LabelSide::lsOutside);

  ui->futuresNextButton->setEnabled(false);
  ui->spotNextButton->setEnabled(false);
  ui->spotPrevButton->setEnabled(false);

  m_worker = std::make_unique<brocolli::Worker>([this]{
    QMetaObject::invokeMethod(this, [this] {
      /* Set up and initialize the graph plotting timer */
      QObject::connect(&m_timerPlot, &QTimer::timeout, m_worker.get(), [this] {
        realTimePlot();
      });
      m_timerPlot.start(100);
    });
  });

  m_thread.reset(new QThread);
  QObject::connect(m_thread.get(), &QThread::started, m_worker.get(),
                   &brocolli::Worker::startWork);
  m_worker->moveToThread(m_thread.get());
  m_thread->start();
}

// this is the timer tick
void MainDialog::realTimePlot() {
  static const double maxVisiblePlot = 100.0;
  static QTime time(QTime::currentTime());

  auto const key = time.elapsed() / 1000.0;
  double minValue = 0.0, maxValue = 0.0;

  {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    minValue = maxValue = m_tokens.begin()->second.normalizedPrice;
    auto const keyStart = (key >= maxVisiblePlot ?
                             (key - maxVisiblePlot) : 0.0);
    QCPRange const range(keyStart, key);
    for (auto& [_, value]: m_tokens) {
      value.graph->addData(key, value.normalizedPrice);
      bool foundInRange = false;
      auto const f = value.graph->getValueRange(foundInRange, QCP::sdBoth, range);
      if (foundInRange) {
        minValue = std::min(std::min(minValue, f.lower), value.normalizedPrice);
        maxValue = std::max(std::max(maxValue, f.upper), value.normalizedPrice);
      } else {
        minValue = std::min(minValue, value.normalizedPrice);
        maxValue = std::max(maxValue, value.normalizedPrice);
      }
    }
  }

  auto const diff = (maxValue - minValue) / 17.0;
  minValue -= diff;
  maxValue += diff;

  // make key axis range scroll right with the data at a constant range of 100
  ui->customPlot->xAxis->setRange(key, maxVisiblePlot, Qt::AlignRight);
  ui->customPlot->yAxis->setRange(minValue, maxValue);
  ui->customPlot->replot(QCustomPlot::RefreshPriority::rpQueuedReplot);
}
