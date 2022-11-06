#ifndef HELPDIALOG_HPP
#define HELPDIALOG_HPP

#include <QDialog>

namespace Ui {
  class HelpDialog;
}

class HelpDialog : public QDialog
{
  Q_OBJECT

public:
  explicit HelpDialog(QWidget *parent = nullptr);
  ~HelpDialog();
  void reject() override { this->accept(); }

private:
  Ui::HelpDialog *ui;
};

#endif // HELPDIALOG_HPP
