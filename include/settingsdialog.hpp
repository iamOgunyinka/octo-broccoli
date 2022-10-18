#pragma once

#include <QDialog>
#include <QMap>
#include "utils.hpp"

namespace Ui {
class SettingsDialog;
}

class QFile;
class QJsonArray;

class SettingsDialog : public QDialog
{
  Q_OBJECT

public:
  using api_data_map_t = QMap<korrelator::exchange_name_e, korrelator::api_data_t>;
  explicit SettingsDialog(std::string const &directory,
                          QString const &title,
                          QWidget *parent = nullptr);
  ~SettingsDialog();

  static api_data_map_t getApiDataMap(std::string const &directory);
  auto& apiDataMap() { return m_apiInfo; }

private:
  void readConfigurationFile();
  void readEncryptedFile(QFile&);
  void readUnencryptedData(QByteArray const &);
  void processJsonList(QJsonArray&);
  void writeEncryptedFile(QByteArray const &);
  void writeUnencryptedFile(QByteArray const &);
  bool preprocessData();

private:
  Ui::SettingsDialog *ui;
  api_data_map_t m_apiInfo;
  std::string m_key;
  std::string const m_directory;
};

