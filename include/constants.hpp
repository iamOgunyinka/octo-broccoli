#pragma once

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
static size_t const futures_http_request_len;
static size_t const spot_http_request_len;
};

}
