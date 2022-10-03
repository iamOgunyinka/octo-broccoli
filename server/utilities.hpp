#pragma once

#include <boost/algorithm/string.hpp>
#include <boost/utility/string_view.hpp>
#include <fstream>
#include <spdlog/spdlog.h>
#include <sstream>
#include <vector>
#include <map>

namespace korrelator {

namespace utilities {

enum constants_e { TimeoutMinutes = 30, OneGigabyte = 1'024 * 1'024 * 1'024 };

template <typename T, typename Container = std::deque<T>, bool use_cv = false>
struct threadsafe_container {
private:
  std::mutex mutex_{};
  Container container_{};
  std::size_t total_{};

public:
  threadsafe_container(Container &&container)
      : container_{std::move(container)}, total_{container_.size()} {}
  threadsafe_container() = default;
  threadsafe_container(threadsafe_container &&vec)
      : mutex_{std::move(vec.mutex_)},
        container_{std::move(vec.container_)}, total_{vec.total_} {}
  threadsafe_container &operator=(threadsafe_container &&) = delete;
  threadsafe_container(threadsafe_container const &) = delete;
  threadsafe_container &operator=(threadsafe_container const &) = delete;

  T get() {
    std::lock_guard<std::mutex> lock{mutex_};
    if (container_.empty())
      throw std::runtime_error{};
    T value = container_.front();
    container_.pop_front();
    --total_;
    return value;
  }
  template <typename U> void push_back(U &&data) {
    std::lock_guard<std::mutex> lock_{mutex_};
    container_.push_back(std::forward<U>(data));
    total_ = container_.size();
  }
  bool empty() {
    std::lock_guard<std::mutex> lock_{mutex_};
    return container_.empty();
  }
  std::size_t get_total() const { return total_; }
  std::size_t size() {
    std::lock_guard<std::mutex> lock_{mutex_};
    return container_.size();
  }
};

template <typename T, typename Container>
struct threadsafe_container<T, Container, true> {
private:
  std::mutex mutex_{};
  Container container_{};
  std::size_t total_{};
  std::condition_variable cv_{};

public:
  threadsafe_container(Container &&container)
      : container_{std::move(container)}, total_{container_.size()} {}
  threadsafe_container() = default;

  threadsafe_container(threadsafe_container &&vec)
      : mutex_{std::move(vec.mutex_)}, container_{std::move(vec.container_)},
        total_{vec.total_}, cv_{std::move(vec.cv_)} {}
  threadsafe_container &operator=(threadsafe_container &&) = delete;
  threadsafe_container(threadsafe_container const &) = delete;
  threadsafe_container &operator=(threadsafe_container const &) = delete;

  T get() {
    std::unique_lock<std::mutex> u_lock{mutex_};
    cv_.wait(u_lock, [this] { return !container_.empty(); });
    T value = container_.front();
    container_.pop_front();
    total_ = container_.size();
    return value;
  }

  template <typename U, typename Func>
  std::vector<T> remove_task(U &&keys, Func &&function) {
    if (container_.empty())
      return {};
    std::unique_lock<std::mutex> u_lock{mutex_};
    std::vector<T> result{};
    for (auto &task : container_) {
      if (function(task, keys))
        result.emplace_back(std::move(task));
    }
    return result;
  }

  template <typename U> void push_back(U &&data) {
    std::lock_guard<std::mutex> lock_{mutex_};
    container_.push_back(std::forward<U>(data));
    total_ = container_.size();
    cv_.notify_one();
  }
  bool empty() {
    std::lock_guard<std::mutex> lock_{mutex_};
    return container_.empty();
  }
  std::size_t get_total() const { return total_; }
  std::size_t size() {
    std::lock_guard<std::mutex> lock_{mutex_};
    return container_.size();
  }
};

template <std::size_t N>
bool status_in_codes(std::size_t const code,
                     std::array<std::size_t, N> const &codes) {
  for (auto const &stat_code : codes)
    if (code == stat_code)
      return true;
  return false;
}

template <typename Container, typename... IterList>
bool any_of(Container const &container, IterList &&...iter_list) {
  return (... || (std::cend(container) == iter_list));
}

template <typename T>
using threadsafe_cv_container = threadsafe_container<T, std::deque<T>, true>;

std::map<boost::string_view, boost::string_view>
parse_headers(std::string_view const &str);
void normalize_paths(std::string &str);
void remove_file(std::string &filename);
std::string decode_url(boost::string_view const &encoded_string);
std::string view_to_string(boost::string_view const &str_view);
std::string intlist_to_string(std::vector<uint32_t> const &vec);
std::string_view bv2sv(boost::string_view);
std::string str_to_sha1hash(std::string const &);
std::size_t timet_to_string(std::string &, std::size_t,
                            char const * = "%Y-%m-%d %H:%M:%S");
std::vector<boost::string_view> split_string_view(boost::string_view const &str,
                                                  char const *delimeter);
std::string get_todays_date();
} // namespace utilities
} // namespace korrelator
