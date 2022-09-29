#include "mainwindow.hpp"
#include <QApplication>
#include <QProcess>
#include <QDebug>

static char const *kRunLogic = "run__correlator__program";
static char const *kRunLogicValue = "run__correlator__program";


static int runCorrelatorProgram(int &argc, char **argv) {
  QApplication a(argc, argv);
  MainWindow w;
  w.showMaximized();
  return a.exec();
}


static int monitorCorrelatorProgram(int &argc, char **argv) {
   QCoreApplication app{argc, argv};
   QProcess proc;
   auto onFinished = [&](int retcode, QProcess::ExitStatus status) {
      qDebug() << status;
      if (status == QProcess::CrashExit) {
         proc.start();      // restart the app if the app crashed
      } else {
         app.exit(retcode); // no restart required
      }
   };

   QObject::connect(&proc,
                    QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    onFinished);

   auto env = QProcessEnvironment::systemEnvironment();
   env.insert(kRunLogic, kRunLogicValue);
   env.insert("QT_LOGGING_TO_CONSOLE", "1");   // ensure that the debug output gets passed along
   proc.setProgram(app.applicationFilePath()); // logic and monitor are the same executable
   proc.setProcessEnvironment(env);
   proc.setProcessChannelMode(QProcess::ForwardedChannels);
   proc.start();
   return app.exec();
}

int main(int argc, char **argv) {
  if (qgetenv(kRunLogic) != kRunLogicValue)
    return monitorCorrelatorProgram(argc, argv);

  qunsetenv(kRunLogic);
  return runCorrelatorProgram(argc, argv);
}
