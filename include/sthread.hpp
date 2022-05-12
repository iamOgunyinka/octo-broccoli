#pragma once

#include <QThread>
#include <memory>

namespace korrelator {
class Worker : public QObject {
  Q_OBJECT
public:
  explicit Worker(std::function<void()> func, QObject *parent = nullptr)
      : QObject(parent), m_func(std::move(func)) {}

  void startWork() { m_func(); }

private:
  std::function<void()> m_func;
};

struct ThreadCleanup {
  void operator()(QThread *thread) const {
    if (thread) {
      if (thread->isRunning()) {
        thread->quit();
        thread->wait();
      }
      delete thread;
      thread = nullptr;
    }
  }
};

using cthread_ptr = std::unique_ptr<QThread, ThreadCleanup>;
using worker_ptr = std::unique_ptr<Worker>;

} // namespace shiny_guide
