#include "maindialog.hpp"

#include "ui_maindialog.h"

#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <set>

#include "container.hpp"
#include "order_model.hpp"
#include "qcustomplot.h"

static char const *const binance_futures_tokens_url =
    "https://fapi.binance.com/fapi/v1/ticker/price";
static char const *const binance_spot_tokens_url =
    "https://api.binance.com/api/v3/ticker/price";

static char const *const kucoin_spot_tokens_url =
    "https://api.kucoin.com/api/v1/market/allTickers";
static char const *const kucoin_futures_tokens_url =
    "https://api-futures.kucoin.com/api/v1/contracts/active";

static constexpr const double maxDoubleValue =
    std::numeric_limits<double>::max();

namespace korrelator {

QString actionTypeToString(korrelator::trade_action_e a) {
  if (a == korrelator::trade_action_e::buy)
    return "BUY";
  return "SELL";
}

QString exchangeNameToString(korrelator::exchange_name_e const ex) {
  if (ex == korrelator::exchange_name_e::binance)
    return "Binance";
  else if (ex == korrelator::exchange_name_e::kucoin)
    return "KuCoin";
  return QString();
}

korrelator::exchange_name_e stringToExchangeName(QString const &name) {
  auto const name_ = name.trimmed();
  if (name_.compare("binance", Qt::CaseInsensitive) == 0)
    return korrelator::exchange_name_e::binance;
  else if (name_.compare("kucoin", Qt::CaseInsensitive) == 0)
    return korrelator::exchange_name_e::kucoin;
  return korrelator::exchange_name_e::none;
}

void updateTokenIter(token_list_t::iterator iter, double const price) {
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
  graphPointsDrawnCount = 0;
  alpha = 1.0;
}

static double maxVisiblePlot = 100.0;
static QTime time(QTime::currentTime());
static double lastPoint = 0.0;
static std::vector<std::optional<double>> restartTickValues{};
} // namespace korrelator

// =======================================================================

MainDialog::MainDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::MainDialog) {
  ui->setupUi(this);

  qRegisterMetaType<korrelator::model_data_t>();
  qRegisterMetaType<korrelator::cross_over_data_t>();
  qRegisterMetaType<korrelator::exchange_name_e>();
  qRegisterMetaType<korrelator::trade_type_e>();

  setWindowIcon(qApp->style()->standardPixmap(QStyle::SP_DesktopIcon));
  setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint |
                 Qt::WindowMaximizeButtonHint);
  ui->restartTickCombo->addItems(
      {"Normal lines", "Ref line", "All lines", "Special"});
  populateUIComponents();
  connectAllUISignals();
  readTokensFromFile();
}

MainDialog::~MainDialog() {
  stopGraphPlotting();
  resetGraphComponents();
  m_websocket.reset();
  saveTokensToFile();

  delete ui;
}

void MainDialog::populateUIComponents() {
  korrelator::restartTickValues.clear();
  auto const totalRestartTicks = ui->restartTickCombo->count();
  for (int i = 0; i < totalRestartTicks; ++i) {
    if (i == totalRestartTicks - 1) {
      korrelator::restartTickValues.push_back(maxDoubleValue);
    }
    korrelator::restartTickValues.push_back(2'500);
  }

  ui->restartTickLine->setValidator(new QIntValidator(1, 1'000'000));
  ui->timerTickCombo->addItems(
      {"100ms", "200ms", "500ms", "1 sec", "2 secs", "5 secs"});
  ui->selectionCombo->addItems({"Default(100 seconds)", "1 min", "2 mins",
                                "5 mins", "10 mins", "30 mins", "1 hr", "2 hrs",
                                "3 hrs", "5 hrs"});
  ui->legendPositionCombo->addItems(
      {"Top Left", "Top Right", "Bottom Left", "Bottom Right"});
  ui->umbralLine->setValidator(new QDoubleValidator);
  ui->umbralLine->setText("5");
}

void MainDialog::onApplyButtonClicked() {
  using korrelator::tick_line_type_e;

  auto const index = ui->restartTickCombo->currentIndex();
  auto const value = getIntegralValue(ui->restartTickLine);
  if (isnan(value))
    return;

  if (value == maxDoubleValue)
    return ui->restartTickLine->setFocus();

  if (index == tick_line_type_e::all) {
    korrelator::restartTickValues[tick_line_type_e::normal].emplace(value);
    korrelator::restartTickValues[tick_line_type_e::ref].emplace(value);
  } else if (index == tick_line_type_e::special) {
    auto const specialValue = getIntegralValue(ui->specialLine);
    if (isnan(specialValue))
      return;
    if (specialValue == maxDoubleValue)
      korrelator::restartTickValues[tick_line_type_e::special].reset();
    else
      korrelator::restartTickValues[tick_line_type_e::special].emplace(value);
    m_specialRef = specialValue;
    return ui->specialLine->setFocus();
  } else
    korrelator::restartTickValues[index].emplace(value);
  ui->restartTickLine->setFocus();
}

void MainDialog::connectAllUISignals() {
  QObject::connect(
      ui->restartTickCombo,
      static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
      this, [this](int const index) {
        using korrelator::tick_line_type_e;
        ui->specialLine->setText("");
        ui->restartTickLine->setText("");

        auto &optionalValue = korrelator::restartTickValues[index];
        if (optionalValue) {
          if (index == tick_line_type_e::special &&
              *optionalValue != maxDoubleValue) {
            ui->specialLine->setText(QString::number(m_specialRef));
            ui->restartTickLine->setText(QString::number(*optionalValue));
          }
          if (index != tick_line_type_e::special)
            ui->restartTickLine->setText(
                QString::number(optionalValue.value()));
        } else
          ui->restartTickLine->clear();
        ui->restartTickLine->setFocus();
      });

  QObject::connect(
      ui->exchangeCombo,
      static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
      this, [this](int const index) {
        if (index == 2) {
          ui->futuresCombo->clear();
          ui->spotCombo->clear();
          m_currentExchange = exchange_name_e::none;
          return;
        }
        m_currentExchange = static_cast<exchange_name_e>(index);
        getSpotsTokens(m_currentExchange);
        getFuturesTokens(m_currentExchange);
      });
  ui->exchangeCombo->addItems({"Binance", "KuCoin"});

  QObject::connect(ui->applyButton, &QPushButton::clicked, this,
                   &MainDialog::onApplyButtonClicked);
  QObject::connect(this, &MainDialog::newOrderDetected, this,
                   &MainDialog::generateJsonFile);

  QObject::connect(
      ui->selectionCombo,
      static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
      this, [this](int const) {
        korrelator::maxVisiblePlot = getMaxPlotsInVisibleRegion();
        if (!m_programIsRunning) {
          double const key = korrelator::time.elapsed() / 1'000.0;
          ui->customPlot->xAxis->setRange(key, korrelator::maxVisiblePlot,
                                          Qt::AlignRight);
          ui->customPlot->replot();
        }
      });

  QObject::connect(ui->spotNextButton, &QToolButton::clicked, this, [this] {
    auto const tokenName = ui->spotCombo->currentText().trimmed();
    if (tokenName.isEmpty())
      return;
    addNewItemToTokenMap(tokenName, trade_type_e::spot, m_currentExchange);
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

  QObject::connect(ui->futuresNextButton, &QToolButton::clicked, this, [this] {
    auto const tokenName = ui->futuresCombo->currentText().trimmed();
    if (tokenName.isEmpty())
      return;

    addNewItemToTokenMap(tokenName, trade_type_e::futures, m_currentExchange);
    saveTokensToFile();
  });
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
  ui->specialLine->setEnabled(enabled);
  ui->specialLine->setEnabled(enabled);
  ui->exchangeCombo->setEnabled(enabled);
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
  m_websocket.reset();
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

MainDialog::list_iterator MainDialog::find(korrelator::token_list_t &container,
                                           QString const &tokenName,
                                           trade_type_e const tt,
                                           exchange_name_e const exchange) {
  return std::find_if(container.begin(), container.end(),
                      [&tokenName, tt, exchange](korrelator::token_t const &a) {
                        return a.tokenName.compare(tokenName,
                                                   Qt::CaseInsensitive) == 0 &&
                               tt == a.tradeType && a.exchange == exchange;
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

void MainDialog::newItemAdded(QString const &tokenName, trade_type_e const tt,
                              exchange_name_e const exchange) {
  bool const isRef = ui->refCheckBox->isChecked();
  bool const hasRefStored = find(m_tokens, "*") != m_tokens.end();
  if (isRef) {
    if (!hasRefStored) {
      korrelator::token_t token;
      token.tokenName = "*";
      token.calculatingNewMinMax = true;
      token.tradeType = tt;
      token.normalizedPrice = maxDoubleValue;
      m_tokens.push_back(std::move(token));
    }

    if (find(m_refs, tokenName, tt, exchange) == m_refs.end()) {
      korrelator::token_t token;
      token.tradeType = tt;
      token.tokenName = tokenName;
      token.calculatingNewMinMax = true;
      token.tradeType = tt;
      token.exchange = exchange;
      m_refs.push_back(std::move(token));
    }
  } else {
    if (find(m_tokens, tokenName, tt, exchange) == m_tokens.end()) {
      korrelator::token_t token;
      token.tokenName = tokenName;
      token.calculatingNewMinMax = true;
      token.tradeType = tt;
      token.exchange = exchange;
      m_tokens.push_back(std::move(token));
    }
  }
}

auto tokenNameFromWidgetName(QString specialTokenName) {
  struct token_separate_t {
    QString tokenName;
    korrelator::trade_type_e tradeType;
    korrelator::exchange_name_e exchange;
  };

  token_separate_t data;
  if (specialTokenName.endsWith('*'))
    specialTokenName.chop(1);

  auto const openingBrace = specialTokenName.indexOf('(') + 1;
  auto const closingBrace = specialTokenName.indexOf(')');
  data.tokenName = specialTokenName.left(specialTokenName.indexOf('_'));
  data.exchange = korrelator::stringToExchangeName(
      specialTokenName.mid(openingBrace, closingBrace - openingBrace));

  if (specialTokenName.contains("_SPOT"))
    data.tradeType = korrelator::trade_type_e::spot;
  else
    data.tradeType = korrelator::trade_type_e::futures;
  return data;
}

void MainDialog::tokenRemoved(QString const &text) {
  auto const &data = tokenNameFromWidgetName(text);
  auto &tokenMap = text.endsWith('*') ? m_refs : m_tokens;
  auto const &tokenName = data.tokenName;
  auto const tradeType = data.tradeType;
  auto const exchange = data.exchange;

  if (auto iter = find(tokenMap, tokenName, tradeType, exchange);
      iter != tokenMap.end()) {
    tokenMap.erase(iter);
  }

  if (m_refs.empty()) {
    if (auto iter = find(m_tokens, "*"); iter != m_tokens.end())
      m_tokens.erase(iter);
  }
}

void MainDialog::getSpotsTokens(korrelator::exchange_name_e const exchange,
                                callback_t cb) {
  char const *const exchangeUrl =
      exchange == korrelator::exchange_name_e::binance ? binance_spot_tokens_url
                                                       : kucoin_spot_tokens_url;
  if (cb)
    return sendNetworkRequest(QUrl(exchangeUrl), cb, trade_type_e::spot,
                              exchange);

  auto &spots = m_watchables[(int)exchange].spots;
  if (!spots.empty()) {
    ui->spotCombo->clear();
    for (auto const &d : spots)
      ui->spotCombo->addItem(d.tokenName.toUpper());
    return;
  }

  auto callback = [this](korrelator::token_list_t &&list,
                         exchange_name_e const exchange) {
    ui->spotCombo->clear();
    for (auto const &d : list)
      ui->spotCombo->addItem(d.tokenName.toUpper());
    m_watchables[(int)exchange].spots = std::move(list);
  };
  sendNetworkRequest(QUrl(exchangeUrl), callback, trade_type_e::spot, exchange);
}

void MainDialog::getFuturesTokens(korrelator::exchange_name_e const exchange,
                                  callback_t cb) {
  char const *const exchangeUrl =
      exchange == korrelator::exchange_name_e::binance
          ? binance_futures_tokens_url
          : kucoin_futures_tokens_url;

  if (cb)
    return sendNetworkRequest(QUrl(exchangeUrl), cb, trade_type_e::futures,
                              exchange);

  auto &futures = m_watchables[(int)exchange].futures;
  if (!futures.empty()) {
    ui->futuresCombo->clear();
    for (auto const &d : futures)
      ui->futuresCombo->addItem(d.tokenName.toUpper());
    return;
  }

  auto callback = [this](korrelator::token_list_t &&list,
                         exchange_name_e const exchange) {
    ui->futuresCombo->clear();
    for (auto const &d : list)
      ui->futuresCombo->addItem(d.tokenName.toUpper());
    m_watchables[(int)exchange].futures = std::move(list);
  };
  sendNetworkRequest(QUrl(exchangeUrl), callback, trade_type_e::futures,
                     exchange);
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
    auto const &d = tokenNameFromWidgetName(displayedName);
    obj["symbol"] = d.tokenName.toLower();
    obj["market"] =
        d.tradeType == korrelator::trade_type_e::spot ? "spot" : "futures";
    obj["ref"] = displayedName.endsWith('*');
    obj["exchange"] = korrelator::exchangeNameToString(d.exchange);
    jsonList.append(obj);
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
    auto const exchange =
        korrelator::stringToExchangeName(obj["exchange"].toString());
    if (exchange == korrelator::exchange_name_e::none)
      continue;
    ui->refCheckBox->setChecked(isRef);
    addNewItemToTokenMap(tokenName, tradeType, exchange);
  }
  ui->refCheckBox->setChecked(refPreValue);
}

void MainDialog::addNewItemToTokenMap(
    QString const &tokenName, korrelator::trade_type_e const tt,
    korrelator::exchange_name_e const exchange) {
  auto const text =
      tokenName.toUpper() +
      (tt == korrelator::trade_type_e::spot ? "_SPOT" : "_FUTURES") +
      ("(" + exchangeNameToString(exchange) + ")") +
      (ui->refCheckBox->isChecked() ? "*" : "");
  for (int i = 0; i < ui->tokenListWidget->count(); ++i) {
    QListWidgetItem *item = ui->tokenListWidget->item(i);
    if (text == item->text())
      return;
  }
  ui->tokenListWidget->addItem(text);
  newItemAdded(tokenName.toLower(), tt, exchange);
}

void MainDialog::sendNetworkRequest(QUrl const &url,
                                    callback_t onSuccessCallback,
                                    korrelator::trade_type_e const tradeType,
                                    exchange_name_e const exchange) {
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader,
                    "application/json");
  korrelator::configureRequestForSSL(request);
  m_networkManager.enableStrictTransportSecurityStore(true);

  auto reply = m_networkManager.get(request);
  QObject::connect(
      reply, &QNetworkReply::finished, this,
      [=, cb = std::move(onSuccessCallback)] {
        if (reply->error() != QNetworkReply::NoError) {
          QMessageBox::critical(this, "Error",
                                "Unable to get the list of all token pairs"
                                "=> " +
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

        korrelator::token_list_t tokenList;
        QJsonArray list;
        QString key = "price";
        QString const key2 = "lastTradePrice";
        auto const isKuCoin = exchange == korrelator::exchange_name_e::kucoin;

        if (exchange == korrelator::exchange_name_e::binance)
          list = jsonResponse.array();
        else if (isKuCoin) {
          auto const rootObject = jsonResponse.object();
          auto const jsonData = rootObject.value("data");
          if (jsonData.isObject())
            list = jsonData.toObject().value("ticker").toArray();
          else if (jsonData.isArray())
            list = jsonData.toArray();
          key = "last";
        }

        if (list.isEmpty())
          return cb({}, exchange);

        tokenList.reserve(list.size());
        for (int i = 0; i < list.size(); ++i) {
          auto const tokenObject = list[i].toObject();
          korrelator::token_t t;
          t.tokenName = tokenObject.value("symbol").toString().toLower();
          if (isKuCoin && !tokenObject.contains(key)) {
            if (auto const f = tokenObject.value(key2); f.isString())
              t.realPrice = f.toString().toDouble();
            else
              t.realPrice = f.toDouble();
          }
          else
            t.realPrice = tokenObject.value(key).toString().toDouble();
          t.exchange = exchange;
          t.tradeType = tradeType;
          tokenList.push_back(std::move(t));
        }

        std::sort(tokenList.begin(), tokenList.end(),
                  korrelator::token_compare_t{});
        cb(std::move(tokenList), exchange);
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
  static size_t numberOfRecursions = 0;

  auto normalizePrice = [this](auto &list, auto &result, auto const tt) {
    for (auto listIter = list.begin(); listIter != list.end(); ++listIter) {
      auto &value = *listIter;
      if (value.tradeType != tt || value.tokenName.size() == 1)
        continue;
      auto iter = find(result, value.tokenName, value.tradeType, value.exchange);
      if (iter != result.end()) {
        listIter->calculatingNewMinMax = true;
        updateTokenIter(listIter, iter->realPrice);
      }
    }
  };

  auto foo = [this, normalizePrice](korrelator::token_list_t &&result,
                                    trade_type_e const tt) {
    normalizePrice(m_tokens, result, tt);
    normalizePrice(m_refs, result, tt);

    if (--numberOfRecursions == 0) {
      if (m_hasReferences) {
        auto price = 0.0;
        for (auto const &t : m_refs)
          price += t.normalizedPrice;
        m_refIterator->normalizedPrice = (price / (double)m_refs.size());
      }
      // after successfully getting the SPOTs and FUTURES' prices,
      // start the websocket and get real-time updates.
      startWebsocket();
    }
  };

  std::set<exchange_name_e> exchanges;
  for (auto &t : m_refs)
    exchanges.insert(t.exchange);
  for (auto &t: m_tokens)
    if (t.exchange != exchange_name_e::none)
      exchanges.insert(t.exchange);

  numberOfRecursions = 2 * exchanges.size();

  // get prices of symbols that are on the SPOT "list" first
  for (auto const &exchange : exchanges) {
    getSpotsTokens(exchange, [&, foo](korrelator::token_list_t &&result,
                                      exchange_name_e const) {
      foo(std::move(result), trade_type_e::spot);
    });

    getFuturesTokens(exchange, [&, foo](korrelator::token_list_t &&result,
                                        exchange_name_e const) {
      foo(std::move(result), trade_type_e::futures);
    });
  }
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

bool MainDialog::validateUserInput() {
  m_threshold = getIntegralValue(ui->umbralLine);
  if (isnan(m_threshold))
    return false;
  m_findingUmbral = m_threshold != maxDoubleValue;
  if (m_findingUmbral)
    m_threshold /= 100.0;

  auto &specialValue = korrelator::restartTickValues[3];
  m_findingSpecialRef =
      specialValue.has_value() && specialValue.value() != maxDoubleValue;
  if (m_findingSpecialRef)
    m_specialRef /= 100.0;
  return true;
}

void MainDialog::setupOrderTableModel() {
  m_model = std::make_unique<korrelator::order_model>();
  ui->tableView->setModel(m_model.get());
  auto header = ui->tableView->horizontalHeader();
  header->setSectionResizeMode(QHeaderView::Stretch);
  ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
  header->setMaximumSectionSize(200);
  ui->tableView->setWordWrap(true);
  ui->tableView->resizeRowsToContents();
}

void MainDialog::onOKButtonClicked() {
  if (ui->tokenListWidget->count() == 0)
    return;

  if (m_programIsRunning)
    return stopGraphPlotting();

  if (!validateUserInput())
    return;

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

  setupOrderTableModel();
  getInitialTokenPrices();
}

void MainDialog::onNewPriceReceived(QString const &tokenName,
                                    double const price,
                                    exchange_name_e const exchange,
                                    trade_type_e const tt) {
  auto iter = find(m_tokens, tokenName, tt, exchange);
  if (iter != m_tokens.end()) {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    updateTokenIter(iter, price);
  }

  iter = find(m_refs, tokenName, tt, exchange);
  if (iter != m_refs.end()) {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    updateTokenIter(iter, price);
  }
}

void MainDialog::startWebsocket() {
  // price updater
  m_priceUpdater.worker = std::make_unique<korrelator::Worker>([this] {
    m_websocket = std::make_unique<korrelator::cwebsocket>();

    for (auto const &tokenInfo : m_refs) {
      m_websocket->addSubscription(tokenInfo.tokenName, tokenInfo.tradeType,
                                   tokenInfo.exchange);
    }

    for (auto const &tokenInfo : m_tokens) {
      if (tokenInfo.tokenName.size() != 1)
        m_websocket->addSubscription(tokenInfo.tokenName, tokenInfo.tradeType,
                                     tokenInfo.exchange);
    }

    QObject::connect(m_websocket.get(),
                     &korrelator::cwebsocket::onNewPriceAvailable, this,
                     &MainDialog::onNewPriceReceived);
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

void calculateGraphMinMax(korrelator::token_t &value, QCPRange const &range,
                          double &minValue, double &maxValue) {
  bool foundInRange = false;
  auto const visibleValueRange =
      value.graph->getValueRange(foundInRange, QCP::sdBoth, range);
  if (foundInRange) {
    minValue = std::min(std::min(minValue, visibleValueRange.lower),
                        value.normalizedPrice);
    maxValue = std::max(std::max(maxValue, visibleValueRange.upper),
                        value.normalizedPrice);
  } else {
    minValue = std::min(minValue, value.normalizedPrice);
    maxValue = std::max(maxValue, value.normalizedPrice);
  }
}

korrelator::ref_calculation_data_t
MainDialog::updateRefGraph(double const keyStart, double const keyEnd,
                           bool const updatingMinMax) {
  using korrelator::tick_line_type_e;

  korrelator::ref_calculation_data_t refResult;

  auto &value = m_tokens[0];
  ++value.graphPointsDrawnCount;

  if (!m_findingSpecialRef) {
    auto const &refTickValue =
        korrelator::restartTickValues[tick_line_type_e::ref];
    refResult.isResettingRef =
        refTickValue.has_value() &&
        (value.graphPointsDrawnCount >= (qint64)*refTickValue);
  } else {
    auto const &refTickValue =
        korrelator::restartTickValues[tick_line_type_e::special];
    refResult.eachTickNormalize = value.graphPointsDrawnCount >= *refTickValue;
  }

  if (refResult.isResettingRef || refResult.eachTickNormalize)
    value.graphPointsDrawnCount = 0;

  // get the normalizedValue
  value.normalizedPrice = 0.0;
  for (auto const &v : m_refs)
    value.normalizedPrice += v.normalizedPrice;

  value.normalizedPrice /= ((double)m_refs.size());
  m_refIterator->normalizedPrice = value.normalizedPrice * m_refIterator->alpha;

  if (updatingMinMax) {
    QCPRange const range(keyStart, keyEnd);
    calculateGraphMinMax(value, range, refResult.minValue, refResult.maxValue);
  }

  value.prevNormalizedPrice = value.prevNormalizedPrice;
  value.graph->setName(
      QString("%1(%2)").arg(value.legendName).arg(value.graphPointsDrawnCount));
  value.graph->addData(keyEnd, value.normalizedPrice);
  return refResult;
}

void MainDialog::updateGraphData(double const key, bool const updatingMinMax) {
  using korrelator::tick_line_type_e;
  using korrelator::trade_action_e;

  static char const *const legendDisplayFormat = "%1(%2)";

  double const keyStart =
      (key >= korrelator::maxVisiblePlot ? (key - korrelator::maxVisiblePlot)
                                         : 0.0);
  double const prevRef =
      (m_hasReferences) ? m_refIterator->normalizedPrice : maxDoubleValue;

  std::lock_guard<std::mutex> lock_g{m_mutex};
  bool isResettingSymbols = false;

  // update ref symbol data on the graph
  auto refResult = (!m_hasReferences)
                       ? korrelator::ref_calculation_data_t()
                       : updateRefGraph(keyStart, key, updatingMinMax);
  double currentRef = m_refIterator->normalizedPrice;

  // update the real symbols
  for (int i = 1; i < m_tokens.size(); ++i) {
    auto &value = m_tokens[i];

    ++value.graphPointsDrawnCount;
    if (value.prevNormalizedPrice == maxDoubleValue)
      value.prevNormalizedPrice = value.normalizedPrice;

    isResettingSymbols =
        !m_findingSpecialRef && korrelator::restartTickValues[0].has_value() &&
        (value.graphPointsDrawnCount >=
         (qint64)*korrelator::restartTickValues[tick_line_type_e::normal]);

    if (isResettingSymbols)
      value.graphPointsDrawnCount = 0;

    auto const crossOverDecision = lineCrossedOver(
        prevRef, currentRef, value.prevNormalizedPrice, value.normalizedPrice);

    if (crossOverDecision != trade_action_e::do_nothing) {
      auto &crossOver = value.crossOver.emplace();
      crossOver.price = value.realPrice;
      crossOver.action = crossOverDecision;
      crossOver.time =
          QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
      value.crossedOver = true;
    }

    if (value.crossedOver) {
      double amp = 0.0;
      auto &crossOverValue = *value.crossOver;
      if (crossOverValue.action == trade_action_e::buy)
        amp = (value.normalizedPrice / currentRef) - 1.0;
      else
        amp = (currentRef / value.normalizedPrice) - 1.0;

      if (m_findingUmbral && amp >= m_threshold) {
        korrelator::model_data_t data;
        data.marketType =
            (value.tradeType == korrelator::trade_type_e::spot ? "SPOT"
                                                               : "FUTURES");
        data.signalPrice = crossOverValue.price;
        data.openPrice = value.realPrice;
        data.side = actionTypeToString(value.crossOver->action);
        data.symbol = value.tokenName;
        data.openTime =
            QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        data.signalTime = crossOverValue.time;

        emit newOrderDetected(crossOverValue, data);
        m_model->AddData(std::move(data));

        value.crossedOver = false;
        value.crossOver.reset();
      }
    }

    if (refResult.eachTickNormalize) {
      currentRef /= m_refIterator->alpha;
      auto const distanceFromRefToSymbol = // a
          ((value.normalizedPrice > currentRef)
               ? (value.normalizedPrice / currentRef)
               : (currentRef / value.normalizedPrice)) -
          1.0;
      auto const distanceThreshold = m_specialRef; // b
      if (value.normalizedPrice > currentRef) {
        m_refIterator->alpha =
            ((distanceFromRefToSymbol + 1) / (distanceThreshold + 1.0));
      } else {
        m_refIterator->alpha =
            ((distanceThreshold + 1) / (distanceFromRefToSymbol + 1.0));
      }
    }

    if (updatingMinMax) {
      QCPRange const range(keyStart, key);
      calculateGraphMinMax(value, range, refResult.minValue,
                           refResult.maxValue);
    }

    value.prevNormalizedPrice = value.normalizedPrice;
    value.graph->setName(QString(legendDisplayFormat)
                             .arg(value.legendName)
                             .arg(value.graphPointsDrawnCount));
    value.graph->addData(key, value.normalizedPrice);
  } // end for

  if (refResult.isResettingRef || isResettingSymbols)
    resetTickerData(refResult.isResettingRef, isResettingSymbols);

  if (updatingMinMax) {
    auto const diff = (refResult.maxValue - refResult.minValue) / 19.0;
    refResult.minValue -= diff;
    refResult.maxValue += diff;
    ui->customPlot->yAxis->setRange(refResult.minValue, refResult.maxValue);
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

void MainDialog::resetTickerData(const bool resetRefs,
                                 const bool resetSymbols) {
  static auto resetMap = [](auto &map) {
    for (auto &value : map)
      value.calculatingNewMinMax = true;
  };

  if (resetRefs && resetSymbols) {
    resetMap(m_refs);
    return resetMap(m_tokens);
  } else if (resetRefs)
    resetMap(m_refs);
  else
    resetMap(m_tokens);
}

void MainDialog::generateJsonFile(korrelator::cross_over_data_t const &,
                                  korrelator::model_data_t const &modelData) {
  static auto const path =
      QDir::currentPath() + "/correlator/" +
      QDateTime::currentDateTime().toString("yyyy_MM_dd_hh_mm_ss") + "/";
  if (auto dir = QDir(path); !dir.exists())
    dir.mkpath(path);
  auto const filename =
      path + QTime::currentTime().toString("hh_mm_ss") + ".json";
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
