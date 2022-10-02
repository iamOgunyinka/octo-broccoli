#ifndef CRASHREPORTDIALOG_HPP
#define CRASHREPORTDIALOG_HPP

#include <QDialog>

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
  void OnDeleteFileRequested(bool const confirmDelete);
  void OnSendFileForCheck();

  Ui::CrashReportDialog *ui;
  QString m_oldCrashFilename;
  // QString m_newCrashFilename;
};

#endif // CRASHREPORTDIALOG_HPP
