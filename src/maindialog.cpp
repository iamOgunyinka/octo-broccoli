#include "maindialog.hpp"

#include "ui_maindialog.h"
#include "container.hpp"

#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMessageBox>

static char const * const futures_tokens_url =
    "https://fapi.binance.com/fapi/v1/ticker/price";
static char const * const spots_tokens_url =
    "https://api.binance.com/api/v3/ticker/price";

namespace brocolli {

QSslConfiguration getSSLConfig() {
  auto ssl_config = QSslConfiguration::defaultConfiguration();
  ssl_config.setProtocol(QSsl::TlsV1_2OrLater);
  return ssl_config;
}

void configureRequestForSSL(QNetworkRequest& request)
{
  request.setSslConfiguration(getSSLConfig());
}

brocolli::token_list_t::iterator
findToken(brocolli::token_list_t& tokenList, QString const &tokenName) {
  return std::lower_bound(tokenList.begin(), tokenList.end(), tokenName,
                          brocolli::token_compare_t{});
}

}

MainDialog::MainDialog(QWidget *parent)
  : QDialog(parent)
  , ui(new Ui::MainDialog)
{
  ui->setupUi(this);
  setWindowIcon(qApp->style()->standardPixmap(QStyle::SP_DesktopIcon));
  setWindowFlags(windowFlags() |
                 Qt::WindowMinimizeButtonHint |
                 Qt::WindowMaximizeButtonHint);
  getSpotsTokens();
  getFuturesTokens();

  QObject::connect(ui->spotNextButton, &QToolButton::clicked, this, [this] {
    auto const tokenName = ui->spotCombo->currentText();
    auto const text = tokenName + " (SPOT)";
    for (int i = 0; i < ui->tokenListWidget->count(); ++i) {
      QListWidgetItem *item = ui->tokenListWidget->item(i);
      if (text == item->text())
        return;
    }
    ui->tokenListWidget->addItem(text);
    ui->priceListWidget->addItem("");

    auto iter = brocolli::findToken(m_spotData, tokenName.toLower());
    if (iter != m_spotData.end())
      iter->item = ui->priceListWidget->item(ui->priceListWidget->count() - 1);
    newItemAdded(tokenName.toLower(), brocolli::trade_type_e::spot);
  });

  QObject::connect(ui->futuresNextButton, &QToolButton::clicked, this, [this] {
    auto const tokenName = ui->futuresCombo->currentText();
    auto const text = tokenName + " (FUTURES)";
    for (int i = 0; i < ui->tokenListWidget->count(); ++i) {
      QListWidgetItem *item = ui->tokenListWidget->item(i);
      if (text == item->text())
        return;
    }
    ui->tokenListWidget->addItem(text);
    ui->priceListWidget->addItem("");
    auto iter = brocolli::findToken(m_futuresData, tokenName.toLower());
    if (iter != m_futuresData.end())
      iter->item = ui->priceListWidget->item(ui->priceListWidget->count() - 1);
    newItemAdded(tokenName.toLower(), brocolli::trade_type_e::futures);
  });

  QObject::connect(ui->spotPrevButton, &QToolButton::clicked, this, [this] {
    auto currentRow = ui->tokenListWidget->currentRow();
    if (currentRow < 0 || currentRow >= ui->tokenListWidget->count())
      return;
    auto item = ui->tokenListWidget->takeItem(currentRow);
    tokenRemoved(item->text());
    delete item;
    delete ui->priceListWidget->takeItem(currentRow);
  });
}

MainDialog::~MainDialog()
{
  delete ui;
}

void MainDialog::tokenRemoved(QString const &text) {
  if (text.lastIndexOf(" (SPOT)") != -1) {
    auto const tokenName = text.chopped(7).toLower();
    auto iter = brocolli::findToken(m_spotData, tokenName);

    Q_ASSERT(iter != m_spotData.end());
    if (iter != m_spotData.end()) {
      iter->item = nullptr;
      if (m_websocket)
        m_websocket->unsubscribe(tokenName, brocolli::trade_type_e::spot);
    }
  } else {
    auto const tokenName = text.chopped(10).toLower();
    auto iter = brocolli::findToken(m_futuresData, tokenName);
    if (iter != m_futuresData.end()) {
      iter->item = nullptr;
      if (m_websocket)
        m_websocket->unsubscribe(tokenName, brocolli::trade_type_e::futures);
    }
  }

  if (m_websocket && ui->tokenListWidget->count() == 0)
    m_websocket.reset();
}

void MainDialog::getSpotsTokens() {
  auto callback = [this](brocolli::token_list_t && list) {
    ui->spotCombo->clear();
    for (auto const &d: list)
      ui->spotCombo->addItem(d.tokenName.toUpper());
    m_spotData = std::move(list);
  };
  auto const url = QUrl(spots_tokens_url);
  sendNetworkRequest(url, callback);
}

void MainDialog::getFuturesTokens() {
  auto const url = QUrl(futures_tokens_url);
  auto callback = [this](brocolli::token_list_t && list) {
    ui->futuresCombo->clear();
    for (auto const &d: list)
      ui->futuresCombo->addItem(d.tokenName.toUpper());
    m_futuresData = std::move(list);
  };
  sendNetworkRequest(url, callback);
}

void MainDialog::sendNetworkRequest(QUrl const & url,
    std::function<void (brocolli::token_list_t &&)> onSuccessCallback)
{
  QNetworkRequest request(url);
  brocolli::configureRequestForSSL(request);
  m_networkManager.enableStrictTransportSecurityStore(true);

  auto reply = m_networkManager.get(request);
  QObject::connect(reply, &QNetworkReply::finished, this,
                   [this, reply, cb=std::move(onSuccessCallback)]{
    if (reply->error() != QNetworkReply::NoError){
      QMessageBox::critical(this, "Error", "Unable to get the list of "
                                           "all token pairs => " +
                            reply->errorString());
      return;
    }
    auto const responseString = reply->readAll();
    auto const jsonResponse = QJsonDocument::fromJson(responseString);
    if (jsonResponse.isEmpty()){
      QMessageBox::critical(this, "Error", "Unable to read the response sent");
      return;
    }
    auto const list = jsonResponse.array();
    brocolli::token_list_t tokenList;
    tokenList.reserve(list.size());

    for (auto const & token: list) {
      auto const tokenObject = token.toObject();
      brocolli::token_t t;
      t.tokenName = tokenObject.value("symbol").toString().toLower();
      auto const jsonPrice = tokenObject.value("price");
      if (jsonPrice.isString())
        t.price = jsonPrice.toString().toDouble();
      else
        t.price = jsonPrice.toDouble();
      t.minPrice = t.price * 0.75;
      t.maxPrice = t.price * 1.25;
      tokenList.push_back(std::move(t));
    }

    std::sort(tokenList.begin(), tokenList.end(), brocolli::token_compare_t{});
    cb(std::move(tokenList));
  });

  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}

brocolli::token_list_t::iterator
updateData(brocolli::token_list_t& dataList, QString const &tokenName,
           double const price) {
  auto iter = brocolli::findToken(dataList, tokenName);
  if (iter != dataList.end()) {
    iter->price = price;
    iter->minPrice = std::min(iter->minPrice, price);
    iter->maxPrice = std::max(iter->maxPrice, price);
  }
  return iter;
}

void MainDialog::newItemAdded(QString const &tokenName,
                              brocolli::trade_type_e const tt)
{
  if (!m_websocket) {
    m_websocket = std::make_unique<brocolli::cwebsocket>();
    QObject::connect(m_websocket.get(),
                     &brocolli::cwebsocket::newSpotPriceReceived, this,
                     [this](QString const &tokenName, double price)
    {
      auto iter = updateData(m_spotData, tokenName, price);
      if (iter != m_spotData.end() && iter->item != nullptr)
        iter->item->setText(QString::number(price, 'f'));
    });

    QObject::connect(m_websocket.get(),
                     &brocolli::cwebsocket::newFuturesPriceReceived, this,
                     [this](QString const &tokenName, double const price)
    {
      auto iter = updateData(m_futuresData, tokenName, price);
      if (iter != m_futuresData.end() && iter->item != nullptr)
        iter->item->setText(QString::number(price, 'f'));
    });
  }
  m_websocket->subscribe(tokenName, tt);
}
