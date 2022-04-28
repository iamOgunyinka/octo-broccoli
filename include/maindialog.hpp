#pragma once

#include <QDialog>


QT_BEGIN_NAMESPACE
namespace Ui { class MainDialog; }
QT_END_NAMESPACE

namespace brocolli {

}

class MainDialog : public QDialog
{
  Q_OBJECT

public:
  MainDialog(QWidget *parent = nullptr);
  ~MainDialog();


private:
  Ui::MainDialog *ui;
};
