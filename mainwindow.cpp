#include "mainwindow.hpp"
#include "ui_mainwindow.h"

#include <QMdiArea>
#include <QAction>
#include <QStyle>
#include <QToolBar>
#include <QMessageBox>
#include <QCloseEvent>
#include <QMdiSubWindow>

#include "constants.hpp"
#include "maindialog.hpp"

MainWindow::MainWindow(QWidget *parent) :
  QMainWindow(parent),
  ui(new Ui::MainWindow),
  m_workSpace(nullptr)
{
  ui->setupUi(this);
  m_workSpace = std::make_unique<QMdiArea>(this);

  m_rootConfigDirectory = std::filesystem::path(".") / korrelator::constants::root_dir;

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
                   &MainWindow::close);

  m_newDialogAction = new QAction("&New Trade");
  m_newDialogAction->setToolTip("Open a new correlator window");
  m_newDialogAction->setIcon(QIcon(":/image/resources/images/new.png"));
  m_newDialogAction->setShortcut(QKeySequence("Ctrl+N"));
  QObject::connect(m_newDialogAction, &QAction::triggered, this,
                   &MainWindow::onNewDialogTriggered);

  m_preferenceAction = new QAction("&API Key");
  m_preferenceAction->setToolTip("Open the settings window where API Keys can be set");
  m_preferenceAction->setIcon(QIcon(":/image/resources/images/settings.png"));
  m_preferenceAction->setShortcut(QKeySequence("Ctrl+E"));
  QObject::connect(m_preferenceAction, &QAction::triggered, this,
                   &MainWindow::onPreferenceTriggered);

  m_reloadTradeAction = new QAction("&Reload trade");
  m_reloadTradeAction->setToolTip("Read the trade configuration again. Perhaps, "
                                  "it has been modified while the window is opened.");
  m_reloadTradeAction->setIcon(QIcon(":/image/resources/images/refresh.png"));
  m_reloadTradeAction->setShortcut(QKeySequence("F5"));
  QObject::connect(m_reloadTradeAction, &QAction::triggered, this,
                   &MainWindow::onReloadTradeConfigTriggered);

  m_aboutAction = new QAction("&About");
  m_aboutAction->setShortcut(QKeySequence("F1"));
  m_aboutAction->setToolTip("Show the software information used for"
                                  " developing this software");
  m_aboutAction->setIcon(QIcon(":/image/resources/images/about.png"));
  QObject::connect(m_aboutAction, &QAction::triggered, qApp,
                   &QApplication::aboutQt);
}

MainDialog* MainWindow::getActiveDialog() {
  auto subWindow = m_workSpace->currentSubWindow();
  if (!subWindow) {
    if (!m_dialogs.empty())
      QMessageBox::information(this, tr("Preference"),
                               tr("Select any window and press again"));
    return nullptr;
  }
  auto dialog = qobject_cast<MainDialog*>(subWindow->widget());
  if (!dialog) {
    QMessageBox::critical(this, tr("Error"),
                          tr("An internal error occurred"));
    return nullptr;
  }
  return dialog;
}

void MainWindow::onPreferenceTriggered() {
  if (auto dialog = getActiveDialog(); dialog)
    dialog->openPreferenceWindow();
}

void MainWindow::onReloadTradeConfigTriggered() {
  if (auto dialog = getActiveDialog(); dialog)
    dialog->reloadTradeConfig();
}

void MainWindow::closeEvent(QCloseEvent* closeEvent) {
  if (!m_dialogs.empty()) {
    auto const response = QMessageBox::question(
          this, tr("Exit"), tr("You still have %1 correlator windows open, do you "
                               "really want to close them?").arg(m_dialogs.size()));
    if (response == QMessageBox::No)
      return closeEvent->ignore();
  }

  m_warnOnClose = false;
  for (auto& dialog: m_dialogs)
    dialog->close();

  closeEvent->accept();
}

void MainWindow::createMenus() {
  QMenu* fileMenu = menuBar()->addMenu("&File");
  fileMenu->addAction(m_newDialogAction);
  fileMenu->addAction(m_exitAction);

  auto editMenu = menuBar()->addMenu("&Edit");
  editMenu->addAction(m_reloadTradeAction);
  editMenu->addAction(m_preferenceAction);

  auto helpMenu = menuBar()->addMenu("&Help");
  helpMenu->addAction(m_aboutAction);
}

void MainWindow::createToolbar() {
  auto toolbar = addToolBar("Toolbar");
  toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

  toolbar->addAction(m_newDialogAction);
  toolbar->addSeparator();
  toolbar->addAction(m_reloadTradeAction);
  toolbar->addSeparator();
  toolbar->addAction(m_preferenceAction);
  toolbar->addSeparator();
  toolbar->addAction(m_exitAction);
}

void MainWindow::onNewDialogTriggered() {
  auto const path = m_rootConfigDirectory / std::to_string(m_dialogs.size() + 1);
  auto dialog = new MainDialog(m_warnOnClose, path, this);
  m_dialogs.push_back(dialog);

  auto subWindow = m_workSpace->addSubWindow(dialog);
  subWindow->setAttribute(Qt::WA_DeleteOnClose);
  subWindow->setWindowTitle(QString::number(m_dialogs.size()));
  dialog->setWindowTitle(subWindow->windowTitle());

  QObject::connect(subWindow, &QMdiSubWindow::destroyed, this, [dialog, this] {
      m_dialogs.removeOne(dialog);
      delete dialog;
    });
  dialog->show();
}
