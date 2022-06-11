#include "constants.hpp"
#include <string.h>

namespace korrelator {

#ifdef TESTNET
char const *const constants::binance_ws_spot_url = "testnet.binance.vision";
char const *const constants::binance_http_spot_host = "testnet.binance.vision";
char const *const constants::binance_ws_spot_port = "443";
char const *const constants::binance_ws_futures_url =
    "stream.binancefuture.com";
char const *const constants::binance_http_futures_host =
    "testnet.binancefuture.com";
char const *const constants::binance_ws_futures_port = "443";

char const *const constants::kucoin_https_spot_host =
    "openapi-sandbox.kucoin.com";
char const *const constants::kc_spot_http_request =
    "POST /api/v1/bullet-public HTTP/1.1\r\n"
    "Host: openapi-sandbox.kucoin.com\r\n"
    "Accept: */*\r\n"
    "Content-Type: application/json\r\n"
    "User-Agent: postman\r\n\r\n";
#else
char const *const constants::binance_ws_spot_url = "stream.binance.com";
char const *const constants::binance_http_spot_host = "api.binance.com";
char const *const constants::binance_ws_spot_port = "9443";

char const *const constants::binance_http_futures_host = "fapi.binance.com";
char const *const constants::binance_ws_futures_url = "fstream.binance.com";
char const *const constants::binance_ws_futures_port = "443";

char const *const constants::kucoin_https_spot_host = "api.kucoin.com";
char const *const constants::kc_spot_http_request =
    "POST /api/v1/bullet-public HTTP/1.1\r\n"
    "Host: api.kucoin.com\r\n"
    "Accept: */*\r\n"
    "Content-Type: application/json\r\n"
    "User-Agent: postman\r\n\r\n";
#endif

char const *const constants::kucoin_https_spot_port = "443";
size_t const constants::spot_http_request_len = strlen(kc_spot_http_request);

#ifdef TESTNET
char const *const constants::kc_futures_api_host =
    "api-sandbox-futures.kucoin.com";
char const *const constants::kc_futures_http_request =
    "POST /api/v1/bullet-public HTTP/1.1\r\n"
    "Host: api-sandbox-futures.kucoin.com\r\n"
    "Accept: */*\r\n"
    "Content-Type: application/json\r\n"
    "User-Agent: postman\r\n\r\n";
#else
char const *const constants::kc_futures_api_host = "api-futures.kucoin.com";
char const *const constants::kc_futures_http_request =
    "POST /api/v1/bullet-public HTTP/1.1\r\n"
    "Host: api-futures.kucoin.com\r\n"
    "Accept: */*\r\n"
    "Content-Type: application/json\r\n"
    "User-Agent: postman\r\n\r\n";
#endif

size_t const constants::futures_http_request_len =
    strlen(kc_futures_http_request);
} // namespace korrelator
