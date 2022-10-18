#include "session.hpp"
#include "utilities.hpp"
#include <boost/algorithm/string.hpp>
#include <fstream>
#include <spdlog/spdlog.h>

namespace korrelator {
using string_pair = std::pair<std::string, std::string>;
using optional_string_pair = std::optional<string_pair>;

enum result_type { count = 0, complete_data = 1 };

using namespace fmt::v6::literals;

rule_t::rule_t(std::initializer_list<http::verb> &&verbs, callback_t callback)
    : num_verbs_{verbs.size()}, route_callback_{std::move(callback)} {
  if (verbs.size() > 5)
    throw std::runtime_error{"maximum number of verbs is 5"};
  for (int i = 0; i != verbs.size(); ++i) {
    verbs_[i] = *(verbs.begin() + i);
  }
}

void endpoint_t::add_endpoint(std::string const &route,
                              std::initializer_list<http::verb> verbs,
                              callback_t &&callback) {
  if (route.empty() || route[0] != '/')
    throw std::runtime_error{"A valid route starts with a /"};
  endpoints.emplace(route, rule_t{std::move(verbs), std::move(callback)});
}

std::filesystem::path const session::download_path =
    std::filesystem::current_path() / "uploads";

void session::add_endpoint_interfaces() {
  using http::verb;
  endpoint_apis_.add_endpoint(
      "/", {verb::get},
      std::bind(&session::index_page_handler, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
  endpoint_apis_.add_endpoint(
      "/upload", {verb::post},
      std::bind(&session::upload_handler, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
}

void session::handle_requests(string_request const &request) {
  std::string const request_target{utilities::decode_url(request.target())};
  if (request_target.empty())
    return index_page_handler(request, {});
  auto const method = request.method();
  boost::string_view request_target_view = request_target;
  auto split = utilities::split_string_view(request_target_view, "?");
  if (auto iter = endpoint_apis_.get_rules(split[0]); iter.has_value()) {
    auto iter_end =
        iter.value()->second.verbs_.cbegin() + iter.value()->second.num_verbs_;
    auto found_iter =
        std::find(iter.value()->second.verbs_.cbegin(), iter_end, method);
    if (found_iter == iter_end) {
      return error_handler(method_not_allowed(request));
    }
    boost::string_view const query_string = split.size() > 1 ? split[1] : "";
    auto url_query_{split_optional_queries(query_string)};
    return iter.value()->second.route_callback_(request, url_query_);
  } else {
    return error_handler(not_found(request));
  }
}

std::optional<endpoint_t::iterator>
endpoint_t::get_rules(std::string const &target) {
  auto iter = endpoints.find(target);
  if (iter == endpoints.end())
    return std::nullopt;
  return iter;
}

std::optional<endpoint_t::iterator>
endpoint_t::get_rules(boost::string_view const &target) {
  return get_rules(target.to_string());
}

void session::http_read_data() {
  buffer_.clear();
  empty_body_parser_.emplace();
  empty_body_parser_->body_limit(utilities::OneGigabyte);
  beast::get_lowest_layer(tcp_stream_)
      .expires_after(std::chrono::minutes(utilities::TimeoutMinutes));
  http::async_read_header(
      tcp_stream_, buffer_, *empty_body_parser_,
      beast::bind_front_handler(&session::on_header_read, shared_from_this()));
}

void session::on_header_read(beast::error_code ec, std::size_t const) {
  if (ec == http::error::end_of_stream)
    return shutdown_socket();
  if (ec) {
    return error_handler(
        server_error(ec.message(), error_type_e::ServerError, string_request{}),
        true);
  } else {
    content_type_ = empty_body_parser_->get()[http::field::content_type];
    if (!is_binary_data()) {
      client_request_.emplace(std::move(*empty_body_parser_));
      http::async_read(tcp_stream_, buffer_, *client_request_,
                       beast::bind_front_handler(&session::on_data_read,
                                                 shared_from_this()));
    } else {
      dynamic_body_parser_.emplace(std::move(*empty_body_parser_));
      dynamic_body_parser_->body_limit(utilities::OneGigabyte);
      http::async_read(tcp_stream_, buffer_, *dynamic_body_parser_,
                       beast::bind_front_handler(&session::binary_data_read,
                                                 shared_from_this()));
    }
  }
}

void session::binary_data_read(beast::error_code ec,
                               std::size_t bytes_transferred) {
  if (ec == http::error::end_of_stream) { // end of connection
    return shutdown_socket();
  } else if (ec == http::error::body_limit) {
    return error_handler(
        server_error(ec.message(), error_type_e::ServerError, string_request{}),
        true);
  } else if (ec) {
    fputs(ec.message().c_str(), stderr);
    return error_handler(
        server_error(ec.message(), error_type_e::ServerError, string_request{}),
        true);
  }
  auto &request = dynamic_body_parser_->get();
  handle_requests(request);
}

void session::on_data_read(beast::error_code ec, std::size_t const) {
  if (ec == http::error::end_of_stream) { // end of connection
    return shutdown_socket();
  } else if (ec == http::error::body_limit) {
    return error_handler(
        server_error(ec.message(), error_type_e::ServerError, string_request{}),
        true);
  } else if (ec) {
    fputs(ec.message().c_str(), stderr);
    return error_handler(
        server_error(ec.message(), error_type_e::ServerError, string_request{}),
        true);
  } else {
    handle_requests(client_request_->get());
  }
}

void session::shutdown_socket() {
  beast::error_code ec{};
  beast::get_lowest_layer(tcp_stream_)
      .socket()
      .shutdown(asio::socket_base::shutdown_send, ec);
  beast::get_lowest_layer(tcp_stream_).close();
  is_shutdown_ = true;
}

bool session::is_json() const {
  return content_type_.find("application/json") != std::string::npos;
}

bool session::is_binary_data() const {
  return content_type_.find("application/octet-stream") != std::string::npos;
}

void session::error_handler(string_response &&response, bool close_socket) {
  auto resp = std::make_shared<string_response>(std::move(response));
  resp_ = resp;
  if (!close_socket) {
    http::async_write(tcp_stream_, *resp,
                      beast::bind_front_handler(&session::on_data_written,
                                                shared_from_this()));
  } else {
    http::async_write(
        tcp_stream_, *resp,
        [self = shared_from_this()](auto const err_c, std::size_t const) {
          self->shutdown_socket();
        });
  }
}

void session::on_data_written(beast::error_code ec,
                              std::size_t const bytes_written) {
  if (ec) {
    return spdlog::error(ec.message());
  }
  resp_ = nullptr;
  http_read_data();
}

void session::upload_handler(string_request const &request,
                             url_query const &optional_query) {
  static std::string const uploads_directory{"uploads/"};
  auto const method = request.method();
  if (method == http::verb::post) {
    if (!dynamic_body_parser_) {
      return error_handler(bad_request("", request));
    }
    auto &parser = dynamic_body_parser_->get();

    if (!std::filesystem::exists(uploads_directory)) {
      std::error_code ec{};
      std::filesystem::create_directory(uploads_directory, ec);
    }
    boost::string_view filename_view = parser["filename"];
    if (filename_view.empty()) {
      return error_handler(bad_request("key parameters is missing", request));
    }

    std::string_view temp_filename_view(filename_view.data(),
                                        filename_view.size());
    std::string file_path{
        "{}{}.dmp"_format(uploads_directory, temp_filename_view)};
    std::size_t counter{1};
    while (std::filesystem::exists(file_path)) {
      file_path = "{}{}_{}.dmp"_format(uploads_directory, temp_filename_view,
                                       counter++);
    }
    FILE *file = fopen(file_path.c_str(), "wb");
    if (!file)
      return error_handler(server_error("unable to save file",
                                        error_type_e::ServerError, request));
    auto &body = request.body();
    try {
      fwrite(body.data(), 1, body.size(), file);
      fclose(file);
      return send_response(success("ok", request));
    } catch (std::exception const &e) {
      if (file)
        fclose(file);
      spdlog::error(e.what());
      return error_handler(bad_request("unable to process file", request));
    }
  }
  return error_handler(bad_request("unable to process file", request));
}

void session::index_page_handler(string_request const &request,
                                 url_query const &) {
  spdlog::info("[index_page_handler] {}", request.target().to_string());
  return send_response(redirect_to("https://duckduckgo.com", request));
}

session::session(asio::io_context &io, asio::ip::tcp::socket &&socket)
    : io_context_{io}, tcp_stream_{std::move(socket)} {
  add_endpoint_interfaces();
}

void session::run() { http_read_data(); }

string_response session::not_found(string_request const &request) {
  return get_error("resource not found", error_type_e::ResourceNotFound,
                   http::status::not_found, request);
}

string_response session::staff_not_found(string_request const &request) {
  return get_error("no staff found", error_type_e::ResourceNotFound,
                   http::status::precondition_failed, request);
}

string_response session::role_not_found(string_request const &request) {
  return get_error("role defined not found", error_type_e::ResourceNotFound,
                   http::status::precondition_failed, request);
}

string_response session::server_error(std::string const &message,
                                      error_type_e type,
                                      string_request const &request) {
  return get_error(message, type, http::status::internal_server_error, request);
}

string_response session::bad_request(std::string const &message,
                                     string_request const &request) {
  return get_error(message, error_type_e::BadRequest, http::status::bad_request,
                   request);
}

string_response session::method_not_allowed(string_request const &req) {
  return get_error("method not allowed", error_type_e::MethodNotAllowed,
                   http::status::method_not_allowed, req);
}

string_response session::get_error(std::string const &error_message,
                                   error_type_e type, http::status status,
                                   string_request const &req) {
  json::object_t result_obj;
  result_obj["status"] = type;
  result_obj["message"] = error_message;
  json result = result_obj;

  string_response response{status, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = result.dump();
  response.prepare_payload();
  return response;
}

string_response session::json_success(json const &body,
                                      string_request const &req) {
  string_response response{http::status::ok, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = body.dump();
  response.prepare_payload();
  return response;
}

empty_response session::redirect_to(char const *address,
                                    string_request const &request) {
  http::response<http::empty_body> response(http::status::permanent_redirect,
                                            request.version());
  response.set(http::field::location, address);
  return response;
}

string_response session::success(char const *message,
                                 string_request const &req) {
  json::object_t result_obj;
  result_obj["status"] = error_type_e::NoError;
  result_obj["message"] = message;
  json result{result_obj};

  string_response response{http::status::ok, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = result.dump();
  response.prepare_payload();
  return response;
}

string_response session::failed(char const *message,
                                string_request const &req) {
  json::object_t result_obj;
  result_obj["status"] = error_type_e::HasError;
  result_obj["message"] = message;
  json result{result_obj};

  string_response response{http::status::forbidden, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = result.dump();
  response.prepare_payload();
  return response;
}

void session::send_response(string_response &&response) {
  auto resp = std::make_shared<string_response>(std::move(response));
  resp_ = resp;
  http::async_write(
      tcp_stream_, *resp,
      beast::bind_front_handler(&session::on_data_written, shared_from_this()));
}

void session::send_response(empty_response &&response) {
  auto resp = std::make_shared<string_response>(std::move(response));
  resp_ = resp;
  http::async_write(
      tcp_stream_, *resp,
      beast::bind_front_handler(&session::on_data_written, shared_from_this()));
}

string_response session::forbidden(string_request const &request) {
  string_response response(http::status::forbidden, request.version());
  response.set(http::field::content_type, "application/json");
  response.keep_alive(request.keep_alive());
  json::object_t result;
  result["what"] = "cannot access requested information, maybe due to timeout "
                   "or you're logged out";
  result["code"] = 403;
  response.body() = json(result).dump();
  response.prepare_payload();
  return response;
}

url_query
session::split_optional_queries(boost::string_view const &optional_query) {
  url_query result{};
  if (!optional_query.empty()) {
    auto queries = utilities::split_string_view(optional_query, "&");
    for (auto const &q : queries) {
      auto split = utilities::split_string_view(q, "=");
      if (split.size() < 2)
        continue;
      result.emplace(split[0], split[1]);
    }
  }
  return result;
}
} // namespace korrelator
