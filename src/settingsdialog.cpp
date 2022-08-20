#include "settingsdialog.hpp"
#include "ui_settingsdialog.h"

#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <filesystem>

#include <sodium.h>
#include "constants.hpp"

SettingsDialog::SettingsDialog(std::string const &path, QString const &title,
                               QWidget *parent)
  : QDialog(parent)
  , ui(new Ui::SettingsDialog),
    m_directory(path)
{
  ui->setupUi(this);
  ui->encryptCheckbox->setChecked(false);

  Q_ASSERT(QDir().mkpath("config"));

  QObject::connect(ui->exchangeCombo, &QComboBox::currentTextChanged,
                   this, [this](QString const & exchangeName)
  {
    if (m_apiInfo.isEmpty() || exchangeName.isEmpty())
      return;
    auto const exchange = korrelator::stringToExchangeName(exchangeName);
    if (exchange == korrelator::exchange_name_e::none)
      return;

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

  if (!title.isEmpty())
    setWindowTitle(tr("Settings for ") + title);

  readConfigurationFile();
  ui->spotPassphraseLine->setFocus();
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

    if (data.futuresApiKey.isEmpty() && !data.spotApiKey.isEmpty())
      data.futuresApiKey = data.spotApiKey;

    if (data.futuresApiSecret.isEmpty() && !data.spotApiSecret.isEmpty())
      data.futuresApiSecret = data.spotApiSecret;

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
  randombytes_buf(nonce, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);

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

  using korrelator::constants;

  auto const rootPath = std::filesystem::path(m_directory);
  auto const filename = (rootPath / constants::encrypted_config_filename).string();

  QFile file{filename.c_str()};
  if (file.exists())
    file.remove();

  {
    auto const configFilename = (rootPath / constants::config_json_filename).string();
    if (QFile f(configFilename.c_str()); f.exists())
      f.remove();
  }

  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::critical(this, "Error", "Unable to save file to file");
    return;
  }

  t.resize(messageSize);
  file.write(t.data(), messageSize);
  file.close();
  QMessageBox::information(this, "Saved", "Changes saved successfully");
}

void SettingsDialog::writeUnencryptedFile(QByteArray const & payload) {
  using korrelator::constants;

  auto const rootPath = std::filesystem::path(m_directory);
  auto const filename = (rootPath / constants::config_json_filename).string();

  QFile file{filename.c_str()};
  if (file.exists())
    file.remove();

  {
    auto const configFilename =
        (rootPath / constants::encrypted_config_filename).string();
    if (QFile f(configFilename.c_str()); f.exists())
      f.remove();
  }

  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::critical(this, "Error", "Unable to save file to file");
    return;
  }

  file.write(payload);
  QMessageBox::information(this, "Saved", "Changes saved successfully");
}

void SettingsDialog::readConfigurationFile() {
  using korrelator::constants;

  auto const rootPath = std::filesystem::path(m_directory);

  {
    auto const filename = (rootPath / constants::config_json_filename).string();
    // priorities on unencrypted file, read this first
    QFile unencyrptedFile(filename.c_str());
    if (unencyrptedFile.exists() && unencyrptedFile.open(QIODevice::ReadOnly))
      return readUnencryptedData(unencyrptedFile.readAll());
  }

  auto const filename = (rootPath / constants::encrypted_config_filename).string();
  QFile encryptedFile(filename.c_str());
  if (encryptedFile.exists() && encryptedFile.open(QIODevice::ReadOnly))
    return readEncryptedFile(encryptedFile);
}

void SettingsDialog::readEncryptedFile(QFile& file) {
  if (m_key.empty()) {
    auto const decryptionKey = QInputDialog::getText(
          this, "Decryption key", "Please input the decryption key",
          QLineEdit::Password).toStdString();
    if (decryptionKey.empty())
      return;
    m_key = decryptionKey;
  }

  std::string fileDecrypted(4'096, '0');
  unsigned long long fileDecryptedLength;
  unsigned char *mFileDecrypted = reinterpret_cast<unsigned char *>(fileDecrypted.data());
  unsigned long long fileContentLength = (unsigned long long) // fileContent.length();
  file.size();
  auto const fileContent = file.readAll().toStdString();
  file.close();
  unsigned char const *rawFileContent =
      reinterpret_cast<unsigned char const *>(fileContent.c_str());
  unsigned char* nsec = nullptr;
  unsigned char const * ad = nullptr;
  unsigned long long adLength = 0;
  unsigned char nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES]{};
  randombytes_buf(nonce, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
  unsigned char const * secretKey = reinterpret_cast<unsigned char const *>(m_key.data());

  if (crypto_aead_xchacha20poly1305_ietf_decrypt(
        mFileDecrypted, &fileDecryptedLength, nsec,
        rawFileContent, fileContentLength, ad, adLength, nonce, secretKey) != 0)
  {
    QMessageBox::critical(this, "Error", "unable to decrypt the encrypted text");
    return;
  }
  fileDecrypted.resize(fileDecryptedLength);

  auto const byteContent = QByteArray::fromStdString(fileDecrypted);
  readUnencryptedData(byteContent);
}

void SettingsDialog::readUnencryptedData(QByteArray const & fileContent) {
  auto const rootDataList = QJsonDocument::fromJson(fileContent).array();

  m_apiInfo.clear();
  for (int i = 0; i < rootDataList.size(); ++i) {
    auto const dataObject = rootDataList[i].toObject();
    auto const exchange = korrelator::stringToExchangeName(
          dataObject.value("name").toString());
    if (exchange == korrelator::exchange_name_e::none)
      continue;

    korrelator::api_data_t data;
    data.spotApiKey = dataObject.value("spot_api_key").toString();
    data.spotApiSecret = dataObject.value("spot_api_secret").toString();
    data.spotApiPassphrase = dataObject.value("spot_api_passphrase").toString();
    data.futuresApiKey = dataObject.value("futures_api_key").toString();
    data.futuresApiSecret = dataObject.value("futures_api_secret").toString();
    data.futuresApiPassphrase = dataObject.value("futures_api_passphrase").toString();

    if (data.futuresApiKey.isEmpty() && !data.spotApiKey.isEmpty())
      data.futuresApiKey = data.spotApiKey;

    if (data.futuresApiSecret.isEmpty() && !data.spotApiSecret.isEmpty())
      data.futuresApiSecret = data.spotApiSecret;

    m_apiInfo[exchange] = std::move(data);
  }

  ui->exchangeCombo->clear();
  auto const exchanges = m_apiInfo.uniqueKeys();
  for (int i = 0; i < exchanges.size(); ++i)
    ui->exchangeCombo->addItem(korrelator::exchangeNameToString(exchanges[i]));

  if (ui->exchangeCombo->count() != 0)
    ui->exchangeCombo->setCurrentIndex(0);
}

SettingsDialog::api_data_map_t SettingsDialog::getApiDataMap(std::string const &path) {
  SettingsDialog dialog(path, "");
  return dialog.apiDataMap();
}
