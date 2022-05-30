#include "settingsdialog.hpp"
#include "ui_settingsdialog.h"

#include <QFile>
#include <QMessageBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "sodium.h"

static char const * const encrypted_config_filename = ".config.dat";
static char const * const config_json_filename = "config.json";

SettingsDialog::SettingsDialog(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::SettingsDialog)
{
  ui->setupUi(this);

  readConfigurationFile();
  QObject::connect(
        ui->exchangeCombo, &QComboBox::currentTextChanged,
        this, [this](QString const & exchangeName)
  {
    if (m_apiInfo.isEmpty())
      return;
    auto const exchange = korrelator::stringToExchangeName(exchangeName);
    auto valueIter = m_apiInfo.find(exchange);
    if (valueIter == m_apiInfo.end())
      return;
    ui->spotApiKeyLine->setText(valueIter->spotApiKey);
    ui->spotSecretLine->setText(valueIter->spotApiSecret);
    ui->spotPassphraseLine->setText(valueIter->spotApiPassphrase);
    ui->futuresApiKeyLine->setText(valueIter->futuresApiKey);
    ui->futuresSecretLine->setText(valueIter->futuresApiSecret);
    ui->futuresPassphraseLine->setText(valueIter->futuresApiPassphrase);
  });

  QObject::connect(ui->saveButton, &QPushButton::clicked, this, [this]{
    if (!preprocessData() || m_apiInfo.isEmpty())
      return;

    QJsonArray list;
    for (auto iter = m_apiInfo.constKeyValueBegin();
         iter != m_apiInfo.constKeyValueEnd(); ++iter) {
      QJsonObject object;
      auto const &value = (*iter).second;
      object["name"] = korrelator::exchangeNameToString((*iter).first);
      object["spot_api_key"] = value.spotApiKey;
      object["spot_api_passphrase"] = value.spotApiPassphrase;
      object["spot_api_secret"] = value.spotApiSecret;
      object["futures_api_passphrase"] = value.futuresApiPassphrase;
      object["futures_api_secret"] = value.futuresApiSecret;
      object["futures_api_key"] = value.futuresApiKey;

      list.append(object);
    }
    auto const payload = QJsonDocument(list).toJson();
    if (ui->encryptCheckbox->isChecked())
      return writeEncryptedFile(payload);
    writeUnencryptedFile(payload);
  });
}

SettingsDialog::~SettingsDialog()
{
  delete ui;
}

void SettingsDialog::processJsonList(QJsonArray& list) {
  if (list.isEmpty())
    return;

  for (int i = 0; i < list.size(); ++i) {
    QJsonObject const object = list[i].toObject();
    auto const exchange = korrelator::stringToExchangeName(
          object.value("name").toString());
    if (exchange == korrelator::exchange_name_e::none)
      continue;
    korrelator::api_data_t data;
    data.spotApiKey = object.value("spot_api_key").toString();
    data.spotApiSecret = object.value("spot_api_secret").toString();
    data.spotApiPassphrase = object.value("spot_api_passphrase").toString();
    data.futuresApiKey = object.value("futures_api_key").toString();
    data.futuresApiSecret = object.value("futures_api_secret").toString();
    data.futuresApiPassphrase = object.value("futures_api_passphrase").toString();

    m_apiInfo[exchange] = data;
  }

  if (m_apiInfo.empty())
    return;
  auto const keys = m_apiInfo.uniqueKeys();
  ui->exchangeCombo->clear();
  for (auto const key: keys)
    ui->exchangeCombo->addItem(korrelator::exchangeNameToString(key));
}

bool SettingsDialog::preprocessData() {
  QString exchangeName;
  if (m_apiInfo.isEmpty() || ui->checkBox->isChecked()) {
    exchangeName = QInputDialog::getText(
          this, "Exchange name", "Input the exchange name e.g. Binance")
        .trimmed();
  } else {
    exchangeName = ui->exchangeCombo->currentText();
  }

  if (exchangeName.isEmpty()) {
    QMessageBox::critical(this, "Error", "Exchange name cannot be empty");
    return false;
  }

  auto const exchange = korrelator::stringToExchangeName(exchangeName);
  if (exchange == korrelator::exchange_name_e::none) {
    QMessageBox::critical(this, "Error", "This exchange name is invalid");
    return false;
  }

  korrelator::api_data_t apiData;
  apiData.futuresApiKey = ui->futuresApiKeyLine->text();
  apiData.futuresApiPassphrase = ui->futuresPassphraseLine->text();
  apiData.futuresApiSecret = ui->futuresSecretLine->text();
  apiData.spotApiPassphrase = ui->spotPassphraseLine->text();
  apiData.spotApiSecret = ui->spotSecretLine->text();
  apiData.spotApiKey = ui->spotApiKeyLine->text();
  m_apiInfo[exchange] = apiData;

  return true;
}

void SettingsDialog::writeEncryptedFile(QByteArray const & payload) {
  if (m_key.empty()) {
    auto const decryptionKey = QInputDialog::getText(
          this, "Decryption key", "Please input the encryption key",
          QLineEdit::Password).toStdString();
    if (decryptionKey.empty())
      return;
    m_key = decryptionKey;
  }
  auto const payloadStr = payload.toStdString();
  unsigned char const *m = reinterpret_cast<unsigned char const *>(
        payloadStr.data());
  unsigned char const * encryptionKey = reinterpret_cast<unsigned char const *>(
        m_key.data());
  unsigned char nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES]{};
  for (int i = 0; i < crypto_aead_xchacha20poly1305_ietf_NPUBBYTES; ++i)
    nonce[i] = 'a' + i;

  std::string t(2048, '0');
  unsigned long long messageSize = t.size();
  unsigned char* resultData = reinterpret_cast<unsigned char*>(t.data());

  int const result = crypto_aead_xchacha20poly1305_ietf_encrypt(
        resultData, &messageSize, m, payloadStr.size(),
        nullptr, 0, nullptr, nonce, encryptionKey);
  if (result != 0) {
    QMessageBox::critical(this, "Error", "Unable to encrypt data");
    return;
  }

  QFile file{encrypted_config_filename};
  if (file.exists())
    file.remove();

  if (QFile f(config_json_filename); f.exists())
    f.remove();

  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::critical(this, "Error", "Unable to save file to file");
    return;
  }

  file.write(t.data(), messageSize);
  QMessageBox::information(this, "Saved", "Changes saved successfully");
}

void SettingsDialog::writeUnencryptedFile(QByteArray const & payload) {
  QFile file{config_json_filename};
  if (file.exists())
    file.remove();

  if (QFile f(encrypted_config_filename); f.exists())
    f.remove();

  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::critical(this, "Error", "Unable to save file to file");
    return;
  }
  file.write(payload);
  QMessageBox::information(this, "Saved", "Changes saved successfully");
}

void SettingsDialog::readConfigurationFile() {
  {
    // priorities on unencrypted file, read this first
    QFile unencyrptedFile(config_json_filename);
    if (unencyrptedFile.exists() && unencyrptedFile.open(QIODevice::ReadOnly))
      return readUnencryptedJsonFile(unencyrptedFile);
  }

  QFile encryptedFile(encrypted_config_filename);
  if (!encryptedFile.exists() || !encryptedFile.open(QIODevice::ReadOnly))
    return;
  return readEncryptedFile(encryptedFile);
}

void SettingsDialog::readUnencryptedJsonFile(QFile& file) {

}

void SettingsDialog::readEncryptedFile(QFile& file) {

}

SettingsDialog::api_data_map_t SettingsDialog::getApiDataMap() {
  SettingsDialog dialog;
  return dialog.apiDataMap();
}
