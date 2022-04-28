#include "maindialog.hpp"

#include "ui_maindialog.h"
#include "container.hpp"

namespace brocolli {

}

MainDialog::MainDialog(QWidget *parent)
  : QDialog(parent)
  , ui(new Ui::MainDialog)
{
  ui->setupUi(this);
  setWindowFlags(windowFlags() |
                 Qt::WindowMinimizeButtonHint |
                 Qt::WindowMaximizeButtonHint);
}

MainDialog::~MainDialog()
{
  delete ui;
}
