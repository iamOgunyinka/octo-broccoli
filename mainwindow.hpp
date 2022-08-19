#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>
// #include <QAction>

namespace Ui {
  class MainWindow;
}

class QMdiArea;
class QAction;

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

private:
  void createActions();
  void createMenus();
  void createToolbar();
  void onExitActionTriggered();
  void onNewDialogTriggered();
  void onPreferenceTriggered();

  Ui::MainWindow *ui;
  std::unique_ptr<QMdiArea> m_workSpace;

  QAction* m_exitAction = nullptr;
  QAction* m_preferenceAction = nullptr;
  QAction* m_reloadTradeAction = nullptr;
  QAction* m_aboutAction = nullptr;
  QAction* m_newDialogAction = nullptr;
};

#endif // MAINWINDOW_HPP
