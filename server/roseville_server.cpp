#include "server.hpp"
#include <thread>

int main(int argc, char *argv[]) {
  korrelator::command_line_interface args{};
  auto const thread_count = std::thread::hardware_concurrency();
  args.thread_count = thread_count;
  args.port = 40'002;

  korrelator::asio::io_context context{static_cast<int>(thread_count)};
  auto server_instance = std::make_shared<korrelator::server>(context, args);
  server_instance->run();

  std::vector<std::thread> threads{};
  threads.reserve(args.thread_count);
  for (std::size_t counter = 0; counter < args.thread_count; ++counter) {
    threads.emplace_back([&] { context.run(); });
  }
  context.run();
  return EXIT_SUCCESS;
}
