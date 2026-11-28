/* esp_err_to_name for the host: the names of the codes the stubs answer. */
#include "stub.h"

const char *esp_err_to_name(int err) {
  switch (err) {
  case ESP_OK: return "ESP_OK";
  case ESP_FAIL: return "ESP_FAIL";
  case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
  case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
  case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
  case ESP_ERR_NOT_SUPPORTED: return "ESP_ERR_NOT_SUPPORTED";
  case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
  default: return "UNKNOWN ERROR";
  }
}
