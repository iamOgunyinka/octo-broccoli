#include "maindialog.hpp"
#include "ui_maindialog.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <set>

#include "binance_symbols.hpp"
#include "constants.hpp"
#include "container.hpp"
#include "double_trader.hpp"
#include "ftx_symbols.hpp"
#include "kucoin_symbols.hpp"
#include "order_model.hpp"
#include "qcustomplot.h"
#include "single_trader.hpp"
#include "websocket_manager.hpp"

namespace korrelator {

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
  else if (ex == exchange_name_e::ftx)
    return "FTX";
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
  else if (name_.compare("ftx", Qt::CaseInsensitive) == 0)
    return exchange_name_e::ftx;
  return exchange_name_e::none;
}

void updateTokenIter(token_t &value) {
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

  crossedOver = false;
  calculatingNewMinMax = true;
  pricePrecision = quantityPrecision = baseAssetPrecision = quotePrecision = -1;
  minPrice = prevNormalizedPrice = CMAX_DOUBLE_VALUE;
  maxPrice = -(CMAX_DOUBLE_VALUE);
  alpha = 1.0;
  baseMinSize = 0.0;
  quoteMinSize = 0.0;
  normalizedPrice = realPrice = 0.0;
  multiplier = 1.0;
  tickSize = 0.0;
  graphPointsDrawnCount = 0;
  graph = nullptr;
  baseCurrency = quoteCurrency = legendName = "";
  crossOver.reset();
}

symbol_fetcher_t::symbol_fetcher_t()
    : binance{nullptr}, kucoin{nullptr}, ftx{nullptr} {}

symbol_fetcher_t::~symbol_fetcher_t() {
  binance.reset();
  kucoin.reset();
}

bool hasValidExchange(exchange_name_e const exchange) {
  return exchange == exchange_name_e::binance ||
      exchange_name_e::ftx == exchange ||
      exchange_name_e::kucoin == exchange;
}

} // namespace korrelator

MainDialog::MainDialog(bool &warnOnExit,
                       std::filesystem::path const configDirectory,
                       QWidget *parent)
    : QDialog(parent), ui(new Ui::MainDialog), m_websocket(nullptr),
      m_legendLayout(nullptr), m_configDirectory(configDirectory),
      m_warnOnExit(warnOnExit) {

  ui->setupUi(this);

  m_symbolUpdater.binance.reset(
      new korrelator::binance_symbols(m_networkManager));
  m_symbolUpdater.kucoin.reset(
      new korrelator::kucoin_symbols(m_networkManager));
  m_symbolUpdater.ftx.reset(new korrelator::ftx_symbols(m_networkManager));

  setWindowIcon(qApp->style()->standardPixmap(QStyle::SP_DesktopIcon));
  setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint |
                 Qt::WindowMaximizeButtonHint);

  ui->restartTickCombo->addItems(
      {"Normal lines", "Ref line", "All lines", "Special"});

  registerCustomTypes();
  populateUIComponents();
  connectAllUISignals();

  readAppConfigFromFile();
  readTradesConfigFromFile();

  m_apiTradeApiMap = SettingsDialog::getApiDataMap(m_configDirectory.string());
  if (m_apiTradeApiMap.empty()) {
    QMessageBox::information(this, "Information",
                             "To automate orders, please use the settings "
                             "button to add new API information");
  }

  std::thread{[this] {
    auto cb = [this] { refreshModel(); };
    tradeExchangeTokens(std::move(cb), m_tokenPlugs, m_model, m_maxOrderRetries,
                        m_expectedTradeCount);
  }}.detach();

  std::thread{[this] {
    updatePlottingKey(m_graphKeys, ui->customPlot, m_maxVisiblePlot);
  }}.detach();

  QTimer::singleShot(std::chrono::milliseconds(500), this, [this] {
    for (auto const &exchange :
         {exchange_name_e::ftx, exchange_name_e::kucoin}) {
      getSpotsTokens(exchange);
      getFuturesTokens(exchange);
    }
  });
}

void MainDialog::connectRestartTickSignal() {
  auto getValue =
      [this](int const index) -> std::optional<korrelator::rot_metadata_t> {
    if (index == 0)
      return m_restartTickValues.normalLines;
    else if (index == 1)
      return m_restartTickValues.refLines;
    else if (index == 3)
      return m_restartTickValues.special;
    return std::nullopt; // should almost never happen
  };

  auto displayValuesInGUI =
      [this](std::optional<korrelator::rot_metadata_t> const &rotMetadata) {
        if (!rotMetadata.has_value())
          return;
        if (rotMetadata->restartOnTickEntry != 0.0)
          ui->restartTickLine->setText(
              QString::number(rotMetadata->restartOnTickEntry));

        if (auto const value = rotMetadata->percentageEntry; value != 0.0)
          ui->resetPercentageLine->setText(QString::number(value));

        if (auto const value = rotMetadata->specialEntry; value != 0.0)
          ui->specialLine->setText(QString::number(value));
      };

  QObject::connect(
      ui->restartTickCombo,
      static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
      this, [this, displayValuesInGUI, getValue](int const index) {
        using korrelator::tick_line_type_e;
        ui->specialLine->setText("");
        ui->restartTickLine->setText("");
        ui->resetPercentageLine->setText("");

        if (index == 2)
          return;
        displayValuesInGUI(getValue(index));
        ui->restartTickLine->setFocus();
      });
}

MainDialog::~MainDialog() {
  stopGraphPlotting();
  resetGraphComponents();
  m_websocket.reset();
  saveAppConfigToFile();

  delete ui;
}

void MainDialog::closeEvent(QCloseEvent *closeEvent) {
  if (m_warnOnExit && m_programIsRunning) {
    auto const response = QMessageBox::question(
        this, tr("Close"), tr("Are you sure you want to close this window?"));
    if (response == QMessageBox::No)
      return closeEvent->ignore();
  }
  closeEvent->accept();
}

void MainDialog::takeBackToFactoryReset() {
  enableUIComponents(false);
  ui->startButton->setEnabled(false);

  for (auto &token : m_tokens)
    token.reset();

  for (auto &token : m_refs)
    token.reset();

  readTradesConfigFromFile();

  std::vector<exchange_name_e> const exchanges{
      exchange_name_e::binance, exchange_name_e::kucoin, exchange_name_e::ftx};
  for (auto const &exchange : exchanges) {
    m_watchables[(int)exchange].futures.clear();
    m_watchables[(int)exchange].spots.clear();
    getSpotsTokens(exchange);
    getFuturesTokens(exchange);
  }

  QTimer::singleShot(std::chrono::seconds(7), this, [this] {
    ui->startButton->setEnabled(true);
    onStartVerificationSuccessful();
  });
}

void MainDialog::registerCustomTypes() {
  qRegisterMetaType<korrelator::model_data_t>();
  qRegisterMetaType<korrelator::cross_over_data_t>();
  qRegisterMetaType<korrelator::exchange_name_e>();
  qRegisterMetaType<korrelator::trade_type_e>();
  qRegisterMetaType<QVector<int>>();
}

void MainDialog::populateUIComponents() {
  // value initialize normal and refLine's `restartOnTickEntry` data to 2500
  m_restartTickValues.normalLines.emplace().restartOnTickEntry = 2500;
  m_restartTickValues.refLines.emplace().restartOnTickEntry = 2500;

  ui->resetPercentageLine->setValidator(new QDoubleValidator);
  ui->maxRetriesLine->setValidator(new QIntValidator(1, 100));
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
  ui->maxRetriesLine->setText("10");

#ifdef TESTNET
  ui->liveTradeCheckbox->setChecked(true);
#endif
}

bool MainDialog::setRestartTickRowValues(
    std::optional<korrelator::rot_metadata_t> &optValue) {
  auto const optRestartValue = getIntegralValue(ui->restartTickLine);
  auto const optPercentage = getIntegralValue(ui->resetPercentageLine);
  auto const optSpecialEntry = getIntegralValue(ui->specialLine);

  if (!(optRestartValue && optPercentage && optSpecialEntry)) {
    optValue.reset();
    return false;
  }

  auto &v = optValue.emplace();
  v.restartOnTickEntry = *optRestartValue;
  v.percentageEntry = v.specialEntry = v.afterDivisionPercentageEntry =
      v.afterDivisionSpecialEntry = 0.0;
  if (optPercentage.has_value())
    v.percentageEntry = *optPercentage;
  if (optSpecialEntry.has_value())
    v.specialEntry = *optSpecialEntry;

  if (v.percentageEntry != 0.0)
    v.afterDivisionPercentageEntry = v.percentageEntry / 100.0;
  if (v.specialEntry != 0.0)
    v.afterDivisionSpecialEntry = v.specialEntry / 100.0;
  return true;
}

void MainDialog::onApplyButtonClicked() {
  using korrelator::tick_line_type_e;

  bool valueSet = false;
  int const index = ui->restartTickCombo->currentIndex();
  if (index == 0) {
    valueSet = setRestartTickRowValues(m_restartTickValues.normalLines);
    m_restartTickValues.special.reset();
  } else if (index == 1) {
    valueSet = setRestartTickRowValues(m_restartTickValues.refLines);
    m_restartTickValues.special.reset();
  } else if (index == 2) {
    valueSet = setRestartTickRowValues(m_restartTickValues.refLines);
    setRestartTickRowValues(m_restartTickValues.normalLines);
    m_restartTickValues.special.reset();
  } else {
    setRestartTickRowValues(m_restartTickValues.special);
    m_restartTickValues.normalLines.reset();
    m_restartTickValues.refLines.reset();
  }

  if (valueSet)
    m_restartTickValues.special.reset();
  return ui->restartTickLine->setFocus();
}

void MainDialog::connectAllUISignals() {
  connectRestartTickSignal();

  QObject::connect(
      ui->exchangeCombo,
      static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
      this, [this](int const index) {
        auto const exchange = static_cast<exchange_name_e>(index);
        getSpotsTokens(exchange);
        getFuturesTokens(exchange);
      });
  QObject::connect(ui->doubleTradeCheck, &QCheckBox::toggled, this,
                   [this](bool const checked)
  {
    if (checked)
      m_expectedTradeCount = 2;
    else
      m_expectedTradeCount = 1;
  });
  ui->exchangeCombo->addItems({"Binance", "FTX", "KuCoin"});

  QObject::connect(ui->applySpecialButton, &QPushButton::clicked, this,
                   &MainDialog::onApplyButtonClicked);
  QObject::connect(this, &MainDialog::newOrderDetected, this,
                   [this](auto a, auto b, exchange_name_e const exchange,
                          trade_type_e const tt) {
                     onNewOrderDetected(std::move(a), std::move(b), exchange,
                                        tt);
                   });
  /*
  QObject::connect(ui->openConfigFolderButton, &QPushButton::clicked, this, [] {
    QProcess process;
    process.setWorkingDirectory(".");
    process.startDetached("explorer", QStringList()
                                          << korrelator::constants::root_dir);
  });
  */
  QObject::connect(
      ui->selectionCombo,
      static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
      this, [this](int const) {
        m_maxVisiblePlot = getMaxPlotsInVisibleRegion();
        if (!m_programIsRunning) {
          double const key = m_elapsedTime.elapsed() / 1'000.0;
          ui->customPlot->xAxis->setRange(key, m_maxVisiblePlot,
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
    saveAppConfigToFile();
  });

  QObject::connect(ui->spotPrevButton, &QToolButton::clicked, this, [this] {
    auto currentRow = ui->tokenListWidget->currentRow();
    if (currentRow < 0 || currentRow >= ui->tokenListWidget->count())
      return;
    auto item = ui->tokenListWidget->takeItem(currentRow);
    tokenRemoved(item->text());
    delete item;
    saveAppConfigToFile();
  });

  QObject::connect(ui->startButton, &QPushButton::clicked, this,
                   &MainDialog::onOKButtonClicked);

  QObject::connect(ui->futuresNextButton, &QToolButton::clicked, this, [this] {
    auto const tokenName = ui->futuresCombo->currentText().trimmed();
    if (tokenName.isEmpty())
      return;

    auto const exchange =
        static_cast<exchange_name_e>(ui->exchangeCombo->currentIndex());
    addNewItemToTokenMap(tokenName, trade_type_e::futures, exchange);
    saveAppConfigToFile();
  });

  QObject::connect(ui->applyUmbralButton, &QPushButton::clicked, this, [this] {
    auto const optThreshold = getIntegralValue(ui->umbralLine);
    if (!optThreshold.has_value())
      return;
    m_findingUmbral = *optThreshold != 0.0;
    if (m_findingUmbral)
      m_threshold /= 100.0;
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

  modelData.side = korrelator::actionTypeToString(currentAction);
  modelData.exchange = korrelator::exchangeNameToString(exchange);
  modelData.userOrderID = QString::number(QRandomGenerator::global()->generate());

  if (m_model)
    m_model->AddData(modelData);
  m_model->front()->friendModel = nullptr;

  if (ui->liveTradeCheckbox->isChecked() && !m_apiTradeApiMap.empty()) {
    sendExchangeRequest(modelData, exchange, tradeType, crossOver.action,
                        crossOver.openPrice);
    if (m_model) {
      auto const &modelDataPtr = m_model->front();
      if (modelDataPtr && modelDataPtr->friendModel) {
        m_model->AddData(*modelDataPtr->friendModel);
        delete modelDataPtr->friendModel;
        modelDataPtr->friendModel = nullptr;
      }
      m_model->refreshModel();
    }
  }

  generateJsonFile(modelData);
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
  ui->applySpecialButton->setEnabled(enabled);
  ui->specialLine->setEnabled(enabled);
  ui->specialLine->setEnabled(enabled);
  ui->exchangeCombo->setEnabled(enabled);
  ui->resetPercentageLine->setEnabled(enabled);
  ui->reverseCheckBox->setEnabled(enabled);
  ui->oneOpCheckBox->setEnabled(enabled);
  ui->graphThicknessCombo->setEnabled(enabled);
  ui->maxRetriesLine->setEnabled(enabled);
  ui->doubleTradeCheck->setEnabled(enabled);
}

void MainDialog::stopGraphPlotting() {
  m_timerPlot.stop();

  ui->futuresNextButton->setEnabled(true);
  ui->spotNextButton->setEnabled(true);
  ui->spotPrevButton->setEnabled(true);
  ui->startButton->setText("Start");

  // m_timerPlot.disconnect(m_graphUpdater.worker.get());
  m_graphUpdater.worker.reset();
  m_graphUpdater.thread.reset();
  m_priceUpdater.worker.reset();
  m_priceUpdater.thread.reset();

  m_programIsRunning = false;
  m_firstRun = false;
  m_lastTradeAction = korrelator::trade_action_e::nothing;
  enableUIComponents(true);

  // for (auto &token : m_tokens)
  //  token.reset();

  // for (auto &token : m_refs)
  //  token.reset();

  korrelator::plug_data_t data;
  data.tradeType = trade_type_e::unknown;
  m_tokenPlugs.append(std::move(data));
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
      token.normalizedPrice = CMAX_DOUBLE_VALUE;
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
  auto errorCallback = [this](QString const &errorMessage) {
    QMessageBox::critical(this, "Error", errorMessage);
  };

  if (cb) {
    if (exchange == exchange_name_e::binance) {
      return m_symbolUpdater.binance->getSpotsSymbols(cb, errorCallback);
    } else if (exchange_name_e::kucoin == exchange) {
      return m_symbolUpdater.kucoin->getSpotsSymbols(cb, errorCallback);
    } else if (exchange == exchange_name_e::ftx) {
      return m_symbolUpdater.ftx->getSpotsSymbols(cb, errorCallback);
    }
  }

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
  if (exchange == exchange_name_e::binance) {
    return m_symbolUpdater.binance->getSpotsSymbols(callback, errorCallback);
  } else if (exchange_name_e::kucoin == exchange) {
    return m_symbolUpdater.kucoin->getSpotsSymbols(callback, errorCallback);
  } else if (exchange == exchange_name_e::ftx) {
    return m_symbolUpdater.ftx->getSpotsSymbols(callback, errorCallback);
  }
}

void MainDialog::getFuturesTokens(korrelator::exchange_name_e const exchange,
                                  callback_t cb) {
  auto errorCallback = [this](QString const &errorMessage) {
    QMessageBox::critical(this, "Error", errorMessage);
  };

  if (cb) {
    if (exchange == exchange_name_e::binance) {
      return m_symbolUpdater.binance->getFuturesSymbols(cb, errorCallback);
    } else if (exchange_name_e::kucoin == exchange) {
      return m_symbolUpdater.kucoin->getFuturesSymbols(cb, errorCallback);
    } else if (exchange == exchange_name_e::ftx) {
      return m_symbolUpdater.ftx->getFuturesSymbols(cb, errorCallback);
    }
  }

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

  if (exchange == exchange_name_e::binance) {
    return m_symbolUpdater.binance->getFuturesSymbols(callback, errorCallback);
  } else if (exchange_name_e::kucoin == exchange) {
    return m_symbolUpdater.kucoin->getFuturesSymbols(callback, errorCallback);
  } else if (exchange == exchange_name_e::ftx) {
    return m_symbolUpdater.ftx->getFuturesSymbols(callback, errorCallback);
  }
}

void MainDialog::getExchangeInfo(exchange_name_e const exchange,
                                 trade_type_e const tradeType) {

  auto errorCallback = [this](QString const &errorMessage) {
    QMessageBox::critical(this, "Error", errorMessage);
  };

  auto &container = m_watchables[(int)exchange];
  if (exchange == exchange_name_e::binance) {
    auto successCallback = [this, &container, tradeType] {
      auto &tokenList = tradeType == trade_type_e::futures ? container.futures
                                                           : container.spots;
      auto combo =
          trade_type_e::futures == tradeType ? ui->futuresCombo : ui->spotCombo;
      combo->clear();
      for (auto const &t : tokenList)
        combo->addItem(t.symbolName.toUpper());
    };

    if (tradeType == trade_type_e::spot) {
      return m_symbolUpdater.binance->getSpotsExchangeInfo(
          &container.spots, successCallback, errorCallback);
    } else {
      return m_symbolUpdater.binance->getFuturesExchangeInfo(
          &container.futures, successCallback, errorCallback);
    }
  } else if (exchange == exchange_name_e::kucoin) {
    if (tradeType == trade_type_e::spot) {
      return m_symbolUpdater.kucoin->getSpotsExchangeInfo(&container.spots,
                                                          errorCallback);
    } else {
      return m_symbolUpdater.kucoin->getFuturesExchangeInfo(&container.futures,
                                                            errorCallback);
    }
  } else if (exchange == exchange_name_e::ftx) {
    if (tradeType == trade_type_e::spot) {
      return m_symbolUpdater.ftx->getSpotsExchangeInfo(&container.spots,
                                                       errorCallback);
    } else {
      return m_symbolUpdater.ftx->getFuturesExchangeInfo(&container.spots,
                                                         errorCallback);
    }
  }
}

QJsonObject getJsonObjectFromRot(korrelator::rot_metadata_t const &v,
                                 QString const &name) {
  QJsonObject result;
  result["name"] = name;
  result["specialV"] = QString::number(v.specialEntry);
  result["percentageV"] = QString::number(v.percentageEntry);
  result["restartV"] = QString::number(v.restartOnTickEntry);
  return result;
}

void MainDialog::saveAppConfigToFile() {
  using korrelator::tick_line_type_e;

  if (ui->tokenListWidget->count() == 0)
    return;

  auto const filenameFs =
      m_configDirectory / korrelator::constants::app_json_filename;
  auto const filename = filenameFs.string();
  QFile file{filename.c_str()};
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return;

  QJsonObject rootObject;
  rootObject["doubleTrade"] = ui->doubleTradeCheck->isChecked();
  rootObject["umbral"] = ui->umbralLine->text().trimmed().toDouble();
  rootObject["graphThickness"] = ui->graphThicknessCombo->currentText().toInt();
  rootObject["maxRetries"] = ui->maxRetriesLine->text().trimmed().toInt();
  rootObject["reverse"] = ui->reverseCheckBox->isChecked();
  rootObject["liveTrade"] = ui->liveTradeCheckbox->isChecked();

  {
    QJsonArray jsonTicks;
    if (auto &special = m_restartTickValues.special; special.has_value())
      jsonTicks.append(getJsonObjectFromRot(*special, "Special"));

    if (auto &value = m_restartTickValues.normalLines; value.has_value())
      jsonTicks.append(getJsonObjectFromRot(*value, "normalLine"));

    if (auto &ref = m_restartTickValues.refLines; ref.has_value())
      jsonTicks.append(getJsonObjectFromRot(*ref, "refLine"));

    if (!jsonTicks.isEmpty())
      rootObject["ticks"] = jsonTicks;
  }

  QJsonArray tokensJsonList;
  for (int i = 0; i < ui->tokenListWidget->count(); ++i) {
    QListWidgetItem *item = ui->tokenListWidget->item(i);
    QJsonObject obj;
    auto const displayedName = item->text();
    auto const &d = tokenNameFromWidgetName(displayedName);
    obj["symbol"] = d.tokenName.toLower();
    obj["market"] = d.tradeType == trade_type_e::spot ? "spot" : "futures";
    obj["ref"] = displayedName.endsWith('*');
    obj["exchange"] = korrelator::exchangeNameToString(d.exchange);
    tokensJsonList.append(obj);
  }
  rootObject["tokens"] = tokensJsonList;
  file.write(QJsonDocument(rootObject).toJson());
}

void MainDialog::updateTradeConfigurationPrecisions() {
  if (m_tradeConfigDataList.empty())
    return;

  for (auto &tradeConfig : m_tradeConfigDataList) {
    if (tradeConfig.exchange == exchange_name_e::kucoin)
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
      tradeConfig.quoteCurrency = iter->quoteCurrency;
      tradeConfig.baseCurrency = iter->baseCurrency;
      tradeConfig.tickSize = iter->tickSize;
      tradeConfig.quoteMinSize = iter->quoteMinSize;
    }
  }
}

void MainDialog::readAppConfigFromFile() {
  using korrelator::constants;
  using korrelator::tick_line_type_e;

  QJsonArray tokenJsonList;

  {
    auto const appFilename =
        (m_configDirectory / constants::app_json_filename).string();
    QDir().mkpath(m_configDirectory.string().c_str());

    {
      auto const oldFilename =
          (m_configDirectory / constants::old_json_filename).string();
      if (QFileInfo::exists(oldFilename.c_str()))
        QFile::rename(oldFilename.c_str(), appFilename.c_str());
    }

    QFile file{appFilename.c_str()};
    if (!file.open(QIODevice::ReadOnly))
      return;

    auto const jsonRootValue = QJsonDocument::fromJson(file.readAll());
    if (jsonRootValue.isArray())
      tokenJsonList = jsonRootValue.array();
    else {
      auto const jsonObject = jsonRootValue.object();

      auto const umbral = jsonObject.value("umbral").toDouble();
      ui->umbralLine->setText(QString::number(umbral));

      auto const doubleTradeChecked =
          jsonObject.value("doubleTrade").toBool(false);
      ui->doubleTradeCheck->setChecked(doubleTradeChecked);
      m_expectedTradeCount = doubleTradeChecked ? 2 : 1;

      auto const graphThickness =
          std::clamp(jsonObject.value("graphThickness").toInt(), 1, 5);
      ui->graphThicknessCombo->setCurrentIndex(graphThickness - 1);

      m_maxOrderRetries =
          std::clamp(jsonObject.value("maxRetries").toInt(), 1, 10);
      ui->maxRetriesLine->setText(QString::number(m_maxOrderRetries));

      auto const reverse = jsonObject.value("reverse").toBool(false);
      ui->reverseCheckBox->setChecked(reverse);

      auto const liveTrade = jsonObject.value("liveTrade").toBool(false);
      ui->liveTradeCheckbox->setChecked(liveTrade);

      auto const jsonTicks = jsonObject.value("ticks").toArray();
      ui->restartTickCombo->disconnect(this);

      for (int i = 0; i < jsonTicks.size(); ++i) {
        QJsonObject const tickData = jsonTicks[i].toObject();
        auto const fieldName = tickData.value("name").toString();
        auto const specialV = tickData.value("specialV").toString();
        auto const percentageV = tickData.value("percentageV").toString();
        auto const restartV = tickData.value("restartV").toString();

        ui->resetPercentageLine->setText(percentageV);
        ui->specialLine->setText(specialV);
        ui->restartTickLine->setText(restartV);

        if (fieldName.compare("special", Qt::CaseInsensitive) == 0) {
          ui->restartTickCombo->setCurrentIndex(tick_line_type_e::special);
        } else if (fieldName.compare("refLine", Qt::CaseInsensitive) == 0) {
          ui->restartTickCombo->setCurrentIndex(tick_line_type_e::ref);
        } else {
          ui->restartTickCombo->setCurrentIndex(tick_line_type_e::normal);
        }
        onApplyButtonClicked();
      }

      tokenJsonList = jsonObject.value("tokens").toArray();
    }
  }

  connectRestartTickSignal();

  if (tokenJsonList.isEmpty())
    return;

  auto const refPreValue = ui->refCheckBox->isChecked();
  for (int i = 0; i < tokenJsonList.size(); ++i) {
    QJsonObject const obj = tokenJsonList[i].toObject();
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

void MainDialog::readTradesConfigFromFile() {
  QByteArray fileContent;

  {
    auto const filename =
        (m_configDirectory / korrelator::constants::trade_json_filename)
            .string();
    QFile file(filename.c_str());
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
  auto const objectKeys = jsonObject.keys();

  for (int i = 0; i < objectKeys.size(); ++i) {
    auto const &key = objectKeys[i];
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
      auto marketType = object.value("marketType").toString();
      if (marketType.isEmpty())
        marketType = "market";
      data.marketType = korrelator::stringToMarketType(marketType);
      if (data.marketType == korrelator::market_type_e::unknown)
        continue;
      data.size = object.value("size").toDouble();
      data.baseBalance = 0.0;

      // original JSON field used a wrong name for the field, the USDT
      // in BTCUSDT was thought to be the base amount, instead of
      // 'quote amount' that it is.
      data.originalQuoteAmount = data.quoteAmount =
          object.value("baseAmount").toDouble();

      if (data.marketType == korrelator::market_type_e::market) {
        if (data.quoteAmount == 0.0) {
          QMessageBox::critical(this, "Error",
                                tr("You need to specify the size or the balance"
                                   " for %1/%2/%3")
                                    .arg(key, data.symbol, side));
          continue;
        }
      }
      data.tradeID = object.value("id").toInt();
      data.friendForID = object.value("friendID").toInt();

      if (data.tradeType == trade_type_e::futures)
        data.leverage = object.value("leverage").toInt();
      m_tradeConfigDataList.push_back(std::move(data));
    }
  }

  // add tradeID to all trades without prior ID
  for (auto& tradeData: m_tradeConfigDataList) {
    if (tradeData.tradeID == 0) {
      auto const iter = std::max_element(m_tradeConfigDataList.cbegin(),
                                         m_tradeConfigDataList.cend(),
                                         [](auto const &data1, auto const &data2)
      {
          return data1.tradeID < data2.tradeID;
      });
      tradeData.tradeID = iter->tradeID + 1;
    }
  }

  // validate the friends side of things
  for (auto const &tradeData: m_tradeConfigDataList) {
    if (tradeData.friendForID == 0)
      continue;

    auto findIiter = std::find_if(m_tradeConfigDataList.cbegin(),
                                  m_tradeConfigDataList.cend(),
                                  [id = tradeData.friendForID](auto const &tradeData)
    {
      return id == tradeData.tradeID;
    });
    if (findIiter == m_tradeConfigDataList.cend()) { // the friend specified is not found
      QMessageBox::critical(
            this, tr("Error"),
            tr("The friend specified for tradeID %1 is not found")
            .arg(tradeData.tradeID));
      return m_tradeConfigDataList.clear();
    }
    auto const &friendTradeData = *findIiter;
    // a friend should have an opposite trade type
    // i.e spot trade should be friends with futures trade and vice versa
    if (friendTradeData.tradeType == tradeData.tradeType) {
      QMessageBox::critical(
            this, tr("Error"),
            tr("In trade with iD: %1, a %2 trade should only be friends with %3 trade")
            .arg(tradeData.tradeID)
            .arg((tradeData.tradeType == trade_type_e::spot ? "spot": "futures"))
            // display the opposite
            .arg((tradeData.tradeType != trade_type_e::spot ? "spot": "futures")));;
      return m_tradeConfigDataList.clear();
    }

    if (friendTradeData.side == tradeData.side) {
      QMessageBox::critical(
            this, tr("Error"),
            tr("In trade with id %1, the sides (BUY/SELL) for the trade"
               " should be opposites BUY->SELL, SELL->BUY")
            .arg(tradeData.tradeID));
      return m_tradeConfigDataList.clear();
    }
  }

  std::sort(m_tradeConfigDataList.begin(), m_tradeConfigDataList.end(),
            [](auto const &a, auto const &b) {
              return std::tuple(a.exchange, a.symbol.toLower()) <
                     std::tuple(b.exchange, b.symbol.toLower());
            });

  updateTradeConfigurationPrecisions();
  updateKuCoinTradeConfiguration();
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
  static size_t numberOfRecursions;
  numberOfRecursions = 0;

  auto normalizePrice = [this](korrelator::token_list_t &list,
                               korrelator::token_list_t &result,
                               trade_type_e const tt) {
    for (auto &value : list) {
      if (value.tradeType != tt || value.symbolName.length() == 1)
        continue;
      auto iter =
          find(result, value.symbolName, value.tradeType, value.exchange);
      if (iter != result.end()) {
        value.realPrice = iter->realPrice;
        value.baseCurrency = iter->baseCurrency;
        value.quoteCurrency = iter->quoteCurrency;
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
      // update kucoin's trade config && start the websockets.
      updateKuCoinTradeConfiguration();
      updateTradeConfigurationPrecisions();
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

std::optional<double> MainDialog::getIntegralValue(QLineEdit *lineEdit) {
  auto const resetTickerStr = lineEdit->text().trimmed();
  if (!resetTickerStr.isEmpty()) {
    bool isOK = true;
    double const restartNumber = resetTickerStr.toDouble(&isOK);
    if (restartNumber < 0.0 || !isOK) {
      QMessageBox::critical(
          this, "Error",
          "Something is wrong with the input value, please check");
      lineEdit->setFocus();
      return std::nullopt;
    }
    return restartNumber;
  }
  return 0;
}

bool MainDialog::validateUserInput() {
  if (auto const maxRetries = getIntegralValue(ui->maxRetriesLine);
      maxRetries.has_value()) {
    if (*maxRetries == 0.0)
      m_maxOrderRetries = 10;
    else
      m_maxOrderRetries = *maxRetries;
  } else
    return false;

  m_doingAutoLDClosure = false;
  if (m_restartTickValues.special.has_value()) {
    m_doingManualLDClosure =
        m_restartTickValues.special->restartOnTickEntry != 0.0;
    m_doingAutoLDClosure =
        m_restartTickValues.special->percentageEntry != 0.0 &&
        m_restartTickValues.special->specialEntry != 0.0;
  }

  if (auto const threshold = getIntegralValue(ui->umbralLine);
      threshold.has_value()) {
    m_threshold = *threshold;
    m_findingUmbral = *threshold != 0.0;

    if (m_findingUmbral)
      m_threshold /= 100.0;
  } else
    return false;

  if (m_tokens.size() > 2) {
    QMessageBox::critical(this, tr("Error"), "You can only trade one token");
    return false;
  }

  if (m_refs.empty()) {
    QMessageBox::critical(this, tr("Error"),
                          tr("There must be at least one ref"));
    return false;
  }

  return true;
}

void MainDialog::setupOrderTableModel() {
  m_model = std::make_unique<korrelator::order_model>();
  ui->tableView->setModel(m_model.get());
  auto header = ui->tableView->horizontalHeader();
  header->setSectionResizeMode(QHeaderView::Stretch);
  ui->tableView->setSelectionBehavior(QAbstractItemView::SelectItems);
  header->setMaximumSectionSize(200);
  ui->tableView->setWordWrap(true);
  ui->tableView->resizeRowsToContents();
}

void MainDialog::onStartVerificationSuccessful() {
  m_programIsRunning = true;
  m_lastTradeAction = korrelator::trade_action_e::nothing;
  m_maxVisiblePlot = getMaxPlotsInVisibleRegion();

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

void MainDialog::onOKButtonClicked() {
  if (ui->tokenListWidget->count() == 0)
    return;

  if (m_programIsRunning)
    return stopGraphPlotting();

  if (!validateUserInput())
    return;

  if (!m_firstRun)
    return takeBackToFactoryReset();

  onStartVerificationSuccessful();
}

void MainDialog::calculatePriceNormalization() {
  for (auto &token : m_refs)
    korrelator::updateTokenIter(token);

  if (m_hasReferences && m_tokens.size() == 2)
    return korrelator::updateTokenIter(m_tokens[1]);

  size_t const tokenStartIndex = m_hasReferences ? 1 : 0;
  for (size_t i = tokenStartIndex; i < m_tokens.size(); ++i)
    korrelator::updateTokenIter(m_tokens[i]);
}

void MainDialog::updateKuCoinTradeConfiguration() {
  using korrelator::trade_type_e;
  {
    for (size_t x = 0; x < m_tradeConfigDataList.size(); ++x) {
      auto &configuration = m_tradeConfigDataList[x];
      if (configuration.exchange != exchange_name_e::kucoin)
        continue;
      auto &kucoinContainer =
          configuration.tradeType == trade_type_e::spot
              ? m_watchables[(int)exchange_name_e::kucoin].spots
              : m_watchables[(int)exchange_name_e::kucoin].futures;
      auto iter =
          std::find_if(kucoinContainer.cbegin(), kucoinContainer.cend(),
                       [configuration](korrelator::token_t const &t) {
                         return configuration.tradeType == t.tradeType &&
                                t.symbolName.compare(configuration.symbol,
                                                     Qt::CaseInsensitive) == 0;
                       });
      if (iter != kucoinContainer.cend()) {
        configuration.multiplier = iter->multiplier;
        configuration.tickSize = iter->tickSize;
        configuration.quoteMinSize = iter->quoteMinSize;
        configuration.baseMinSize = iter->baseMinSize;
        configuration.baseAssetPrecision = iter->baseAssetPrecision;
        configuration.quotePrecision = iter->quotePrecision;
        configuration.baseCurrency = iter->baseCurrency;
        configuration.quoteCurrency = iter->quoteCurrency;
      }
    }
  }
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
      if (tokenInfo.symbolName.length() != 1)
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
      m_elapsedTime.restart();
      QObject::connect(&m_timerPlot, &QTimer::timeout,
                       m_graphUpdater.worker.get(), [this] { onTimerTick(); });
      m_lastGraphPoint = 0.0;
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

  if (!m_doingManualLDClosure) {
    refResult.isResettingRef =
        m_restartTickValues.refLines &&
        value.graphPointsDrawnCount >=
            (qint64)m_restartTickValues.refLines->restartOnTickEntry;
  } else {
    auto const &specialTickValue = m_restartTickValues.special;
    refResult.eachTickNormalize =
        specialTickValue.has_value() &&
        value.graphPointsDrawnCount >= specialTickValue->restartOnTickEntry;
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

  value.prevNormalizedPrice = value.normalizedPrice;
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
      (key >= m_maxVisiblePlot ? (key - m_maxVisiblePlot) : 0.0);
  auto &refSymbol = m_tokens[0];
  double const prevRef =
      (m_hasReferences) ? refSymbol.normalizedPrice : CMAX_DOUBLE_VALUE;

  // update ref symbol data on the graph
  auto refResult = (!m_hasReferences)
                       ? korrelator::ref_calculation_data_t()
                       : updateRefGraph(keyStart, key, updatingMinMax);
  if (!m_hasReferences)
    return;

  double currentRef = refSymbol.normalizedPrice;
  bool isResettingSymbols = false;

  // update the real symbols
  auto &value = m_tokens[1];
  ++value.graphPointsDrawnCount;
  if (value.prevNormalizedPrice == CMAX_DOUBLE_VALUE)
    value.prevNormalizedPrice = value.normalizedPrice;

  isResettingSymbols =
      !m_doingManualLDClosure && m_restartTickValues.normalLines.has_value() &&
      (value.graphPointsDrawnCount >=
       (qint64)m_restartTickValues.normalLines->restartOnTickEntry);

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

  if (refResult.eachTickNormalize || m_doingAutoLDClosure) {
    currentRef /= refSymbol.alpha;
    auto const distanceFromRefToSymbol = // a
        ((value.normalizedPrice > currentRef)
             ? (value.normalizedPrice / currentRef)
             : (currentRef / value.normalizedPrice)) -
        1.0;
    auto const distanceThreshold =
        m_restartTickValues.special->afterDivisionSpecialEntry; // b
    bool const resettingRef =
        m_doingAutoLDClosure &&
        distanceFromRefToSymbol >
            m_restartTickValues.special->afterDivisionPercentageEntry;

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
    calculateGraphMinMax(value, range, refResult.minValue, refResult.maxValue);
  }

  value.prevNormalizedPrice = value.normalizedPrice;
  value.graph->setName(QString(legendDisplayFormat)
                           .arg(value.legendName)
                           .arg(value.graphPointsDrawnCount));
  value.graph->addData(key, value.normalizedPrice);

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
  double const key = m_elapsedTime.elapsed() / 1'000.0;

  // update the min max on the y-axis every second
  bool const updatingMinMax = (key - m_lastGraphPoint) >= 1.0;
  if (updatingMinMax)
    m_lastGraphPoint = key;

  calculatePriceNormalization();
  updateGraphData(key, updatingMinMax);

  m_graphKeys.append(key);
}

void MainDialog::updatePlottingKey(
    korrelator::waitable_container_t<double> &graphKeys,
    QCustomPlot *customPlot, double &maxVisiblePlot) {
  while (true) {
    auto const key = graphKeys.get();
    // make `key` axis range scroll right with the data at a
    // constant range of `maxVisiblePlot`, set by the user
    customPlot->xAxis->setRange(key, maxVisiblePlot, Qt::AlignRight);
    customPlot->replot(QCustomPlot::RefreshPriority::rpQueuedReplot);
  }
}

void MainDialog::resetTickerData(const bool resetRefs,
                                 const bool resetSymbols) {
  static auto resetMap = [](auto &map) {
    for (auto &value : map)
      value.calculatingNewMinMax = true;
  };

  if (resetRefs)
    resetMap(m_refs);
  if (resetSymbols)
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
  auto dialog =
      new SettingsDialog{m_configDirectory.string(), this->windowTitle(), this};
  QObject::connect(dialog, &SettingsDialog::finished, this,
                   [this, dialog] { m_apiTradeApiMap = dialog->apiDataMap(); });
  QObject::connect(dialog, &SettingsDialog::finished, dialog,
                   &SettingsDialog::deleteLater);
  dialog->open();
}

korrelator::plug_data_t createPlugData(
  korrelator::trade_config_data_t *tradeConfigPtr,
  korrelator::api_data_t const &apiInfo,
  double const openPrice) {

  korrelator::plug_data_t data;
  data.exchange = tradeConfigPtr->exchange;
  data.tradeConfig = tradeConfigPtr;
  data.apiInfo = apiInfo;
  data.tradeType = tradeConfigPtr->tradeType;
  data.currentTime = std::time(nullptr);
  data.tokenPrice = openPrice;
  data.multiplier = tradeConfigPtr->multiplier;
  data.tickSize = tradeConfigPtr->tickSize;
  return data;
}

korrelator::trade_config_data_t* MainDialog::getTradeInfo(
    exchange_name_e const exchange, trade_type_e const tradeType,
    korrelator::trade_action_e const action,
    QString const &symbol)
{
  if (!m_tradeConfigDataList.empty() &&
      m_tradeConfigDataList[0].pricePrecision == -1)
    updateTradeConfigurationPrecisions();

  auto configIterPair = std::equal_range(
      m_tradeConfigDataList.begin(), m_tradeConfigDataList.end(), exchange,
      [symbol](auto const &a, auto const &b) {
        using a_type = std::decay_t<std::remove_cv_t<decltype(a)>>;
        if constexpr (std::is_same_v<exchange_name_e, a_type>)
          return std::tie(a, symbol) <
                 std::tuple(b.exchange, b.symbol.toLower());
        else
          return std::tuple(a.exchange, a.symbol.toLower()) <
                 std::tie(b, symbol);
      });

  auto modelDataPtr = m_model->front();
  if (configIterPair.first == configIterPair.second) {
    modelDataPtr->remark = "Token pair or exchange not found";
    return nullptr;
  }

  std::vector<korrelator::trade_config_data_t*> tradeConfigPtrs;
  for (auto iter = configIterPair.first; iter != configIterPair.second; ++iter) {
    if (tradeType == iter->tradeType)
      tradeConfigPtrs.push_back(&(*iter));
  }

  if (tradeConfigPtrs.empty()) {
    modelDataPtr->remark = "Cannot find tradeType of this account";
    return nullptr;
  }

  if (tradeConfigPtrs.size() == 1) {
    auto oppositeConfig = *tradeConfigPtrs[0];
    if (oppositeConfig.side == korrelator::trade_action_e::buy)
      oppositeConfig.side = korrelator::trade_action_e::sell;
    else
      oppositeConfig.side = korrelator::trade_action_e::buy;
    oppositeConfig.oppositeSide = nullptr;
    m_tradeConfigDataList.push_back(oppositeConfig);
    tradeConfigPtrs.push_back(&m_tradeConfigDataList.back());
  }

  if (tradeConfigPtrs.size() > 2) {
    modelDataPtr->remark = "You have configurations for " +
                           tradeConfigPtrs[0]->symbol +
                           " that exceeds BUY and SELL."
                           " Please check for duplicates.";
    return nullptr;
  }

  if (!tradeConfigPtrs[0]->oppositeSide || !tradeConfigPtrs[1]->oppositeSide) {
    tradeConfigPtrs[0]->oppositeSide = tradeConfigPtrs[1];
    tradeConfigPtrs[1]->oppositeSide = tradeConfigPtrs[0];
  }

  korrelator::trade_config_data_t *tradeConfigPtr = nullptr;
  for (auto &tradeConfig : tradeConfigPtrs) {
    if (action == tradeConfig->side) {
      tradeConfigPtr = tradeConfig;
      break;
    }
  }

  if (!tradeConfigPtr)
    modelDataPtr->remark = "Trade configuration was not found for this token/side";
  return tradeConfigPtr;
}

bool apiKeysAvailable(korrelator::plug_data_t const &data,
                      korrelator::api_data_t const &apiInfo)
{
  bool const isFutures = data.tradeType == trade_type_e::futures;
  return ((!isFutures && !apiInfo.spotApiKey.isEmpty()) ||
      (isFutures && !apiInfo.futuresApiKey.isEmpty())) &&
      korrelator::hasValidExchange(data.exchange);
}

bool MainDialog::onSingleTradeInfoGenerated(
    korrelator::trade_config_data_t *tradeConfigPtr,
    korrelator::api_data_t const &apiInfo,
    double const openPrice) {

  auto data = createPlugData(tradeConfigPtr, apiInfo, openPrice);
  auto const isTradable = apiKeysAvailable(data, apiInfo);
  if (isTradable)
    m_tokenPlugs.append(std::move(data));

  return isTradable;
}

void MainDialog::onDoubleTradeInfoGenerated(
    korrelator::trade_config_data_t *firstTradeConfigPtr,
    korrelator::api_data_t const &apiInfo,
    double const openPrice)
{
  auto iter =
      std::find_if(m_tradeConfigDataList.cbegin(), m_tradeConfigDataList.cend(),
                   [id = firstTradeConfigPtr->friendForID](auto const & tradeData)
  {
    return tradeData.tradeID == id;
  });

  auto& modelDataPtr = *m_model->front();
  modelDataPtr.friendModel = new korrelator::model_data_t(modelDataPtr);
  auto& secondTradeModelData = *modelDataPtr.friendModel;
  auto& remark = secondTradeModelData.remark;

  if (iter == m_tradeConfigDataList.cend()) {
    remark = "Unable to find second pair to trade with";
    return;
  }

  auto secondTradeConfig =
      getTradeInfo(iter->exchange, iter->tradeType, iter->side, iter->symbol.toLower());
  if (!secondTradeConfig)
    return;

  auto data1 = createPlugData(firstTradeConfigPtr, apiInfo, openPrice);
  auto data2 = createPlugData(secondTradeConfig, apiInfo, openPrice);
  data1.correlatorID = data2.correlatorID = m_model->front()->userOrderID;

  bool const isTradable = apiKeysAvailable(data1, apiInfo) &&
      apiKeysAvailable(data2, apiInfo);

  if (!isTradable) {
    remark = "One of the API keys for the trade is unavailable";
    modelDataPtr.remark = remark;
    return;
  }

  secondTradeModelData.side = korrelator::actionTypeToString(data2.tradeConfig->side);
  secondTradeModelData.symbol = data2.tradeConfig->symbol;
  secondTradeModelData.marketType =
      (data2.tradeConfig->tradeType == trade_type_e::spot ? "SPOT" : "FUTURES");

  m_tokenPlugs.append(std::move(data1));
  m_tokenPlugs.append(std::move(data2));
}

void MainDialog::sendExchangeRequest(
    korrelator::model_data_t &modelData,
    exchange_name_e const exchange,
    trade_type_e const tradeType,
    korrelator::trade_action_e const action,
    double const openPrice)
{
  auto iter = m_apiTradeApiMap.find(exchange);
  if (iter == m_apiTradeApiMap.end())
    return;

  auto tradeConfigPtr =
    getTradeInfo(exchange, tradeType, action, modelData.symbol.toLower());
  if (!tradeConfigPtr)
    return;

  auto modelDataPtr = m_model->front();
  if (m_expectedTradeCount == 1) {
    if (!onSingleTradeInfoGenerated(tradeConfigPtr, *iter, openPrice)) {
      modelDataPtr->remark = "Error: please check that the API keys are correctly set";
    }
    return;
  }

  onDoubleTradeInfoGenerated(tradeConfigPtr, *iter, openPrice);
}

void MainDialog::tradeExchangeTokens(
    std::function<void()> refreshModelCallback,
    korrelator::waitable_container_t<korrelator::plug_data_t> &tokenPlugs,
    std::unique_ptr<korrelator::order_model> &model, int &maxRetries,
    int &expectedTradeCount)
{
  korrelator::single_trader_t singleTrade(refreshModelCallback, model,
                                          maxRetries);
  korrelator::double_trader_t doubleTrade(refreshModelCallback, model,
                                          maxRetries);

  korrelator::plug_data_t firstMetadata;
  korrelator::plug_data_t secondMetadata;

  while (true) {
    firstMetadata = tokenPlugs.get();
    if (firstMetadata.tradeType == trade_type_e::unknown)
      tokenPlugs.clear();

    if (1 == expectedTradeCount) {
      singleTrade(std::move(firstMetadata));
    } else if (2 == expectedTradeCount) {
      secondMetadata = tokenPlugs.get();
      doubleTrade(std::move(firstMetadata), std::move(secondMetadata));
    }
  }
}
