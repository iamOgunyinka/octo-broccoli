#include "crashreportdialog.hpp"
#include "ui_crashreportdialog.h"

#include <QMessageBox>
#include <QDateTime>
#include <QDebug>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFileInfo>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>

#include <filesystem>

CrashReportDialog::CrashReportDialog(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::CrashReportDialog)
{
  ui->setupUi(this);

  QObject::connect(ui->deleteFileButton, &QPushButton::clicked, this,
                   &CrashReportDialog::OnDeleteFileRequested);
  ui->progressBar->setVisible(false);
  QObject::connect(ui->sendFileButton, &QPushButton::clicked, this,
                   &CrashReportDialog::OnSendFileForCheck);
}

CrashReportDialog::~CrashReportDialog()
{
  delete ui;
}

void CrashReportDialog::setCrashFile(QString const &filename) {
  m_oldCrashFilename = filename;

  struct stat fileStat;
  memset(&fileStat, 0, sizeof(struct stat));
  std::string const stdFilename = filename.toStdString();
  if (stat(stdFilename.c_str(), &fileStat) != 0){
    QMessageBox::critical(this, "Error", "Unable to get information on this file");
    return;
  }

  ui->nameLine->setText(filename);
  ui->descriptionLine->setText("Microsoft Windows Dump file(*.dmp)");
  ui->dateCreatedLine->setText(QDateTime::fromTime_t(fileStat.st_ctime).toString());
  ui->lastModifiedDateLine->setText(QDateTime::fromTime_t(fileStat.st_mtime).toString());

  size_t const fileSizeInMb = fileStat.st_size / (1024 * 1024);
  QString const fileSizeString = "~" + QString::number(fileSizeInMb) + "MB";
  ui->fileSizeLine->setText(fileSizeString);
}

void CrashReportDialog::OnDeleteFileRequested()
{
  auto const response =
      QMessageBox::question(this, "Delete", "Are you sure you want to delete this file?");
  if (response == QMessageBox::No)
    return;

  std::filesystem::path const p(m_oldCrashFilename.toStdString());
  if (!std::filesystem::remove(p)) {
    QMessageBox::critical(this, "Error", "Unable to delete the crash file");
    return;
  }

  QMessageBox::information(this, "Delete", "File deleted successfully.");
  this->accept();
}

void CrashReportDialog::OnSendFileForCheck()
{
  QFile* payload_file{ new QFile(m_oldCrashFilename) };
  if( !payload_file->open( QIODevice::ReadOnly ) ){
      QMessageBox::information( this, tr( "Error" ), tr( "unable to process payload" ) );
      return;
  }
  ui->sendFileButton->setEnabled(false);
  ui->deleteFileButton->setEnabled(false);
  UploadPayload(payload_file);
}

void CrashReportDialog::UploadPayload(QFile* payloadFile )
{
  QUrl url{"http://173.82.232.184/upload"};
  QByteArray const filename = QFileInfo{m_oldCrashFilename}.baseName().trimmed().toUtf8();

  QNetworkRequest request{url};
  request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader,
                            "application/octet-stream" );
  request.setRawHeader("X-Version-Num", "316" );
  request.setRawHeader( "filename", filename );

  ui->progressBar->setVisible(true);
  ui->progressBar->setValue(0);
  QNetworkReply *reply = m_networkAccessManager.post(request, payloadFile);
  QObject::connect( reply, &QNetworkReply::finished, this, [=]{
    if(payloadFile){
      if(payloadFile->exists())
        payloadFile->remove();
      delete payloadFile;
    }
    QJsonObject response_object = QJsonDocument::fromJson(reply->readAll()).object();
    if(reply->error() != QNetworkReply::NoError){
      QMessageBox::critical(this, tr("Error"), response_object.isEmpty() ?
                            reply->errorString() : response_object["message"].toString());
      ui->sendFileButton->setEnabled(true);
      return ui->deleteFileButton->setEnabled(true);
    }
    onFileUploadCompleted(reply);
  });

  QObject::connect(reply, &QNetworkReply::uploadProgress, this,
                   [this, payloadFile]( qint64 sent, qint64 total ){
    ui->progressBar->setRange(0, total);
    ui->progressBar->setValue(sent);
    if(total < 0 || sent == total) {
      if(payloadFile && payloadFile->exists())
        payloadFile->remove();
      ui->progressBar->setVisible(false);
    }
  });
}

void CrashReportDialog::onFileUploadCompleted(QNetworkReply* response)
{
  QByteArray const response_byte = response->readAll();
  QJsonDocument const json_doc = QJsonDocument::fromJson( response_byte );
  QJsonObject const object_root = json_doc.object();

  if(response->error() != QNetworkReply::NoError) {
    qDebug() << response_byte;
    if(!object_root.isEmpty())
      QMessageBox::critical(this, tr("Error"), object_root["message"].toString());
    else
      QMessageBox::critical(this, tr( "Error" ), response->errorString());
    return ui->deleteFileButton->setEnabled(true);
  }

  QMessageBox::information( this, tr("Successful"), tr("Data upload successful"));
  this->accept();
}
