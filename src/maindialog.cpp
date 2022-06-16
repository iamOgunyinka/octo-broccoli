#include "binance_https_request.hpp"

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

#include "constants.hpp"
#include "container.hpp"
#include "kc_https_request.hpp"
#include "order_model.hpp"
#include "qcustomplot.h"
#include "websocket_manager.hpp"

static auto const https = QString("https://");
static auto const binance_futures_tokens_url =
    https + korrelator::constants::binance_http_futures_host +
    "/fapi/v1/ticker/price";
static auto const binance_spot_tokens_url =
    https + korrelator::constants::binance_http_spot_host +
    "/api/v3/ticker/price";

static auto const kucoin_spot_tokens_url =
    https + korrelator::constants::kucoin_https_spot_host +
    "/api/v1/market/allTickers";
static auto const kucoin_futures_tokens_url =
    https + korrelator::constants::kc_futures_api_host +
    "/api/v1/contracts/active";

static constexpr const double maxDoubleValue =
    std::numeric_limits<double>::max();

namespace korrelator {

static double maxVisiblePlot = 100.0;
static QTime time(QTime::currentTime());
static double lastPoint = 0.0;
static std::vector<std::optional<double>> restartTickValues{};

QString actionTypeToString(trade_action_e a) {
  if (a == trade_action_e::buy)
    return "BUY";
  return "SELL";
}

QString exchangeNameToString(exchange_name_e const ex) {
  if (ex == exchange_name_e::binance)
    return "Binance";
  else if (ex == exchange_name_e::kucoin)
    return "KuCoin";
  return QString();
}

QString marketTypeToString(market_type_e const m) {
  if (m == market_type_e::market)
    return "market";
  else if (m == market_type_e::limit)
    return "limit";
  return "unknown";
}

market_type_e stringToMarketType(QString const &m) {
  if (m.compare("market") == 0)
    return market_type_e::market;
  else if (m.compare("limit") == 0)
    return market_type_e::limit;
  return market_type_e::unknown;
}

exchange_name_e stringToExchangeName(QString const &name) {
  auto const name_ = name.trimmed();
  if (name_.compare("binance", Qt::CaseInsensitive) == 0)
    return exchange_name_e::binance;
  else if (name_.compare("kucoin", Qt::CaseInsensitive) == 0)
    return exchange_name_e::kucoin;
  return exchange_name_e::none;
}

void updateTokenIter(token_list_t::value_type &value) {
  if (value.symbolName.length() == 1)
    return;
  auto const price = value.realPrice;
  if (value.calculatingNewMinMax) {
    value.minPrice = price * 0.75;
    value.maxPrice = price * 1.25;
    value.calculatingNewMinMax = false;
  }

  value.minPrice = std::min(value.minPrice, price);
  value.maxPrice = std::max(value.maxPrice, price);
  value.normalizedPrice =
      (price - value.minPrice) / (value.maxPrice - value.minPrice);
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

struct plug_data_t {
  api_data_t apiInfo;
  trade_config_data_t *tradeConfig;
  trade_type_e tradeType;
  double tokenPrice;
};

} // namespace korrelator

static korrelator::waitable_container_t<korrelator::plug_data_t> kc_plugs{};
static korrelator::waitable_container_t<korrelator::plug_data_t>
    binance_plugs{};

MainDialog::MainDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::MainDialog) {

  ui->setupUi(this);
  setWindowIcon(qApp->style()->standardPixmap(QStyle::SP_DesktopIcon));
  setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint |
                 Qt::WindowMaximizeButtonHint);

  ui->restartTickCombo->addItems(
      {"Normal lines", "Ref line", "All lines", "Special"});

  registerCustomTypes();
  populateUIComponents();
  connectAllUISignals();
  readTokensFromFile();
  readOrdersConfigFromFile();
  m_apiTradeApiMap = SettingsDialog::getApiDataMap();
  if (m_apiTradeApiMap.empty()) {
    QMessageBox::information(this, "Information",
                             "To automate orders, please use the settings "
                             "button to add new API information");
  }

  std::thread{[this]{ tradeBinanceToken(this, m_model); }}.detach();
  std::thread{[this]{ tradeKuCoinToken(this, m_model); }}.detach();

  /*
  QTimer::singleShot(std::chrono::milliseconds(2000), this, [this] {
    getSpotsTokens(exchange_name_e::kucoin);
    getFuturesTokens(exchange_name_e::kucoin);
  });*/
}

MainDialog::~MainDialog() {
  stopGraphPlotting();
  resetGraphComponents();
  m_websocket.reset();
  saveTokensToFile();

  delete ui;
}

void MainDialog::registerCustomTypes() {
  qRegisterMetaType<korrelator::model_data_t>();
  qRegisterMetaType<korrelator::cross_over_data_t>();
  qRegisterMetaType<korrelator::exchange_name_e>();
  qRegisterMetaType<korrelator::trade_type_e>();
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

  ui->resetPercentageLine->setValidator(new QDoubleValidator);
  ui->specialLine->setValidator(new QDoubleValidator);
  ui->restartTickLine->setValidator(new QIntValidator(1, 1'000'000));
  ui->timerTickCombo->addItems(
      {"100ms", "200ms", "500ms", "1 sec", "2 secs", "5 secs"});
  ui->selectionCombo->addItems({"Default(100 seconds)", "1 min", "2 mins",
                                "5 mins", "10 mins", "30 mins", "1 hr", "2 hrs",
                                "3 hrs", "5 hrs"});
  ui->legendPositionCombo->addItems(
      {"Top Left", "Top Right", "Bottom Left", "Bottom Right"});
  ui->graphThicknessCombo->addItems({"1", "2", "3", "4", "5"});
  ui->umbralLine->setValidator(new QDoubleValidator);
  ui->umbralLine->setText("5");
  ui->oneOpCheckBox->setChecked(true);

#ifdef TESTNET
  ui->liveTradeCheckbox->setChecked(true);
#endif
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
    m_resetPercentage = getIntegralValue(ui->resetPercentageLine);
    if (isnan(m_resetPercentage)) {
      m_resetPercentage = maxDoubleValue;
      return;
    }

    if (specialValue == maxDoubleValue)
      korrelator::restartTickValues[tick_line_type_e::special].reset();
    else
      korrelator::restartTickValues[tick_line_type_e::special].emplace(value);
    m_specialRef = specialValue;
    m_doingManualReset = m_resetPercentage != maxDoubleValue;
    return ui->specialLine->setFocus();
  } else {
    korrelator::restartTickValues[index].emplace(value);
  }
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
        ui->resetPercentageLine->setText("");

        auto &optionalValue = korrelator::restartTickValues[index];
        if (optionalValue) {
          if (index == tick_line_type_e::special &&
              *optionalValue != maxDoubleValue) {
            ui->specialLine->setText(QString::number(m_specialRef));
            ui->restartTickLine->setText(QString::number(*optionalValue));

            if (maxDoubleValue != m_resetPercentage) {
              ui->resetPercentageLine->setText(
                  QString::number(m_resetPercentage));
            }
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
        auto const exchange = static_cast<exchange_name_e>(index);
        getSpotsTokens(exchange);
        getFuturesTokens(exchange);
      });
  ui->exchangeCombo->addItems({"Binance", "KuCoin"});

  QObject::connect(ui->applyButton, &QPushButton::clicked, this,
                   &MainDialog::onApplyButtonClicked);
  QObject::connect(this, &MainDialog::newOrderDetected, this,
                   [this](auto a, auto b, exchange_name_e const exchange,
                          trade_type_e const tt) {
                     onNewOrderDetected(std::move(a), std::move(b), exchange,
                                        tt);
                   });
  QObject::connect(ui->reloadTradeConfigButton, &QPushButton::clicked, this,
                   [this] { readOrdersConfigFromFile(); });
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
    auto const exchange =
        static_cast<exchange_name_e>(ui->exchangeCombo->currentIndex());
    addNewItemToTokenMap(tokenName, trade_type_e::spot, exchange);
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
                   &MainDialog::onOKButtonClicked);
  QObject::connect(ui->settingsButton, &QToolButton::clicked, this,
                   &MainDialog::onSettingsDialogClicked);

  QObject::connect(ui->futuresNextButton, &QToolButton::clicked, this, [this] {
    auto const tokenName = ui->futuresCombo->currentText().trimmed();
    if (tokenName.isEmpty())
      return;

    auto const exchange =
        static_cast<exchange_name_e>(ui->exchangeCombo->currentIndex());
    addNewItemToTokenMap(tokenName, trade_type_e::futures, exchange);
    saveTokensToFile();
  });
}

void MainDialog::onNewOrderDetected(korrelator::cross_over_data_t crossOver,
                                    korrelator::model_data_t modelData,
                                    exchange_name_e const exchange,
                                    trade_type_e const tradeType) {
  using korrelator::trade_action_e;
  auto &currentAction = crossOver.action;

  if (ui->reverseCheckBox->isChecked()) {
    if (currentAction == trade_action_e::buy)
      currentAction = trade_action_e::sell;
    else
      currentAction = trade_action_e::buy;
  }

  if (ui->oneOpCheckBox->isChecked()) {
    if (m_lastTradeAction == currentAction)
      return;
    m_lastTradeAction = currentAction;
  }

  crossOver.openPrice = modelData.openPrice;
  if (ui->liveTradeCheckbox->isChecked() && !m_apiTradeApiMap.empty())
    sendExchangeRequest(modelData, exchange, tradeType, crossOver);

  modelData.side = korrelator::actionTypeToString(currentAction);
  modelData.exchange = korrelator::exchangeNameToString(exchange);
  generateJsonFile(modelData);
  if (m_model)
    m_model->AddData(std::move(modelData));
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
  ui->resetPercentageLine->setEnabled(enabled);
  ui->reverseCheckBox->setEnabled(enabled);
  ui->oneOpCheckBox->setEnabled(enabled);
  ui->graphThicknessCombo->setEnabled(enabled);
  // ui->liveTradeCheckbox->setEnabled(enabled);
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
                        return a.symbolName.compare(tokenName,
                                                    Qt::CaseInsensitive) == 0 &&
                               tt == a.tradeType && a.exchange == exchange;
                      });
}

MainDialog::list_iterator MainDialog::find(korrelator::token_list_t &container,
                                           QString const &tokenName) {
  return std::find_if(container.begin(), container.end(),
                      [&tokenName](korrelator::token_t const &a) {
                        return tokenName.compare(a.symbolName,
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
      token.symbolName = "*";
      token.calculatingNewMinMax = true;
      token.tradeType = tt;
      token.normalizedPrice = maxDoubleValue;
      m_tokens.push_back(std::move(token));
    }

    if (find(m_refs, tokenName, tt, exchange) == m_refs.end()) {
      korrelator::token_t token;
      token.tradeType = tt;
      token.symbolName = tokenName;
      token.calculatingNewMinMax = true;
      token.tradeType = tt;
      token.exchange = exchange;
      m_refs.push_back(std::move(token));
    }
  } else {
    if (find(m_tokens, tokenName, tt, exchange) == m_tokens.end()) {
      korrelator::token_t token;
      token.symbolName = tokenName;
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
    trade_type_e tradeType;
    exchange_name_e exchange;
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
    data.tradeType = trade_type_e::spot;
  else
    data.tradeType = trade_type_e::futures;
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
  auto const exchangeUrl = exchange == korrelator::exchange_name_e::binance
                               ? binance_spot_tokens_url
                               : kucoin_spot_tokens_url;
  if (cb)
    return sendNetworkRequest(QUrl(exchangeUrl), cb, trade_type_e::spot,
                              exchange);

  auto const currentSelectedExchange =
      static_cast<exchange_name_e>(ui->exchangeCombo->currentIndex());
  bool const updatingCombo = currentSelectedExchange == exchange;

  if (updatingCombo)
    ui->spotCombo->clear();

  auto &spots = m_watchables[(int)exchange].spots;
  if (!spots.empty() && updatingCombo) {
    for (auto const &d : spots)
      ui->spotCombo->addItem(d.symbolName.toUpper());
    return;
  }

  auto callback = [this, updatingCombo](korrelator::token_list_t &&list,
                                        exchange_name_e const exchange) {
    if (updatingCombo) {
      for (auto const &d : list)
        ui->spotCombo->addItem(d.symbolName.toUpper());
    }

    m_watchables[(int)exchange].spots = std::move(list);
    getExchangeInfo(exchange, trade_type_e::spot);
  };
  sendNetworkRequest(QUrl(exchangeUrl), callback, trade_type_e::spot, exchange);
}

void MainDialog::getFuturesTokens(korrelator::exchange_name_e const exchange,
                                  callback_t cb) {
  auto const exchangeUrl = exchange == korrelator::exchange_name_e::binance
                               ? binance_futures_tokens_url
                               : kucoin_futures_tokens_url;
  if (cb)
    return sendNetworkRequest(QUrl(exchangeUrl), cb, trade_type_e::futures,
                              exchange);

  auto const currentSelectedExchange =
      static_cast<exchange_name_e>(ui->exchangeCombo->currentIndex());
  bool const updatingCombo = currentSelectedExchange == exchange;

  if (updatingCombo)
    ui->futuresCombo->clear();

  auto &futures = m_watchables[(int)exchange].futures;
  if (!futures.empty() && updatingCombo) {
    for (auto const &d : futures)
      ui->futuresCombo->addItem(d.symbolName.toUpper());
    return;
  }

  auto callback = [this, updatingCombo](korrelator::token_list_t &&list,
                                        exchange_name_e const exchange) {
    if (updatingCombo) {
      for (auto const &d : list)
        ui->futuresCombo->addItem(d.symbolName.toUpper());
    }

    m_watchables[(int)exchange].futures = std::move(list);
    getExchangeInfo(exchange, trade_type_e::futures);
  };
  sendNetworkRequest(QUrl(exchangeUrl), callback, trade_type_e::futures,
                     exchange);
}

void MainDialog::getKuCoinExchangeInfo(trade_type_e const tradeType) {
  auto const fullUrl =
      https + (tradeType == trade_type_e::futures
                   ? korrelator::constants::kc_futures_api_host + QString("/")
                   : korrelator::constants::kucoin_https_spot_host +
                         QString("/api/v1/currencies"));
  QNetworkRequest request(fullUrl);
  request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader,
                    "application/json");
  korrelator::configureRequestForSSL(request);
  m_networkManager.enableStrictTransportSecurityStore(true);

  auto reply = m_networkManager.get(request);
  QObject::connect(reply, &QNetworkReply::finished, this, [=] {
    if (reply->error() != QNetworkReply::NoError) {
      QMessageBox::critical(this, "Error",
                            "Unable to get the list of all token pairs"
                            "=> " +
                                reply->errorString());
      return;
    }
    auto const responseString = reply->readAll();
    auto const rootObject = QJsonDocument::fromJson(responseString).object();
    if (auto codeIter = rootObject.find("code");
        codeIter == rootObject.end() || codeIter->toString() != "200000")
      return;
    auto const dataList = rootObject.value("data").toArray();
    if (dataList.isEmpty())
      return;
    auto &container = tradeType == trade_type_e::futures
                          ? m_watchables[(int)exchange_name_e::kucoin].futures
                          : m_watchables[(int)exchange_name_e::kucoin].spots;

    for (int i = 0; i < dataList.size(); ++i) {
      auto const itemObject = dataList[i].toObject();
      auto const currency = itemObject.value("currency").toString();
      auto const precision = itemObject.value("precision").toInt();

      auto iter = std::find_if(container.begin(), container.end(),
                               [currency](korrelator::token_t const &a) {
                                 return a.baseCurrency.compare(
                                            currency, Qt::CaseInsensitive) == 0;
                               });
      if (iter != container.end())
        iter->quotePrecision = precision;
    }
  });

  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}

void MainDialog::getExchangeInfo(exchange_name_e const exchange,
                                 trade_type_e const tradeType) {
  if (exchange == exchange_name_e::kucoin)
    return getKuCoinExchangeInfo(tradeType);

  if (exchange != exchange_name_e::binance)
    return;

  auto const fullUrl =
      https + (tradeType == trade_type_e::futures
                   ? korrelator::constants::binance_http_futures_host +
                         QString("/fapi/v1/exchangeInfo")
                   : korrelator::constants::binance_http_spot_host +
                         QString("/api/v3/exchangeInfo"));
  QNetworkRequest request(fullUrl);
  request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader,
                    "application/json");
  korrelator::configureRequestForSSL(request);
  m_networkManager.enableStrictTransportSecurityStore(true);

  auto reply = m_networkManager.get(request);
  QObject::connect(reply, &QNetworkReply::finished, this, [=] {
    if (reply->error() != QNetworkReply::NoError) {
      QMessageBox::critical(this, "Error",
                            "Unable to get the list of all token pairs"
                            "=> " +
                                reply->errorString());
      return;
    }
    auto const responseString = reply->readAll();
    QJsonObject const rootObject =
        QJsonDocument::fromJson(responseString).object();
    QJsonArray const symbols = rootObject.value("symbols").toArray();

    auto &container = tradeType == trade_type_e::futures
                          ? m_watchables[(int)exchange].futures
                          : m_watchables[(int)exchange].spots;

    for (int i = 0; i < symbols.size(); ++i) {
      auto const symbolObject = symbols[i].toObject();
      auto const tokenName = symbolObject.value("pair").toString();
      auto const status = symbolObject.value("status").toString();

      auto iter = std::lower_bound(container.begin(), container.end(),
                                   tokenName, korrelator::token_compare_t{});
      if (iter != container.end() &&
          iter->symbolName.compare(tokenName, Qt::CaseInsensitive) == 0) {
        if (status.compare("trading", Qt::CaseInsensitive) != 0)
          container.erase(iter);
        else {
          iter->pricePrecision =
              (uint8_t)symbolObject.value("pricePrecision").toInt();
          iter->quantityPrecision =
              (uint8_t)symbolObject.value("quantityPrecision").toInt();
          iter->baseAssetPrecision =
              (uint8_t)symbolObject.value("baseAssetPrecision").toInt();
          iter->quotePrecision =
              (uint8_t)symbolObject.value("quotePrecision").toInt();
        }
      }
    }
    if (!symbols.isEmpty()) {
      auto combo =
          trade_type_e::futures == tradeType ? ui->futuresCombo : ui->spotCombo;
      combo->clear();
      for (auto const &d : container)
        combo->addItem(d.symbolName.toUpper());
    }
  });
  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}

void MainDialog::saveTokensToFile() {
  if (ui->tokenListWidget->count() == 0)
    return;

  QFile file{korrelator::constants::korrelator_json_filename};
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return;

  QJsonArray jsonList;
  for (int i = 0; i < ui->tokenListWidget->count(); ++i) {
    QListWidgetItem *item = ui->tokenListWidget->item(i);
    QJsonObject obj;
    auto const displayedName = item->text();
    auto const &d = tokenNameFromWidgetName(displayedName);
    obj["symbol"] = d.tokenName.toLower();
    obj["market"] = d.tradeType == trade_type_e::spot ? "spot" : "futures";
    obj["ref"] = displayedName.endsWith('*');
    obj["exchange"] = korrelator::exchangeNameToString(d.exchange);
    jsonList.append(obj);
  }
  file.write(QJsonDocument(jsonList).toJson());
}

void MainDialog::updateTradeConfigurationPrecisions() {
  if (m_tradeConfigDataList.empty())
    return;

  for (auto &tradeConfig : m_tradeConfigDataList) {
    if (tradeConfig.exchange != exchange_name_e::binance)
      continue;
    auto const &tokens = tradeConfig.tradeType == trade_type_e::futures
                             ? m_watchables[(int)tradeConfig.exchange].futures
                             : m_watchables[(int)tradeConfig.exchange].spots;

    auto iter =
        std::lower_bound(tokens.cbegin(), tokens.cend(), tradeConfig.symbol,
                         korrelator::token_compare_t{});
    if (iter != tokens.end() &&
        iter->symbolName.compare(tradeConfig.symbol, Qt::CaseInsensitive) ==
            0) {
      tradeConfig.pricePrecision = iter->pricePrecision;
      tradeConfig.baseAssetPrecision = iter->baseAssetPrecision;
      tradeConfig.quantityPrecision = iter->quantityPrecision;
      tradeConfig.quotePrecision = iter->quotePrecision;
    }
  }
}

void MainDialog::readTokensFromFile() {
  using korrelator::constants;

  QJsonArray jsonList;
  {
    if (QFileInfo::exists("brocolli.json"))
      QFile::rename("brocolli.json", "korrelator.json");

    QDir().mkpath(constants::root_dir);
    if (QFile file("korrelator.json"); file.exists()) {
      if (file.copy("korrelator.json", constants::korrelator_json_filename))
        file.remove("korrelator.json");
    }

    QFile file{constants::korrelator_json_filename};
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
                               ? trade_type_e::spot
                               : trade_type_e::futures;
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

void MainDialog::readOrdersConfigFromFile() {
  using korrelator::constants;
  QByteArray fileContent;
  {
    QFile file(constants::trade_json_filename);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
      return;
    fileContent = file.readAll();
  }
  auto const jsonObject = QJsonDocument::fromJson(fileContent).object();
  if (jsonObject.isEmpty()) {
    QMessageBox::critical(this, "Error",
                          "The trade configuration file is empty");
    return;
  }

  m_tradeConfigDataList.clear();
  for (auto const &key : jsonObject.keys()) {
    auto const exchange = korrelator::stringToExchangeName(key);
    if (exchange == exchange_name_e::none)
      continue;
    auto const dataList = jsonObject.value(key).toArray();
    for (int i = 0; i < dataList.size(); ++i) {
      auto const object = dataList[i].toObject();
      if (object.isEmpty())
        continue;

      korrelator::trade_config_data_t data;
      data.symbol = object.value("symbol").toString().toUpper();
      auto const side = object.value("side").toString().toLower().trimmed();
      if (side == "buy")
        data.side = korrelator::trade_action_e::buy;
      else if (side == "sell")
        data.side = korrelator::trade_action_e::sell;
      else if (!side.isEmpty()) {
        QMessageBox::critical(
            this, "Error",
            QString("[%1] with symbol '%2' has erratic 'SIDE', leave it"
                    " empty instead.")
                .arg(key, data.symbol));
        continue;
      }
      auto const tradeType = object.value("tradeType").toString();
      if (tradeType.indexOf("futures") != -1)
        data.tradeType = trade_type_e::futures;
      else if (tradeType.indexOf("spot") != -1)
        data.tradeType = trade_type_e::spot;
      else {
        QMessageBox::critical(
            this, "Error",
            QString("[%1] with symbol '%2' has erratic 'tradeType'")
                .arg(key, data.symbol));
        continue;
      }
      data.exchange = exchange;
      data.marketType = object.value("marketType").toString();
      data.size = object.value("size").toDouble();
      data.baseAmount = object.value("baseAmount").toDouble();
      if (data.marketType.compare("market", Qt::CaseInsensitive) == 0) {
        if (data.baseAmount == 0.0) {
          QMessageBox::critical(this, "Error",
                                tr("You need to specify the size or the balance"
                                   " for %1/%2/%3")
                                    .arg(key, data.symbol, side));
          continue;
        }
      } else if (data.marketType.compare("limit", Qt::CaseInsensitive) == 0) {
        /*if (data.size == 0.0 && data.baseAmount == 0.0) {
          QMessageBox::critical(this, "Error",
                                tr("You need to specify the size and the
        balance" " for %1/%2/%3") .arg(key, data.symbol, side)); continue;
        }*/
      }

      if (data.tradeType == trade_type_e::futures)
        data.leverage = object.value("leverage").toDouble();
      m_tradeConfigDataList.push_back(std::move(data));
    }
  }

  std::sort(m_tradeConfigDataList.begin(), m_tradeConfigDataList.end(),
            [](auto const &a, auto const &b) {
              return std::tuple(a.exchange, a.symbol.toLower()) <
                     std::tuple(b.exchange, b.symbol.toLower());
            });
  updateTradeConfigurationPrecisions();
}

void MainDialog::addNewItemToTokenMap(
    QString const &tokenName, trade_type_e const tt,
    korrelator::exchange_name_e const exchange) {
  auto const text = tokenName.toUpper() +
                    (tt == trade_type_e::spot ? "_SPOT" : "_FUTURES") +
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
                                    trade_type_e const tradeType,
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
          t.symbolName = tokenObject.value("symbol").toString().toLower();
          if (isKuCoin) {
            if (tokenObject.contains("baseCurrency"))
              t.baseCurrency = tokenObject.value("baseCurrency").toString();

            if (tokenObject.contains("quoteCurrency"))
              t.quoteCurrency = tokenObject.value("quoteCurrency").toString();

            if (!tokenObject.contains(key)) {
              if (auto const f = tokenObject.value(key2); f.isString())
                t.realPrice = f.toString().toDouble();
              else
                t.realPrice = f.toDouble();
            } else {
              t.realPrice = tokenObject.value(key).toString().toDouble();
            }
          } else {
            t.realPrice = tokenObject.value(key).toString().toDouble();
          }
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
    auto legendName = [](QString const &key, trade_type_e const tt) {
      if (key.size() == 1 && key[0] == '*') // "ref"
        return QString("ref");
      return (key.toUpper() +
              (tt == trade_type_e::spot ? "_SPOT" : "_FUTURES"));
    };
    int i = 0;
    for (auto &value : m_tokens) {
      value.graph = ui->customPlot->addGraph();
      auto const color = colors[i % (sizeof(colors) / sizeof(colors[0]))];
      value.graph->setPen(
          QPen(color, ui->graphThicknessCombo->currentIndex() + 1));

      qDebug() << value.graph->pen().width();

      value.graph->setAntialiasedFill(true);
      value.legendName = legendName(value.symbolName, value.tradeType);
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
    for (auto &value : list) {
      if (value.tradeType != tt || value.symbolName.size() == 1)
        continue;
      auto iter =
          find(result, value.symbolName, value.tradeType, value.exchange);
      if (iter != result.end()) {
        value.realPrice = iter->realPrice;
        value.calculatingNewMinMax = true;
        updateTokenIter(value);
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
        m_tokens[0].normalizedPrice = (price / (double)m_refs.size());
      }
      // after successfully getting the SPOTs and FUTURES' prices,
      // start the websocket and get real-time updates.
      startWebsocket();
    }
  };

  std::set<exchange_name_e> exchanges;
  for (auto &t : m_refs)
    exchanges.insert(t.exchange);
  for (auto &t : m_tokens)
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
  if (m_doingManualReset)
    m_resetPercentage /= 100.0;

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
  m_lastTradeAction = korrelator::trade_action_e::nothing;
  korrelator::maxVisiblePlot = getMaxPlotsInVisibleRegion();

  ui->startButton->setText("Stop");
  resetGraphComponents();
  enableUIComponents(false);
  setupGraphData();

  m_hasReferences = false;

  if (auto iter = find(m_tokens, "*"); iter != m_tokens.end()) {
    if (iter != m_tokens.begin())
      std::iter_swap(iter, m_tokens.begin());
    m_hasReferences = true;
  }

  setupOrderTableModel();
  getInitialTokenPrices();
}

void MainDialog::calculatePriceNormalization() {
  for (auto &token : m_tokens)
    korrelator::updateTokenIter(token);
  for (auto &token : m_refs)
    korrelator::updateTokenIter(token);
}

void MainDialog::startWebsocket() {
  // price updater
  m_priceUpdater.worker = std::make_unique<korrelator::Worker>([this] {
    m_websocket = std::make_unique<korrelator::websocket_manager>();

    for (auto &tokenInfo : m_refs) {
      m_websocket->addSubscription(tokenInfo.symbolName, tokenInfo.tradeType,
                                   tokenInfo.exchange, tokenInfo.realPrice);
    }

    for (auto &tokenInfo : m_tokens) {
      if (tokenInfo.symbolName.size() != 1)
        m_websocket->addSubscription(tokenInfo.symbolName, tokenInfo.tradeType,
                                     tokenInfo.exchange, tokenInfo.realPrice);
    }

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
  return korrelator::trade_action_e::nothing;
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
  double normalizedPrice = 0.0;
  for (auto const &v : m_refs)
    normalizedPrice += v.normalizedPrice;
  normalizedPrice /= ((double)m_refs.size());
  value.normalizedPrice = normalizedPrice * value.alpha;

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
  auto &refSymbol = m_tokens[0];
  double const prevRef =
      (m_hasReferences) ? refSymbol.normalizedPrice : maxDoubleValue;

  // update ref symbol data on the graph
  auto refResult = (!m_hasReferences)
                       ? korrelator::ref_calculation_data_t()
                       : updateRefGraph(keyStart, key, updatingMinMax);
  if (!m_hasReferences)
    return;

  double currentRef = refSymbol.normalizedPrice;
  bool isResettingSymbols = false;

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

    if (crossOverDecision != trade_action_e::nothing) {
      auto &crossOver = value.crossOver.emplace();
      crossOver.signalPrice = value.realPrice;
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
            (value.tradeType == trade_type_e::spot ? "SPOT" : "FUTURES");
        data.signalPrice = crossOverValue.signalPrice;
        data.openPrice = value.realPrice;
        data.symbol = value.symbolName;
        data.openTime =
            QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        data.signalTime = crossOverValue.time;

        emit newOrderDetected(std::move(crossOverValue), std::move(data),
                              value.exchange, value.tradeType);
        value.crossedOver = false;
        value.crossOver.reset();
      }
    }

    if (refResult.eachTickNormalize || m_doingManualReset) {
      currentRef /= refSymbol.alpha;
      auto const distanceFromRefToSymbol = // a
          ((value.normalizedPrice > currentRef)
               ? (value.normalizedPrice / currentRef)
               : (currentRef / value.normalizedPrice)) -
          1.0;
      auto const distanceThreshold = m_specialRef; // b
      bool const resettingRef =
          m_doingManualReset && distanceFromRefToSymbol > m_resetPercentage;

      if (refResult.eachTickNormalize || resettingRef) {
        if (value.normalizedPrice > currentRef) {
          refSymbol.alpha =
              ((distanceFromRefToSymbol + 1.0) / (distanceThreshold + 1.0));
        } else {
          refSymbol.alpha =
              ((distanceThreshold + 1.0) / (distanceFromRefToSymbol + 1.0));
        }
        if (resettingRef)
          refResult.isResettingRef = true;
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

  if (refResult.isResettingRef || isResettingSymbols) {
    resetTickerData(refResult.isResettingRef, isResettingSymbols);
  }

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
  calculatePriceNormalization();
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

void MainDialog::generateJsonFile(korrelator::model_data_t const &modelData) {
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

void MainDialog::onSettingsDialogClicked() {
  auto dialog = new SettingsDialog{this};
  QObject::connect(dialog, &SettingsDialog::finished, this,
                   [this, dialog] { m_apiTradeApiMap = dialog->apiDataMap(); });
  QObject::connect(dialog, &SettingsDialog::finished, dialog,
                   &SettingsDialog::deleteLater);
  dialog->open();
}

void MainDialog::sendExchangeRequest(
    korrelator::model_data_t const &modelData,
    exchange_name_e const exchange,
    trade_type_e const tradeType,
    korrelator::cross_over_data_t const &crossOver) {
  auto iter = m_apiTradeApiMap.find(exchange);
  if (iter == m_apiTradeApiMap.end())
    return;

  if (!m_tradeConfigDataList.empty() &&
      m_tradeConfigDataList[0].pricePrecision == -1)
    updateTradeConfigurationPrecisions();

  auto configIterPair = std::equal_range(
      m_tradeConfigDataList.begin(), m_tradeConfigDataList.end(), exchange,
      [symbol = modelData.symbol.toLower()](auto const &a, auto const &b) {
        using a_type = std::decay_t<std::remove_cv_t<decltype(a)>>;
        if constexpr (std::is_same_v<exchange_name_e, a_type>)
          return std::tie(a, symbol) <
                 std::tuple(b.exchange, b.symbol.toLower());
        else
          return std::tuple(a.exchange, a.symbol.toLower()) <
                 std::tie(b, symbol);
      });

  if (configIterPair.first == configIterPair.second) {
    qDebug() << "Exchange not found";
    return;
  }

  std::vector<korrelator::trade_config_data_t *> tradeConfigPtrs;
  for (auto iter = configIterPair.first; iter != configIterPair.second;
       ++iter) {
    if (tradeType == iter->tradeType)
      tradeConfigPtrs.push_back(&(*iter));
  }

  if (tradeConfigPtrs.empty()) {
    qDebug() << "Cannot find tradeType of this account";
    return;
  }

  korrelator::trade_config_data_t *tradeConfigPtr = nullptr;
  for (auto &tradeConfig : tradeConfigPtrs) {
    if (crossOver.action == tradeConfig->side) {
      tradeConfigPtr = tradeConfig;
      break;
    }
  }

  if (!tradeConfigPtr) {
    qDebug() << "There was a problem";
    return;
  }

  auto &apiInfo = *iter;
  if (exchange == exchange_name_e::binance) {
    if (tradeType == trade_type_e::spot && !apiInfo.spotApiKey.isEmpty()) {
      binance_plugs.append(korrelator::plug_data_t{
          apiInfo, tradeConfigPtr, tradeType, crossOver.openPrice});
    } else if (tradeType == trade_type_e::futures &&
               !apiInfo.futuresApiKey.isEmpty()) {
      binance_plugs.append(korrelator::plug_data_t{
          apiInfo, tradeConfigPtr, tradeType, crossOver.openPrice});
    }
  } else if (exchange == exchange_name_e::kucoin) {
    if (tradeType == trade_type_e::spot && !apiInfo.spotApiKey.isEmpty())
      kc_plugs.append(korrelator::plug_data_t{apiInfo, tradeConfigPtr,
                                              tradeType, crossOver.openPrice});
    else if (tradeType == trade_type_e::futures &&
             !apiInfo.futuresApiKey.isEmpty())
      kc_plugs.append(korrelator::plug_data_t{apiInfo, tradeConfigPtr,
                                              tradeType, crossOver.openPrice});
  }
}

void MainDialog::tradeKuCoinToken(
    MainDialog* mainDialog, std::unique_ptr<korrelator::order_model>& model) {
  using korrelator::trade_action_e;
  using korrelator::trade_type_e;

  net::io_context ioContext;
  // delay fetching the SSL context so it can be initialized by the other thread
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto &sslContext = korrelator::getSSLContext();

  auto lastAction = trade_action_e::nothing;
  double lastQuantity = NAN;

  while (true) {
    auto kc_data = kc_plugs.get();

    if (lastAction != trade_action_e::nothing) {
      ioContext.restart();
      if (kc_data.tradeType == trade_type_e::futures)
        kc_data.tradeConfig->size = lastQuantity;
    }

    korrelator::kc_https_plug kcRequest(ioContext, sslContext,
                                        kc_data.tradeType, kc_data.apiInfo,
                                        kc_data.tradeConfig);
    kcRequest.setPrice(kc_data.tokenPrice);
    kcRequest.startConnect();
    ioContext.run();

    if (kc_data.tradeType == trade_type_e::futures &&
        lastAction == trade_action_e::nothing) {
      lastAction = kc_data.tradeConfig->side;
      lastQuantity = kc_data.tradeConfig->size * 2;
    }
    auto const quantityPurchased = kcRequest.quantityPurchased();
    auto const sizePurchased = kcRequest.sizePurchased();
    korrelator::model_data_t* modelData = nullptr;
    if (model)
      modelData = model->front();
    if (quantityPurchased != 0.0 && sizePurchased != 0.0) {
      qDebug() << "Price:" << ((quantityPurchased * sizePurchased) / 0.01);
    } else if (auto const errString = kcRequest.errorString();
               !errString.isEmpty()){
      qDebug() << errString;
      if (modelData)
        modelData->remark = errString;
    }

    ioContext.stop();
  }
}

void MainDialog::tradeBinanceToken(
    MainDialog*, std::unique_ptr<korrelator::order_model>& model) {
  using korrelator::trade_action_e;
  using korrelator::trade_type_e;

  net::io_context ioContext;
  auto lastAction = trade_action_e::nothing;
  double lastQuantity = NAN;
  auto &sslContext = korrelator::getSSLContext();

  while (true) {
    auto data = binance_plugs.get();

    if (lastAction != trade_action_e::nothing) {
      ioContext.restart();
      if (data.tradeType == trade_type_e::futures)
        data.tradeConfig->size = lastQuantity;
    }

    korrelator::binance_https_plug binanceRequest(
        ioContext, sslContext, data.tradeType, data.apiInfo, data.tradeConfig);
    binanceRequest.setPrice(data.tokenPrice);
    binanceRequest.startConnect();
    ioContext.run();

    // for futures only, after the first action has been completed, we
    // double the quantity to keep the position open.
    if (data.tradeType == trade_type_e::futures &&
        lastAction == trade_action_e::nothing) {
      lastAction = data.tradeConfig->side;
      lastQuantity = data.tradeConfig->size * 2;
    }

    ioContext.stop();
  }
}
