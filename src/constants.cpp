#include "constants.hpp"
#include <string.h>

namespace korrelator
{

char const *const constants::binance_ws_futures_url = "fstream.binance.com";
char const *const constants::binance_ws_spot_url = "stream.binance.com";
char const *const constants::binance_ws_spot_port = "9443";
char const *const constants::binance_ws_futures_port = "443";
char const *const constants::kucoin_https_spot_host = "api.kucoin.com";
char const *const constants::kucoin_https_spot_port = "443";
char const *const constants::kc_spot_http_request =
    "POST /api/v1/bullet-public HTTP/1.1\r\n"
    "Host: api.kucoin.com\r\n"
    "Accept: */*\r\n"
    "Content-Type: application/json\r\n"
    "User-Agent: postman\r\n\r\n";

size_t const constants::spot_http_request_len = strlen(kc_spot_http_request);
char const *const constants::kc_futures_api_url = "api-futures.kucoin.com";
char const *const constants::kc_futures_http_request =
    "POST /api/v1/bullet-public HTTP/1.1\r\n"
    "Host: api-futures.kucoin.com\r\n"
    "Accept: */*\r\n"
    "Content-Type: application/json\r\n"
    "User-Agent: postman\r\n\r\n";
size_t const constants::futures_http_request_len = strlen(kc_futures_http_request);
}
