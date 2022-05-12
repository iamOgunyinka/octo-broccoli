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
#include "order_model.hpp"

static char const *const futures_tokens_url =
    "https://fapi.binance.com/fapi/v1/ticker/price";
static char const *const spots_tokens_url =
    "https://api.binance.com/api/v3/ticker/price";

static constexpr const double maxDoubleValue =
    std::numeric_limits<double>::max();

namespace korrelator {

QSslConfiguration getSSLConfig() {
  auto ssl_config = QSslConfiguration::defaultConfiguration();
  ssl_config.setProtocol(QSsl::TlsV1_2OrLater);
  return ssl_config;
}

void configureRequestForSSL(QNetworkRequest &request) {
  request.setSslConfiguration(getSSLConfig());
}

void token_t::reset() {
  if (graph && graph->data())
    graph->data()->clear();
  graph = nullptr;
  calculatingNewMinMax = true;
  crossOver.reset();
  minPrice = prevNormalizedPrice = maxDoubleValue;
  maxPrice = -maxDoubleValue;
  normalizedPrice = realPrice = 0.0;
  crossedOver = false;
}

static double maxVisiblePlot = 100.0;
static QTime time(QTime::currentTime());
static double lastPoint = 0.0;
static std::vector<std::optional<double>> restartTickValues{};
} // namespace korrelator


MainDialog::MainDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::MainDialog) {
  ui->setupUi(this);

  qRegisterMetaType<korrelator::model_data_t>();
  qRegisterMetaType<korrelator::cross_over_data_t>();

  setWindowIcon(qApp->style()->standardPixmap(QStyle::SP_DesktopIcon));
  setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint |
                 Qt::WindowMaximizeButtonHint);

  getSpotsTokens();
  getFuturesTokens();

  QObject::connect(ui->spotNextButton, &QToolButton::clicked, this, [this] {
    addNewItemToTokenMap(ui->spotCombo->currentText(),
                         korrelator::trade_type_e::spot);
    saveTokensToFile();
  });

  ui->umbralLine->setValidator(new QDoubleValidator);
  QObject::connect(ui->futuresNextButton, &QToolButton::clicked, this, [this] {
    addNewItemToTokenMap(ui->futuresCombo->currentText(),
                         korrelator::trade_type_e::futures);
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

  QObject::connect(this, &MainDialog::newOrderDetected, this,
                   &MainDialog::generateJsonFile);

  QObject::connect(ui->selectionCombo,
                   static_cast<void(QComboBox::*)(int)>(
                   &QComboBox::currentIndexChanged),
                   this,  [this](int const) {
    korrelator::maxVisiblePlot = getMaxPlotsInVisibleRegion();
    if (!m_programIsRunning) {
      double const key = korrelator::time.elapsed() / 1'000.0;
      ui->customPlot->xAxis->setRange(key, korrelator::maxVisiblePlot,
                                      Qt::AlignRight);
      ui->customPlot->replot();
    }
  });

  QObject::connect(ui->startButton, &QPushButton::clicked, this,
                   [this] { onOKButtonClicked(); });
  ui->restartTickLine->setValidator(new QIntValidator(1, 1'000'000));
  ui->timerTickCombo->addItems(
      {"100ms", "200ms", "500ms", "1 sec", "2 secs", "5 secs"});
  ui->selectionCombo->addItems({"Default(100 seconds)", "1 min", "2 mins",
                                "5 mins", "10 mins", "30 mins", "1 hr", "2 hrs",
                                "3 hrs", "5 hrs"});
  ui->legendPositionCombo->addItems(
      {"Top Left", "Top Right", "Bottom Left", "Bottom Right"});
  ui->umbralLine->setText("5");

  QObject::connect(ui->applyButton, &QPushButton::clicked, this, [this] {
    auto const index = ui->restartTickCombo->currentIndex();
    auto const value = getIntegralValue(ui->restartTickLine);
    if (isnan(value))
      return;

    if (value != maxDoubleValue) {
      korrelator::restartTickValues[index].emplace(value);
      if (index == 2){
        korrelator::restartTickValues[0].emplace(value);
        korrelator::restartTickValues[1].emplace(value);
      }
    }
    ui->restartTickLine->setFocus();
  });

  korrelator::restartTickValues.clear();
  for (int i = 0; i <= 2; ++i)
    korrelator::restartTickValues.push_back(2'500);
  QObject::connect(ui->restartTickCombo,
                   static_cast<void(QComboBox::*)(int)>(
                   &QComboBox::currentIndexChanged),
                   this,  [this](int const index)
  {
    auto &optionalValue = korrelator::restartTickValues[index];
    if (optionalValue)
      ui->restartTickLine->setText(QString::number(optionalValue.value()));
    else
      ui->restartTickLine->clear();
    ui->restartTickLine->setFocus();
  });
  ui->restartTickCombo->addItems({"Normal lines", "Ref line", "All lines"});
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
  ui->spotCombo->setEnabled(enabled);
  ui->timerTickCombo->setEnabled(enabled);
  ui->refCheckBox->setEnabled(enabled);
  ui->restartTickCombo->setEnabled(enabled);
  ui->restartTickLine->setEnabled(enabled);
  ui->umbralLine->setEnabled(enabled);
  ui->applyButton->setEnabled(enabled);
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
  case 0:
    return 100.0;
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
  case 6:
    return 60.0 * 60.0;
  case 7:
    return 2.0 * 60.0 * 60.0;
  case 8:
    return 3.0 * 60.0 * 60.0;
  case 9:
  default:
    return 5.0 * 60.0 * 60.0;
  }
}

void updateTokenIter(korrelator::token_list_t::iterator iter,
                     double const price) {
  auto &value = *iter;
  if (value.calculatingNewMinMax) {
    value.minPrice = price * 0.75;
    value.maxPrice = price * 1.25;
    value.calculatingNewMinMax = false;
  }

  value.minPrice = std::min(value.minPrice, price);
  value.maxPrice = std::max(value.maxPrice, price);
  value.normalizedPrice =
      (price - value.minPrice) / (value.maxPrice - value.minPrice);
  value.realPrice = price;
}

MainDialog::list_iterator MainDialog::find(korrelator::token_list_t &container,
                                           QString const &tokenName,
                                           korrelator::trade_type_e const tt) {
  return std::find_if(container.begin(), container.end(),
                      [&tokenName, tt](korrelator::token_t const &a) {
                        return a.tokenName.compare(tokenName,
                                                   Qt::CaseInsensitive) == 0 &&
                               tt == a.tradeType;
                      });
}

MainDialog::list_iterator MainDialog::find(korrelator::token_list_t &container,
                                           QString const &tokenName) {
  return std::find_if(container.begin(), container.end(),
                      [&tokenName](korrelator::token_t const &a) {
                        return tokenName.compare(a.tokenName,
                                                 Qt::CaseInsensitive) == 0;
                      });
}

void MainDialog::onNewPriceReceived(QString const &tokenName,
                                    double const price,
                                    korrelator::trade_type_e const tt) {
  auto iter = find(m_tokens, tokenName, tt);
  if (iter != m_tokens.end()) {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    updateTokenIter(iter, price);
  }

  iter = find(m_refs, tokenName, tt);
  if (iter != m_refs.end()) {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    updateTokenIter(iter, price);
  }
}

void MainDialog::newItemAdded(QString const &tokenName,
                              korrelator::trade_type_e const tt) {
  bool const isRef = ui->refCheckBox->isChecked();
  if (isRef) {
    if (find(m_tokens, "*") == m_tokens.end()) {
      korrelator::token_t token;
      token.tokenName = "*";
      token.calculatingNewMinMax = true;
      token.tradeType = tt;
      token.normalizedPrice = maxDoubleValue;
      m_tokens.push_back(std::move(token));
    }

    if (find(m_refs, tokenName, tt) == m_refs.end()) {
      korrelator::token_t token;
      token.tradeType = tt;
      token.tokenName = tokenName;
      token.calculatingNewMinMax = true;
      token.tradeType = tt;
      m_refs.push_back(std::move(token));
    }
  } else {
    if (find(m_tokens, tokenName, tt) == m_tokens.end()) {
      korrelator::token_t token;
      token.tokenName = tokenName;
      token.calculatingNewMinMax = true;
      token.tradeType = tt;
      m_tokens.push_back(std::move(token));
    }
  }
}

QPair<QString, korrelator::trade_type_e>
tokenNameFromWidgetName(QString specialTokenName) {
  if (specialTokenName.endsWith('*'))
    specialTokenName.chop(1);
  if (specialTokenName.contains("_SPOT"))
    return {specialTokenName.chopped(5), korrelator::trade_type_e::spot};
  return {specialTokenName.chopped(8), korrelator::trade_type_e::futures};
}

void MainDialog::tokenRemoved(QString const &text) {
  auto const &d = tokenNameFromWidgetName(text);
  auto const &tokenName = d.first.toLower();
  auto const tradeType = d.second;
  auto &tokenMap = text.endsWith('*') ? m_refs : m_tokens;

  if (auto iter = find(tokenMap, tokenName, tradeType); iter != tokenMap.end())
    tokenMap.erase(iter);

  if (m_refs.empty()) {
    if (auto iter = find(m_tokens, "*"); iter != m_tokens.end())
      m_tokens.erase(iter);
  }
}

void MainDialog::getSpotsTokens(callback_t cb) {
  if (cb)
    return sendNetworkRequest(QUrl(spots_tokens_url), cb);

  auto callback = [this](korrelator::token_list_t &&list) {
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

  auto callback = [this](korrelator::token_list_t &&list) {
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

  QFile file{"korrelator.json"};
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
          d.second == korrelator::trade_type_e::spot ? "spot" : "futures";
      obj["ref"] = displayedName.endsWith('*');
      jsonList.append(obj);
    }
  }
  file.write(QJsonDocument(jsonList).toJson());
}

void MainDialog::readTokensFromFile() {
  QJsonArray jsonList;
  {
    if (QFileInfo::exists("brocolli.json"))
      QFile::rename("brocolli.json", "korrelator.json");

    QFile file{"korrelator.json"};
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
                               ? korrelator::trade_type_e::spot
                               : korrelator::trade_type_e::futures;
    auto const isRef = obj["ref"].toBool();
    ui->refCheckBox->setChecked(isRef);
    addNewItemToTokenMap(tokenName, tradeType);
  }
  ui->refCheckBox->setChecked(refPreValue);
}

void MainDialog::addNewItemToTokenMap(QString const &tokenName,
                                      korrelator::trade_type_e const tt) {
  auto const text =
      tokenName.toUpper() +
      (tt == korrelator::trade_type_e::spot ? "_SPOT" : "_FUTURES") +
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
    std::function<void(korrelator::token_list_t &&)> onSuccessCallback) {
  QNetworkRequest request(url);
  korrelator::configureRequestForSSL(request);
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
        korrelator::token_list_t tokenList;
        tokenList.reserve(list.size());

        for (auto const &token : list) {
          auto const tokenObject = token.toObject();
          korrelator::token_t t;
          t.tokenName = tokenObject.value("symbol").toString().toLower();
          t.normalizedPrice = tokenObject.value("price").toString().toDouble();
          tokenList.push_back(std::move(t));
        }

        std::sort(tokenList.begin(), tokenList.end(),
                  korrelator::token_compare_t{});
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
    for (auto &value : m_tokens)
      value.reset();
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
    auto legendName = [](QString const &key,
                         korrelator::trade_type_e const tt) {
      if (key.size() == 1 && key[0] == '*') // "ref"
        return QString("ref");
      return (key.toUpper() +
              (tt == korrelator::trade_type_e::spot ? "_SPOT" : "_FUTURES"));
    };
    int i = 0;
    for (auto &value : m_tokens) {
      value.graph = ui->customPlot->addGraph();
      auto const color = colors[i % (sizeof(colors) / sizeof(colors[0]))];
      value.graph->setPen(QPen(color));
      value.graph->setAntialiasedFill(true);
      value.legendName = legendName(value.tokenName, value.tradeType);
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

void MainDialog::getInitialTokenPrices() {
  auto normalizePrice = [](auto &list, auto const &result, auto const tt) {
    for (auto listIter = list.begin(); listIter != list.end(); ++listIter) {
      auto& value = *listIter;
      if (value.tradeType != tt || value.tokenName.size() == 1)
        continue;
      auto iter =
          std::lower_bound(result.begin(), result.end(), value.tokenName,
                           korrelator::token_compare_t{});
      if (iter != result.end()) {
        listIter->calculatingNewMinMax = true;
        updateTokenIter(listIter, iter->normalizedPrice);
      }
    }
  };

  // get prices of symbols that are on the SPOT "list" first
  getSpotsTokens([this, normalizePrice](korrelator::token_list_t &&result) {
    normalizePrice(m_tokens, result, korrelator::trade_type_e::spot);
    normalizePrice(m_refs, result, korrelator::trade_type_e::spot);

    // then get the prices of symbols on the FUTURES "list" next.
    getFuturesTokens([this, normalizePrice](korrelator::token_list_t &&result) {
      normalizePrice(m_tokens, result, korrelator::trade_type_e::futures);
      normalizePrice(m_refs, result, korrelator::trade_type_e::futures);

      if (m_hasReferences) {
        auto price = 0.0;
        for (auto const &t : m_refs)
          price += t.normalizedPrice;
        m_refIterator->normalizedPrice = (price / (double)m_refs.size());
      }

      // after successfully getting the SPOTs and FUTURES' prices,
      // start the websocket and get real-time updates.
      startWebsocket();
    });
  });
}

double MainDialog::getIntegralValue(QLineEdit *lineEdit) {
  if (auto const resetTickerStr = lineEdit->text().trimmed();
      !resetTickerStr.isEmpty()) {
    bool isOK = true;
    double const restartNumber = resetTickerStr.toDouble(&isOK);
    if (restartNumber < 0.0 || !isOK) {
      QMessageBox::critical(
          this, "Error",
          "Something is wrong with the input value, please check");
      lineEdit->setFocus();
      return NAN;
    }
    return restartNumber;
  }
  return maxDoubleValue;
}

void MainDialog::onOKButtonClicked() {
  if (m_programIsRunning)
    return stopGraphPlotting();

  m_threshold = getIntegralValue(ui->umbralLine);
  if (isnan(m_threshold))
    return;
  m_findingUmbral = m_threshold != maxDoubleValue;
  if (m_findingUmbral)
    m_threshold /= 100.0;

  m_programIsRunning = true;
  korrelator::maxVisiblePlot = getMaxPlotsInVisibleRegion();

  ui->startButton->setText("Stop");
  resetGraphComponents();
  enableUIComponents(false);
  setupGraphData();

  m_refIterator = m_tokens.end();
  m_hasReferences = false;

  if (auto iter = find(m_tokens, "*"); iter != m_tokens.end()) {
    if (iter != m_tokens.begin())
      std::iter_swap(iter, m_tokens.begin());
    m_refIterator = m_tokens.begin();
    m_hasReferences = true;
  }
  m_model = std::make_unique<korrelator::order_model>();
  ui->tableView->setModel(m_model.get());
  auto header = ui->tableView->horizontalHeader();
  header->setSectionResizeMode(QHeaderView::Stretch);
  ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
  header->setMaximumSectionSize(200);
  ui->tableView->setWordWrap(true);
  ui->tableView->resizeRowsToContents();
  getInitialTokenPrices();
}

void MainDialog::startWebsocket() {
  // price updater
  m_priceUpdater.worker = std::make_unique<korrelator::Worker>([this] {
    m_websocket = std::make_unique<korrelator::cwebsocket>();
    QObject::connect(
        m_websocket.get(), &korrelator::cwebsocket::newPriceReceived,
        m_priceUpdater.worker.get(),
        [this](QString const &tokenName, double price, int const t) {
          onNewPriceReceived(tokenName, price, (korrelator::trade_type_e)t);
        });
    for (auto const &ref : m_refs)
      m_websocket->addSubscription(ref.tokenName, ref.tradeType);

    for (auto const &token : m_tokens)
      if (token.tokenName.size() != 1)
        m_websocket->addSubscription(token.tokenName, token.tradeType);
    m_websocket->startWatch();
  });

  m_priceUpdater.thread.reset(new QThread);
  QObject::connect(m_priceUpdater.thread.get(), &QThread::started,
                   m_priceUpdater.worker.get(), &korrelator::Worker::startWork);
  m_priceUpdater.worker->moveToThread(m_priceUpdater.thread.get());
  m_priceUpdater.thread->start();

  // graph updater
  m_graphUpdater.worker = std::make_unique<korrelator::Worker>([this] {
    QMetaObject::invokeMethod(this, [this] {
      /* Set up and initialize the graph plotting timer */
      QObject::connect(&m_timerPlot, &QTimer::timeout,
                       m_graphUpdater.worker.get(), [this] { onTimerTick(); });
      korrelator::time.restart();
      korrelator::lastPoint = 0.0;
      auto const timerTick = getTimerTickMilliseconds();
      m_timerPlot.start(timerTick);
    });
  });

  m_graphUpdater.thread.reset(new QThread);
  QObject::connect(m_graphUpdater.thread.get(), &QThread::started,
                   m_graphUpdater.worker.get(), &korrelator::Worker::startWork);
  m_graphUpdater.worker->moveToThread(m_graphUpdater.thread.get());
  m_graphUpdater.thread->start();
}

korrelator::trade_action_e MainDialog::lineCrossedOver(double const prevA,
                                                       double const currA,
                                                       double const prevB,
                                                       double const currB) {
  // prevA -> previous ref, prevB -> previous symbol price
  // currA -> current ref, currB -> current symbol price
  if ((currA < currB) && (prevB < prevA))
    return korrelator::trade_action_e::buy;
  else if ((prevA < prevB) && (currB < currA))
    return korrelator::trade_action_e::sell;
  return korrelator::trade_action_e::do_nothing;
}

void MainDialog::updateGraphData(double const key, bool const updatingMinMax) {
  using korrelator::trade_action_e;

  static char const *const legendDisplayFormat = "%1(%2)";

  double const keyStart =
      (key >= korrelator::maxVisiblePlot ? (key - korrelator::maxVisiblePlot)
                                         : 0.0);
  QCPRange const range(keyStart, key);
  double const lastRef =
      (m_hasReferences) ? m_refIterator->normalizedPrice : maxDoubleValue;
  double currentRef = maxDoubleValue;
  double minValue = maxDoubleValue;
  double maxValue = -maxDoubleValue;

  std::lock_guard<std::mutex> lock_g{m_mutex};
  bool isResettingSymbols = false, isResettingRefs = false;
  for (auto &value : m_tokens) {
    ++value.graphPointsDrawnCount;
    bool isRefSymbol = value.tokenName.length() == 1;
    double price = value.normalizedPrice;

    if (isRefSymbol) {
      price = 0.0;
      isResettingRefs = korrelator::restartTickValues[1].has_value() &&
          (value.graphPointsDrawnCount >=
          (qint64)*korrelator::restartTickValues[1]);
      if (isResettingRefs)
        value.graphPointsDrawnCount = 0;

      for (auto const &v : m_refs)
        price += v.normalizedPrice;
      price /= ((double)m_refs.size());
      m_refIterator->normalizedPrice = currentRef = price;
    }

    value.graph->addData(key, price);

    if (value.prevNormalizedPrice == maxDoubleValue)
      value.prevNormalizedPrice = price;

    if (!isRefSymbol) {
      isResettingSymbols = korrelator::restartTickValues[0].has_value() &&
          (value.graphPointsDrawnCount >=
           (qint64)*korrelator::restartTickValues[0]);
      if (isResettingSymbols)
        value.graphPointsDrawnCount = 0;
      auto const crossOverDecision = lineCrossedOver(
          lastRef, currentRef, value.prevNormalizedPrice, price);
      if (crossOverDecision != trade_action_e::do_nothing) {
        auto &crossOver = value.crossOver.emplace();
        crossOver.price = value.realPrice;
        crossOver.action = crossOverDecision;
        crossOver.time = QDateTime::currentDateTime()
            .toString("yyyy-MM-dd hh:mm:ss");
        value.crossedOver = true;
      }

      if (value.crossedOver) {
        double amp = 0.0;
        auto &crossOverValue = *value.crossOver;
        if (crossOverValue.action == trade_action_e::buy)
          amp = price/currentRef - 1.0;
        else
          amp = currentRef/price - 1.0;
        if (amp >= m_threshold) {
          korrelator::model_data_t data;
          data.marketType = (value.tradeType == korrelator::trade_type_e::spot ?
                               "SPOT": "FUTURES");
          data.signalPrice = crossOverValue.price;
          data.openPrice = value.realPrice;
          data.side = actionTypeToString(value.crossOver->action);
          data.symbol = value.tokenName;
          data.openTime = QDateTime::currentDateTime()
              .toString("yyyy-MM-dd hh:mm:ss");
          data.signalTime = crossOverValue.time;

          emit newOrderDetected(crossOverValue, data);
          m_model->AddData(std::move(data));

          value.crossedOver = false;
          value.crossOver.reset();
        }
      }
    }

    if (updatingMinMax) {
      bool foundInRange = false;
      auto const visibleValueRange =
          value.graph->getValueRange(foundInRange, QCP::sdBoth, range);
      if (foundInRange) {
        minValue =
            std::min(std::min(minValue, visibleValueRange.lower), price);
        maxValue =
            std::max(std::max(maxValue, visibleValueRange.upper), price);
      } else {
        minValue = std::min(minValue, price);
        maxValue = std::max(maxValue, price);
      }
    }

    value.prevNormalizedPrice = price;
    value.graph->setName(QString(legendDisplayFormat)
                             .arg(value.legendName)
                             .arg(value.graphPointsDrawnCount));
  }

  if (isResettingRefs || isResettingSymbols)
    resetTickerData(isResettingRefs, isResettingSymbols);

  if (updatingMinMax) {
    auto const diff = (maxValue - minValue) / 19.0;
    minValue -= diff;
    maxValue += diff;
    ui->customPlot->yAxis->setRange(minValue, maxValue);
  }
}

void MainDialog::onTimerTick() {
  double const key = korrelator::time.elapsed() / 1'000.0;
  // update the min max on the y-axis every second
  bool const updatingMinMax = (key - korrelator::lastPoint) > 1.0;
  if (updatingMinMax)
    korrelator::lastPoint = key;

  updateGraphData(key, updatingMinMax);
  // make key axis range scroll right with the data at a constant range of 100
  ui->customPlot->xAxis->setRange(key, korrelator::maxVisiblePlot,
                                  Qt::AlignRight);
  ui->customPlot->replot(QCustomPlot::RefreshPriority::rpQueuedReplot);
}

void MainDialog::resetTickerData(const bool resetRefs, const bool resetSymbols) {
  static auto resetMap = [](auto &map) {
    for (auto &value : map)
      value.calculatingNewMinMax = true;
  };

  if (resetRefs && resetSymbols) {
    resetMap(m_refs);
    return resetMap(m_tokens);
  }
  else if (resetRefs)
    resetMap(m_refs);
  else
    resetMap(m_tokens);
}

void MainDialog::generateJsonFile(
    korrelator::cross_over_data_t const &,
    korrelator::model_data_t const &modelData)
{
  static auto const path = QDir::currentPath() + "/correlator/"
      + QDateTime::currentDateTime().toString("yyyy_MM_dd_hh_mm_ss") + "/";
  if (auto dir = QDir(path); !dir.exists())
    dir.mkpath(path);
  auto const filename = path + QTime::currentTime().toString("hh_mm_ss")
      + ".json";
  if (QFileInfo::exists(filename))
    QFile::remove(filename);

  QFile file(filename);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return;
  QJsonObject obj;
  obj["symbol"] = modelData.symbol;
  obj["marketType"] = modelData.marketType;
  obj["signalPrice"] = modelData.signalPrice;
  obj["signalTime"] = modelData.signalTime;
  obj["openPrice"] = modelData.openPrice;
  obj["openTime"] = modelData.openTime;
  obj["side"] = modelData.side;

  file.write(QJsonDocument(obj).toJson());
  file.close();
}
