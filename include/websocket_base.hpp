#pragma once

#include <string>

namespace korrelator {

class websocket_base {
public:
  virtual ~websocket_base() = default;
  virtual void addSubscription(std::string const &) = 0;
  virtual void startFetching() = 0;
  virtual void requestStop() = 0;
};

} // namespace korrelator
