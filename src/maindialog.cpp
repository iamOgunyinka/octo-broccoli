#include "maindialog.hpp"

#include "container.hpp"
#include "ui_maindialog.h"

#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>

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

static double maxVisiblePlot = 100.0;
static QTime time(QTime::currentTime());
static qint64 mapDrawnCount = 0;

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
    saveTokensToFile();
  });
  QObject::connect(ui->startButton, &QPushButton::clicked, this,
                   [this] { onOKButtonClicked(); });
  ui->restartTickLine->setValidator(new QIntValidator(1, 1'000'000));
  ui->timerTickCombo->addItems(
      {"100ms", "200ms", "500ms", "1 sec", "2 secs", "5 secs"});
  ui->selectionCombo->addItems({"Default(100 seconds)", "1 min", "2 mins",
                                "5 mins", "10 mins", "30 mins", "1 hr"});
  ui->legendPositionCombo->addItems(
      {"Top Left", "Top Right", "Bottom Left", "Bottom Right"});
  ui->restartTickCombo->addItems({"All symbol lines", "Ref", "All lines"});
}

MainDialog::~MainDialog() {
  stopGraphPlotting();
  resetGraphComponents();
  m_websocket.reset();
  saveTokensToFile();

  delete ui;
}

void MainDialog::enableUIComponents(bool const enabled) {
  ui->futuresNextButton->setEnabled(enabled);
  ui->spotNextButton->setEnabled(enabled);
  ui->spotPrevButton->setEnabled(enabled);
  ui->futuresCombo->setEnabled(enabled);
  ui->legendPositionCombo->setEnabled(enabled);
  ui->selectionCombo->setEnabled(enabled);
  ui->spotCombo->setEnabled(enabled);
  ui->timerTickCombo->setEnabled(enabled);
  ui->refCheckBox->setEnabled(enabled);
  ui->restartTickCombo->setEnabled(enabled);
  ui->restartTickLine->setEnabled(enabled);
}

void MainDialog::stopGraphPlotting() {
  m_timerPlot.stop();
  ui->futuresNextButton->setEnabled(true);
  ui->spotNextButton->setEnabled(true);
  ui->spotPrevButton->setEnabled(true);
  ui->startButton->setText("Start");

  m_graphUpdater.worker.reset();
  m_graphUpdater.thread.reset();
  m_priceUpdater.worker.reset();
  m_priceUpdater.thread.reset();

  m_programIsRunning = false;
  enableUIComponents(true);
}

int MainDialog::getTimerTickMilliseconds() const {
  switch (ui->timerTickCombo->currentIndex()) {
  case 0:
    return 100;
  case 1:
    return 200;
  case 2:
    return 500;
  case 3:
    return 1'000;
  case 4:
    return 2'000;
  case 5:
  default:
    return 5'000;
  }
}

double MainDialog::getMaxPlotsInVisibleRegion() const {
  switch (ui->selectionCombo->currentIndex()) {
  case 1:
    return 60.0;
  case 2:
    return 120.0;
  case 3:
    return 300.0;
  case 4:
    return 600.0;
  case 5:
    return 1'800.0;
  case 0:
  default:
    return 100.0;
  }
}

void updateTokenIter(brocolli::token_map_t::iterator iter, double const price) {

  auto &value = iter->second;
  if (value.calculatingNewMinMax){
    value.minPrice = price * 0.75;
    value.maxPrice = price * 1.25;
    value.calculatingNewMinMax = false;
  }

  value.minPrice = std::min(value.minPrice, price);
  value.maxPrice = std::max(value.maxPrice, price);
  value.normalizedPrice =
      (price - value.minPrice) / (value.maxPrice - value.minPrice);
}

void MainDialog::onNewPriceReceived(QString const &tokenName,
                                    double const price, int const tt) {
  auto const token = QString::number(tt) + tokenName;
  auto iter = m_tokens.find(token);
  if (iter != m_tokens.end()) {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    updateTokenIter(iter, price);
  }
  iter = m_refs.find(token);
  if (iter != m_refs.end()) {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    updateTokenIter(iter, price);
  }
}

void MainDialog::newItemAdded(QString const &tokenName,
                              brocolli::trade_type_e const tt) {
  auto const specialTokenName = QString::number((int)tt) + tokenName.toLower();
  bool const isRef = ui->refCheckBox->isChecked();
  if (isRef) {
    if (m_tokens.find("ref") == m_tokens.end()) {
      auto &info = m_tokens["ref"];
      info.tokenName = "ref";
      info.calculatingNewMinMax = true;
      info.tradeType = tt;
    }

    if (m_refs.find(specialTokenName) == m_refs.end()) {
      auto &info = m_refs[specialTokenName];
      info.tokenName = specialTokenName;
      info.calculatingNewMinMax = true;
      info.tradeType = tt;
    }
  } else {
    if (m_tokens.find(specialTokenName) == m_tokens.end()) {
      auto &info = m_tokens[specialTokenName];
      info.tokenName = specialTokenName;
      info.calculatingNewMinMax = true;
      info.tradeType = tt;
    }
  }
}

QPair<QString, brocolli::trade_type_e>
tokenNameFromWidgetName(QString specialTokenName) {
  if (specialTokenName.endsWith('*'))
    specialTokenName.chop(1);
  if (specialTokenName.contains("_SPOT"))
    return {specialTokenName.chopped(5), brocolli::trade_type_e::spot};
  return {specialTokenName.chopped(8), brocolli::trade_type_e::futures};
}

void MainDialog::tokenRemoved(QString const &text) {
  auto const &d = tokenNameFromWidgetName(text);
  auto const &tokenName = d.first.toLower();
  auto const tradeType = d.second;
  auto const specialTokenName = QString::number((int)tradeType) + tokenName;
  bool const isRef = text.endsWith('*');
  auto &tokenMap = isRef ? m_refs : m_tokens;

  if (auto iter = tokenMap.find(specialTokenName); iter != tokenMap.end())
    tokenMap.erase(iter);

  if (m_refs.empty()) {
    if (auto iter = m_tokens.find("ref"); iter != m_tokens.end())
      m_tokens.erase(iter);
  }

  if (m_websocket && ui->tokenListWidget->count() == 0)
    m_websocket.reset();
}

void MainDialog::getSpotsTokens(callback_t cb) {
  if (cb)
    return sendNetworkRequest(QUrl(spots_tokens_url), cb);

  auto callback = [this](brocolli::token_list_t &&list) {
    ui->spotCombo->clear();
    for (auto const &d : list)
      ui->spotCombo->addItem(d.tokenName.toUpper());
    attemptFileRead();
  };
  sendNetworkRequest(QUrl(spots_tokens_url), callback);
}

void MainDialog::getFuturesTokens(callback_t cb) {
  if (cb)
    return sendNetworkRequest(QUrl(futures_tokens_url), cb);

  auto callback = [this](brocolli::token_list_t &&list) {
    ui->futuresCombo->clear();
    for (auto const &d : list)
      ui->futuresCombo->addItem(d.tokenName.toUpper());
    attemptFileRead();
  };
  sendNetworkRequest(QUrl(futures_tokens_url), callback);
}

void MainDialog::attemptFileRead() {
  if ((ui->futuresCombo->count() != 0) && (ui->spotCombo->count() != 0))
    readTokensFromFile();
}

void MainDialog::saveTokensToFile() {
  if (ui->tokenListWidget->count() == 0)
    return;

  QFile file{"brocolli.json"};
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return;

  QJsonArray jsonList;
  for (int i = 0; i < ui->tokenListWidget->count(); ++i) {
    QListWidgetItem *item = ui->tokenListWidget->item(i);
    QJsonObject obj;
    auto const displayedName = item->text();
    if (auto const &d = tokenNameFromWidgetName(displayedName);
        !d.first.isEmpty()) {
      obj["symbol"] = d.first.toLower();
      obj["market"] =
          d.second == brocolli::trade_type_e::spot ? "spot" : "futures";
      obj["ref"] = displayedName.endsWith('*');
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
    jsonList = QJsonDocument::fromJson(file.readAll()).array();
  }

  if (jsonList.isEmpty())
    return;

  auto const refPreValue = ui->refCheckBox->isChecked();
  for (int i = 0; i < jsonList.size(); ++i) {
    QJsonObject const obj = jsonList[i].toObject();
    auto const tokenName = obj["symbol"].toString().toUpper();
    auto const tradeType = obj["market"].toString().toLower() == "spot"
                               ? brocolli::trade_type_e::spot
                               : brocolli::trade_type_e::futures;
    auto const isRef = obj["ref"].toBool();
    ui->refCheckBox->setChecked(isRef);
    addNewItemToTokenMap(tokenName, tradeType);
  }
  ui->refCheckBox->setChecked(refPreValue);
}

void MainDialog::addNewItemToTokenMap(QString const &tokenName,
                                      brocolli::trade_type_e const tt) {
  auto const text =
      tokenName.toUpper() +
      (tt == brocolli::trade_type_e::spot ? "_SPOT" : "_FUTURES") +
      (ui->refCheckBox->isChecked() ? "*" : "");
  for (int i = 0; i < ui->tokenListWidget->count(); ++i) {
    QListWidgetItem *item = ui->tokenListWidget->item(i);
    if (text == item->text())
      return;
  }
  ui->tokenListWidget->addItem(text);
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
          t.normalizedPrice = tokenObject.value("price").toString().toDouble();
          tokenList.push_back(std::move(t));
        }

        std::sort(tokenList.begin(), tokenList.end(),
                  brocolli::token_compare_t{});
        cb(std::move(tokenList));
      });

  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}

Qt::Alignment MainDialog::getLegendAlignment() const {
  switch (ui->legendPositionCombo->currentIndex()) {
  case 0:
    return Qt::AlignTop | Qt::AlignLeft;
  case 1:
    return Qt::AlignTop | Qt::AlignRight;
  case 2:
    return Qt::AlignBottom | Qt::AlignLeft;
  case 3:
  default:
    return Qt::AlignBottom | Qt::AlignRight;
  }
}

void MainDialog::resetGraphComponents() {
  {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    for (auto &[_, value] : m_tokens) {
      if (value.graph && value.graph->data())
        value.graph->data()->clear();
      value.graph = nullptr;
      value.normalizedPrice = NAN;
    }
  }

  ui->customPlot->clearGraphs();
  ui->customPlot->clearPlottables();
  ui->customPlot->legend->clearItems();
}

void MainDialog::setupGraphData() {
  static Qt::GlobalColor const colors[] = {
      Qt::red,      Qt::green,    Qt::blue,        Qt::magenta,
      Qt::cyan,     Qt::black,    Qt::darkGray,    Qt::darkGreen,
      Qt::darkBlue, Qt::darkCyan, Qt::darkMagenta, Qt::darkYellow};

  /* Add graph and set the curve lines color */
  {
    auto legendName = [](QString const &key) -> QString {
      return (key.mid(1).toUpper() + (key[0] == '0' ? "_SPOT" : "_FUTURES"));
    };
    int i = 0;
    for (auto &[key, value] : m_tokens) {
      value.graph = ui->customPlot->addGraph();
      auto const color = colors[i % (sizeof(colors) / sizeof(colors[0]))];
      value.graph->setPen(QPen(color));
      value.graph->setAntialiasedFill(true);
      value.legendName = (key == "ref") ? key : legendName(key);
      value.graph->setName(value.legendName);
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
      0, getLegendAlignment());
  ui->customPlot->legend->setBrush(QColor(255, 255, 255, 0));

  /* Configure x and y-Axis to display Labels */
  ui->customPlot->xAxis->setTickLabelFont(QFont(QFont().family(), 8));
  ui->customPlot->yAxis->setTickLabelFont(QFont(QFont().family(), 8));
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
  ui->customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom |
                                  QCP::iSelectPlottables);
  ui->customPlot->xAxis->setTickLabelSide(QCPAxis::LabelSide::lsOutside);
}

void MainDialog::onOKButtonClicked() {
  if (m_programIsRunning)
    return stopGraphPlotting();

  m_tickerResetNumber = -1;
  m_isResettingTickers = false;
  m_programIsRunning = true;
  brocolli::maxVisiblePlot = getMaxPlotsInVisibleRegion();
  brocolli::mapDrawnCount = 0;

  if (auto const resetTickerStr = ui->restartTickLine->text().trimmed();
      !resetTickerStr.isEmpty()) {
    bool isOK = true;
    int const restartNumber = resetTickerStr.toInt(&isOK);
    if (restartNumber <= 0 || !isOK) {
      QMessageBox::critical(
            this, "Error",
            "Something is wrong with the restart tick number, please check");
      return ui->restartTickLine->setFocus();
    }
    m_tickerResetNumber = restartNumber;
    m_isResettingTickers = true;
  }

  ui->startButton->setText("Stop");
  resetGraphComponents();
  enableUIComponents(false);
  setupGraphData();

  auto normalizePrice = [](auto& map, auto const &result, auto const tt)
  {
    for (auto &[key, value]: map) {
      if (value.tradeType != tt || key == "ref")
        continue;
      auto const tokenName = key.mid(1).toLower();
      auto iter = std::lower_bound(result.begin(), result.end(),
                                   tokenName, brocolli::token_compare_t{});
      if (iter != result.end()) {
        auto const price = iter->normalizedPrice;
        value.minPrice = price * 0.75;
        value.maxPrice = price * 1.25;
        value.normalizedPrice = (price - value.minPrice) /
            (value.maxPrice - value.minPrice);
        value.calculatingNewMinMax = false;
      }
    }
  };

  getSpotsTokens([this, normalizePrice](brocolli::token_list_t && result) {
    normalizePrice(m_tokens, result, brocolli::trade_type_e::spot);
    normalizePrice(m_refs, result, brocolli::trade_type_e::spot);

    getFuturesTokens([this, normalizePrice](brocolli::token_list_t &&result) {
      normalizePrice(m_tokens, result, brocolli::trade_type_e::futures);
      normalizePrice(m_refs, result, brocolli::trade_type_e::futures);
      startWebsocket();
    });
  });
}

void MainDialog::startWebsocket() {
  // price updater
  m_priceUpdater.worker = std::make_unique<brocolli::Worker>([this] {
    m_websocket = std::make_unique<brocolli::cwebsocket>();
    QObject::connect(
          m_websocket.get(), &brocolli::cwebsocket::newPriceReceived,
          m_priceUpdater.worker.get(),
          [this](QString const &tokenName, double price, int const t) {
      onNewPriceReceived(tokenName, price, t);
    });
    for (auto const &ref: m_refs)
      m_websocket->subscribe(ref.first.mid(1), ref.second.tradeType);

    for (auto const& token: m_tokens)
      if (token.first != "ref")
        m_websocket->subscribe(token.first.mid(1), token.second.tradeType);
  });

  m_priceUpdater.thread.reset(new QThread);
  QObject::connect(m_priceUpdater.thread.get(), &QThread::started,
                   m_priceUpdater.worker.get(),
                   &brocolli::Worker::startWork);
  m_priceUpdater.worker->moveToThread(m_priceUpdater.thread.get());
  m_priceUpdater.thread->start();

  // graph updater
  m_graphUpdater.worker = std::make_unique<brocolli::Worker>([this] {
    QMetaObject::invokeMethod(this, [this] {
      /* Set up and initialize the graph plotting timer */
      QObject::connect(&m_timerPlot, &QTimer::timeout,
                       m_graphUpdater.worker.get(),
                       [this] { realTimePlot(); });
      auto const timerTick = getTimerTickMilliseconds();
      brocolli::time.restart();
      m_timerPlot.start(timerTick);
    });
  });
  m_graphUpdater.thread.reset(new QThread);
  QObject::connect(m_graphUpdater.thread.get(), &QThread::started,
                   m_graphUpdater.worker.get(),
                   &brocolli::Worker::startWork);
  m_graphUpdater.worker->moveToThread(m_graphUpdater.thread.get());
  m_graphUpdater.thread->start();
}

void MainDialog::realTimePlot() {
  static char const *const legendDisplayFormat = "%1(%2)";
  auto const key = brocolli::time.elapsed() / 1'000.0;
  double minValue = 0.0, maxValue = 0.0;

  ++brocolli::mapDrawnCount;

  {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    minValue = maxValue = m_tokens.begin()->second.normalizedPrice;
    auto const keyStart =
        (key >= brocolli::maxVisiblePlot ? (key - brocolli::maxVisiblePlot)
                                         : 0.0);
    QCPRange const range(keyStart, key);
    for (auto const &[mapKey, value] : m_tokens) {
      double price = value.normalizedPrice;
      if (mapKey.size() == 3 && mapKey[0] == 'r' && mapKey[2] == 'f') { // ref
        price = 0.0;
        for (auto const &[_, v] : m_refs)
          price += v.normalizedPrice;
        price /= ((double)m_refs.size());
      }

      value.graph->addData(key, price);
      bool foundInRange = false;
      auto const visibleValueRange =
          value.graph->getValueRange(foundInRange, QCP::sdBoth, range);
      if (foundInRange) {
        minValue = std::min(std::min(minValue, visibleValueRange.lower), price);
        maxValue = std::max(std::max(maxValue, visibleValueRange.upper), price);
      } else {
        minValue = std::min(minValue, price);
        maxValue = std::max(maxValue, price);
      }

      value.graph->setName(QString(legendDisplayFormat)
                               .arg(value.legendName)
                               .arg(brocolli::mapDrawnCount));
    }

    if (m_isResettingTickers &&
        brocolli::mapDrawnCount == m_tickerResetNumber) {
      brocolli::mapDrawnCount = 0;
      resetTickerData();
    }
  }

  auto const diff = (maxValue - minValue) / 19.0;
  minValue -= diff;
  maxValue += diff;

  // make key axis range scroll right with the data at a constant range of 100
  ui->customPlot->xAxis->setRange(key, brocolli::maxVisiblePlot,
                                  Qt::AlignRight);
  ui->customPlot->yAxis->setRange(minValue, maxValue);
  ui->customPlot->replot(QCustomPlot::RefreshPriority::rpQueuedReplot);
}

void MainDialog::resetTickerData() {
  auto resetMap = [](auto &map) {
    for(auto& [_, value]: map)
      value.calculatingNewMinMax = true;
  };

  // no need to lock the mutex here, it's been locked before this function
  // is called.

  auto const resetType =
      (brocolli::ticker_reset_type_e)ui->restartTickCombo->currentIndex();
  switch(resetType){
  case brocolli::ticker_reset_type_e::non_ref_symbols:
    return resetMap(m_tokens);
  case brocolli::ticker_reset_type_e::ref_symbols:
    return resetMap(m_refs);
  case brocolli::ticker_reset_type_e::both:
    resetMap(m_refs);
    return resetMap(m_tokens);
  default:
    Q_ASSERT(false);
  }
}
