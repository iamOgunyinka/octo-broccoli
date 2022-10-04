#ifndef CRASHREPORTDIALOG_HPP
#define CRASHREPORTDIALOG_HPP

#include <QDialog>
#include <QFile>
#include <QNetworkAccessManager>

namespace Ui {
  class CrashReportDialog;
}

class CrashReportDialog : public QDialog
{
  Q_OBJECT

public:
  explicit CrashReportDialog(QWidget *parent = nullptr);
  ~CrashReportDialog();

  void setCrashFile(QString const &filename);

private:
  void OnDeleteFileRequested();
  void OnSendFileForCheck();
  void UploadPayload(QFile* payloadFile);
  void onFileUploadCompleted(QNetworkReply*);

  Ui::CrashReportDialog *ui;
  QString m_oldCrashFilename;
  QNetworkAccessManager m_networkAccessManager;
};

#endif // CRASHREPORTDIALOG_HPP
