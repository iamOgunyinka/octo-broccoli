#include "mainwindow.hpp"
#include <QApplication>
#include <QProcess>
#include <QDebug>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>

static char const *kRunLogic = "run__correlator__program";
static char const *kRunLogicValue = "run__correlator__program";
static char const *kWindowsHKey = "HKEY_LOCAL_MACHINE";
static char const *kWindowsRegistryPath =
    R"(SOFTWARE\Microsoft\Windows\Windows Error Reporting)";

namespace korrelator {
  void ShowNonQtMessageBox(char const *title, char const *message);
  bool addKeyToRegistryPath(char const * const parentPath, char const * const newPath);
}

static int runCorrelatorProgram(int &argc, char **argv) {
  QApplication a(argc, argv);
  MainWindow w;
  w.showMaximized();
  return a.exec();
}

QString getLocalDumpSite() {
  QString expectedCrashDumpsPath = qgetenv("localAppdata");

  if (expectedCrashDumpsPath.isEmpty()) {
    QStringList const locations =
        QStandardPaths::standardLocations(QStandardPaths::AppLocalDataLocation);
    expectedCrashDumpsPath.clear();
    char const *localAppData = "AppData/Local";
    for (auto const &location: locations) {
      if (location.contains(localAppData)) {
        expectedCrashDumpsPath = location;
        break;
      }
    }
    if (expectedCrashDumpsPath.isEmpty())
      return expectedCrashDumpsPath;
    auto const indexOfAppDataLocal = expectedCrashDumpsPath.lastIndexOf(localAppData);
    expectedCrashDumpsPath = expectedCrashDumpsPath.left(indexOfAppDataLocal + strlen(localAppData));
    expectedCrashDumpsPath.replace('/', '\\');
  }

  if (!expectedCrashDumpsPath.endsWith(QDir::separator()))
    expectedCrashDumpsPath += QDir::separator();
  expectedCrashDumpsPath += "CrashDumps";
  return expectedCrashDumpsPath;
}

static void showAdminPrivilegeNeededErrorAndExit() {
  static char const *messageBody =
      "We need to edit and add new keys to your registry to"
      " enable coredumps. Please restart this app as an"
      " administrator";
  korrelator::ShowNonQtMessageBox("Correlator", messageBody);
}

bool CheckWindowsRegistryForCoreDumpSupport() {
  bool needsRestart = false;
  QSettings settings(QString(kWindowsHKey) + "\\" + kWindowsRegistryPath,
                     QSettings::NativeFormat);

  if (!settings.childGroups().contains("LocalDumps")) {
      if (!settings.isWritable()) {
        showAdminPrivilegeNeededErrorAndExit();
        return false;
      }
      if (!korrelator::addKeyToRegistryPath(kWindowsRegistryPath, "LocalDumps"))
        return false;
      needsRestart = true;
  }

  if (!settings.contains("LocalDumps/CustomDumpFlags")) {
      if (!settings.isWritable()) {
        showAdminPrivilegeNeededErrorAndExit();
        return false;
      }

      settings.setValue("LocalDumps/CustomDumpFlags", 0);
      needsRestart = true;
  }

  if (!settings.contains("LocalDumps/DumpCount")) {
      settings.setValue("LocalDumps/DumpCount", 10);
      needsRestart = true;
  }

  if (!settings.contains("LocalDumps/DumpFolder")) {
      auto const crashSite = getLocalDumpSite();
      if (crashSite.isEmpty())
        return false;
      settings.setValue("LocalDumps/DumpFolder", crashSite);
      needsRestart = true;
  }

  if (!settings.contains("LocalDumps/DumpType")) {
      settings.setValue("LocalDumps/DumpType", 2);
      needsRestart = true;
  }

  settings.sync();

  if (needsRestart) {
    korrelator::ShowNonQtMessageBox(
          "Correlator",
          "Please restart your computer for the changes made to your"
          " Registry to take place.");
  }
  return !needsRestart;
}

static int monitorCorrelatorProgram(int &argc, char **argv) {
   QCoreApplication app{argc, argv};
   QProcess proc;

   int const maxRetries = 5;
   int retries = 0;
   auto onFinished = [&](int retcode, QProcess::ExitStatus status) mutable {
      qDebug() << status;
      if (status == QProcess::CrashExit) {
        if (++retries >= maxRetries)
          return app.exit(EXIT_FAILURE);
        proc.start();      // restart the app if the app crashed
      } else {
         app.exit(retcode); // no restart required
      }
   };

   if (!CheckWindowsRegistryForCoreDumpSupport())
     return EXIT_FAILURE;

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
#ifndef _DEBUG
  if (qgetenv(kRunLogic) != kRunLogicValue)
    return monitorCorrelatorProgram(argc, argv);

  qunsetenv(kRunLogic);
#endif
  return runCorrelatorProgram(argc, argv);
}
