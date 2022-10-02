#include "crashreportdialog.hpp"
#include "ui_crashreportdialog.h"

#include <QMessageBox>
#include <QDateTime>
#include <QDebug>
#include <filesystem>

CrashReportDialog::CrashReportDialog(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::CrashReportDialog)
{
  ui->setupUi(this);

  QObject::connect(ui->deleteFileButton, &QPushButton::clicked, this,
                   [this]
  {
      OnDeleteFileRequested(true);
  });
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

void CrashReportDialog::OnDeleteFileRequested(bool const confirmFileDeletion)
{
  if (confirmFileDeletion) {
    auto const response =
        QMessageBox::question(this, "Delete", "Are you sure you want to delete this file?");
    if (response == QMessageBox::No)
      return;
  }

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
  QMessageBox::critical(this, "Send", "This has not been implemented yet,"
                                      " so we just delete the file.");
  OnDeleteFileRequested(false);
}
