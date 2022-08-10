#pragma once

#include <cstddef>

namespace korrelator {

struct constants {
static char const *const binance_ws_futures_url;
static char const *const binance_ws_spot_url;
static char const *const binance_http_spot_host;
static char const *const binance_http_futures_host;
static char const *const binance_ws_spot_port;
static char const *const binance_ws_futures_port;

static char const *const kucoin_https_spot_host;
static char const *const kucoin_https_spot_port;
static char const *const kc_spot_http_request;
static char const *const kc_futures_api_host;
static char const *const kc_futures_http_request;

static char const * const root_dir;
static char const * const encrypted_config_filename;
static char const * const config_json_filename;
static char const * const app_json_filename;
static char const * const old_json_filename;
static char const * const trade_json_filename;

static size_t const futures_http_request_len;
static size_t const spot_http_request_len;
};

}
