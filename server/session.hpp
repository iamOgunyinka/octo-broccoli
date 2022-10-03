#pragma once

#include "fields_alloc.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>

namespace korrelator {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

using url_query = std::map<boost::string_view, boost::string_view>;
using callback_t = std::function<void(http::request<http::string_body> const &,
                                      url_query const &)>;
using alloc_t = fields_alloc<char>;

struct rule_t {
  std::size_t num_verbs_{};
  std::array<http::verb, 3> verbs_{};
  callback_t route_callback_;

  rule_t(std::initializer_list<http::verb> &&verbs, callback_t callback);
};

class endpoint_t {
  std::map<std::string, rule_t> endpoints;
  using iterator = std::map<std::string, rule_t>::iterator;

public:
  void add_endpoint(std::string const &, std::initializer_list<http::verb>,
                    callback_t &&);
  std::optional<endpoint_t::iterator> get_rules(std::string const &target);
  std::optional<iterator> get_rules(boost::string_view const &target);
};

enum class error_type_e {
  HasError = 0,
  NoError = 1,
  ResourceNotFound,
  RequiresUpdate,
  BadRequest,
  ServerError,
  MethodNotAllowed,
  Unauthorized
};

using string_response = http::response<http::string_body>;
using empty_response = http::response<http::empty_body>;
using string_request = http::request<http::string_body>;
using dynamic_request = http::request_parser<http::string_body>;
using url_query = std::map<boost::string_view, boost::string_view>;
using nlohmann::json;
using optional_dyn_body = std::optional<dynamic_request>;
using optional_string_body =
    std::optional<http::request_parser<http::string_body>>;
using optional_empty_parser =
    std::optional<http::request_parser<http::empty_body>>;

class session {
  static std::filesystem::path const download_path;

  asio::io_context &io_context_;
  beast::tcp_stream tcp_stream_;
  beast::flat_buffer buffer_{};
  optional_empty_parser empty_body_parser_{};
  optional_dyn_body dynamic_body_parser_{};
  optional_string_body client_request_{};
  boost::string_view content_type_{};
  std::shared_ptr<void> resp_;
  endpoint_t endpoint_apis_;
  bool is_shutdown_ = false;

  std::optional<http::response<http::file_body, http::basic_fields<alloc_t>>>
      file_response_;
  alloc_t alloc_{8192};
  // The file-based response serializer.
  std::optional<
      http::response_serializer<http::file_body, http::basic_fields<alloc_t>>>
      file_serializer_;

private:
  session *shared_from_this() { return this; }
  void add_endpoint_interfaces();
  void http_read_data();
  void shutdown_socket();
  bool is_json() const;
  bool is_binary_data() const;
  void send_response(string_response &&response);
  void send_response(empty_response &&response);
  void error_handler(string_response &&response, bool close_socket = false);

  void on_header_read(beast::error_code, std::size_t const);
  void binary_data_read(beast::error_code, std::size_t const);
  void on_data_read(beast::error_code, std::size_t);

  void on_data_written(beast::error_code, std::size_t);

  void handle_requests(string_request const &request);

  void upload_handler(string_request const &, url_query const &);
  void index_page_handler(string_request const &request,
                          url_query const &query);
  static string_response forbidden(string_request const &);
  static string_response json_success(json const &body,
                                      string_request const &req);
  static string_response success(char const *message, string_request const &);
  static string_response failed(char const *message, string_request const &);
  static string_response bad_request(std::string const &message,
                                     string_request const &);
  static string_response not_found(string_request const &);
  static string_response staff_not_found(string_request const &);
  static string_response role_not_found(string_request const &);
  static empty_response redirect_to(char const *address,
                                    string_request const &);
  static string_response method_not_allowed(string_request const &request);
  static string_response server_error(std::string const &, error_type_e,
                                      string_request const &);
  static string_response get_error(std::string const &, error_type_e,
                                   http::status, string_request const &);
  static url_query split_optional_queries(boost::string_view const &args);

public:
  session(asio::io_context &io, asio::ip::tcp::socket &&socket);
  void run();
  bool is_closed() const { return is_shutdown_; }
};
} // namespace korrelator
