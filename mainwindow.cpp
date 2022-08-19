#include "mainwindow.hpp"
#include "ui_mainwindow.h"

#include <QMdiArea>
#include <QAction>
#include <QStyle>
#include <QToolBar>

#include "maindialog.hpp"

MainWindow::MainWindow(QWidget *parent) :
  QMainWindow(parent),
  ui(new Ui::MainWindow),
  m_workSpace(nullptr)
{
  ui->setupUi(this);

  m_workSpace = std::make_unique<QMdiArea>(this);

  setCentralWidget(m_workSpace.get());
  setWindowIcon(qApp->style()->standardPixmap(QStyle::SP_DesktopIcon));
  createActions();
  createMenus();
  createToolbar();
}

MainWindow::~MainWindow()
{
  delete m_exitAction;
  delete m_preferenceAction;
  delete m_reloadTradeAction;
  delete m_aboutAction;
  delete m_newDialogAction;

  m_workSpace.reset();
  delete ui;
}

void MainWindow::createActions() {
  m_exitAction = new QAction("&Exit");
  m_exitAction->setIcon(QIcon(":/image/resources/images/exit.png"));
  m_exitAction->setShortcut(QKeySequence("Ctrl+Q"));
  QObject::connect(m_exitAction, &QAction::triggered, this,
                   &MainWindow::onExitActionTriggered);

  m_newDialogAction = new QAction("&New");
  m_newDialogAction->setIcon(QIcon(":/image/resources/images/new.png"));
  m_newDialogAction->setShortcut(QKeySequence("Ctrl+N"));
  QObject::connect(m_newDialogAction, &QAction::triggered, this,
                   &MainWindow::onNewDialogTriggered);

  m_preferenceAction = new QAction("&Preference");
  m_preferenceAction->setIcon(QIcon(":/image/resources/images/settings.png"));
  m_preferenceAction->setShortcut(QKeySequence("Ctrl+E"));
  QObject::connect(m_preferenceAction, &QAction::triggered, this,
                   &MainWindow::onPreferenceTriggered);

}

void MainWindow::onPreferenceTriggered() {

}

void MainWindow::onExitActionTriggered() {
  this->close();
}

void MainWindow::createMenus() {
  QMenu* fileMenu = menuBar()->addMenu("&File");
  fileMenu->addAction(m_newDialogAction);
  fileMenu->addAction(m_exitAction);

  auto editMenu = menuBar()->addMenu("&Edit");
  editMenu->addAction(m_preferenceAction);
}

void MainWindow::createToolbar() {
  auto toolbar = addToolBar("Toolbar");
  toolbar->addAction(m_newDialogAction);
  toolbar->addSeparator();
  toolbar->addAction(m_preferenceAction);
  toolbar->addSeparator();
  toolbar->addAction(m_exitAction);
}

void MainWindow::onNewDialogTriggered() {
  auto dialog = new MainDialog(m_workSpace.get());
  QObject::connect(dialog, &MainDialog::finished, dialog, &MainDialog::deleteLater);
  dialog->show();
}
