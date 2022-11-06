#include "helpdialog.hpp"
#include "ui_helpdialog.h"
#include <QFileDialog>

HelpDialog::HelpDialog(QWidget *parent) :
  QDialog(parent), ui(new Ui::HelpDialog)
{
  ui->setupUi(this);
  QFile file(":/info/howTo.html");
  if (!file.open(QIODevice::ReadOnly))
    return;
  auto const fileContent = file.readAll();
  file.close();

  ui->label->setWordWrap(true);
  ui->label->setText(fileContent);  
}

HelpDialog::~HelpDialog()
{
  delete ui;
}
