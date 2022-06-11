#pragma once

#include <deque>
#include <mutex>

namespace korrelator {

template <typename T, typename Container = std::deque<T>>
struct waitable_container_t {
private:
  std::mutex mutex_{};
  Container container_{};
  std::condition_variable cv_{};

public:
  waitable_container_t(Container &&container)
      : container_{std::move(container)} {}
  waitable_container_t() = default;

  waitable_container_t(waitable_container_t &&vec)
      : mutex_{std::move(vec.mutex_)},
        container_{std::move(vec.container_)}, cv_{std::move(vec.cv_)} {}
  waitable_container_t &operator=(waitable_container_t &&) = delete;
  waitable_container_t(waitable_container_t const &) = delete;
  waitable_container_t &operator=(waitable_container_t const &) = delete;

  T get() {
    std::unique_lock<std::mutex> u_lock{mutex_};
    cv_.wait(u_lock, [this] { return !container_.empty(); });
    T value{std::move(container_.front())};
    container_.pop_front();
    return value;
  }

  template <typename U> void append(U &&data) {
    std::lock_guard<std::mutex> lock_{mutex_};
    container_.push_back(std::forward<U>(data));
    cv_.notify_all();
  }
};

} // namespace korrelator
