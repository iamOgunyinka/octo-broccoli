#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>
#include <QVector>
#include <filesystem>

namespace Ui {
  class MainWindow;
}

class QMdiArea;
class QAction;
class MainDialog;

class MainWindow final: public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

  void closeEvent(QCloseEvent*) override;

private:
  void createActions();
  void createMenus();
  void createToolbar();
  void onNewDialogTriggered();
  void onPreferenceTriggered();
  void onReloadTradeConfigTriggered();
  void ShowCrashUI(QString const &);
  void ShowHowToWindow();
  MainDialog* getActiveDialog();

  Ui::MainWindow *ui;
  QAction* m_exitAction = nullptr;
  QAction* m_preferenceAction = nullptr;
  QAction* m_reloadTradeAction = nullptr;
  QAction* m_aboutAction = nullptr;
  QAction* m_newDialogAction = nullptr;
  QAction* m_howToAction = nullptr;

  std::unique_ptr<QMdiArea> m_workSpace;
  std::filesystem::path m_rootConfigDirectory;
  QVector<QDialog*> m_dialogs;
  bool m_warnOnClose = true;
};

QString getLocalDumpSite();

#endif // MAINWINDOW_HPP
