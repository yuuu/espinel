/* Network interfaces on ESP-IDF: the Wi-Fi station and, under QEMU, the
   OpenCores Ethernet that `idf.py qemu` attaches (-nic user,model=open_eth).
   Each one is brought up until DHCP gives it an address; after that lwIP
   carries the sockets of Spinel's `require "socket"`, which needs nothing
   from this package. The Ruby half is network.rb.

   Results are esp_err_t codes, or for a connect 0 (an address), -1 (the
   network refused us: a wrong password, no AP with the security asked
   for) and -2 (the time ran out). */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#if SOC_WIFI_SUPPORTED
#include "esp_wifi.h"
#include "nvs_flash.h"
#endif
#if CONFIG_ETH_USE_OPENETH
#include "esp_eth.h"
#endif

static const char *TAG = "network";

/* network.rb names them by these numbers */
enum { IF_WIFI, IF_ETH, IF_COUNT };
#define GOT_IP(i) (BIT0 << ((i) * 2))
#define LOST(i)   (BIT1 << ((i) * 2))

static esp_netif_t *netifs[IF_COUNT];
static EventGroupHandle_t events;

/* The netif layer, the default event loop and the bits the connects wait
   on: whatever interface comes up first sets them up. */
static esp_err_t net_init(void) {
  static bool done;
  if (done) return ESP_OK;
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK) return err;
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
  events = xEventGroupCreate();
  if (!events) return ESP_ERR_NO_MEM;
  done = true;
  return ESP_OK;
}

const char *espinel_net_err_name(int err) { return esp_err_to_name(err); }

static int64_t deadline_after(int timeout_ms) { return esp_timer_get_time() + (int64_t)timeout_ms * 1000; }

static TickType_t ticks_until(int64_t deadline) {
  int64_t left = deadline - esp_timer_get_time();
  return left > 0 ? pdMS_TO_TICKS((left + 999) / 1000) : 0;
}

/* address, netmask or gateway (which 0, 1, 2) of an interface with an
   address; "" otherwise */
const char *espinel_net_ipv4(int ifc, int which) {
  static char buf[16];
  esp_netif_ip_info_t info;
  buf[0] = '\0';
  if (ifc < 0 || ifc >= IF_COUNT || !netifs[ifc] || !events) return buf;
  if (!(xEventGroupGetBits(events) & GOT_IP(ifc))) return buf;
  if (esp_netif_get_ip_info(netifs[ifc], &info) != ESP_OK) return buf;
  esp_ip4_addr_t *a = which == 1 ? &info.netmask : which == 2 ? &info.gw : &info.ip;
  snprintf(buf, sizeof buf, IPSTR, IP2STR(a));
  return buf;
}

int espinel_net_up(int ifc) {
  return ifc >= 0 && ifc < IF_COUNT && events && (xEventGroupGetBits(events) & GOT_IP(ifc)) ? 1 : 0;
}

int espinel_net_dhcp_supplied(int ifc) {
  esp_netif_dhcp_status_t st;
  if (!espinel_net_up(ifc)) return 0;
  return esp_netif_dhcpc_get_status(netifs[ifc], &st) == ESP_OK && st == ESP_NETIF_DHCP_STARTED;
}

/* ---- Wi-Fi station ---- */

#if SOC_WIFI_SUPPORTED

int espinel_wifi_auth_open(void) { return WIFI_AUTH_OPEN; }
int espinel_wifi_auth_wpa_psk(void) { return WIFI_AUTH_WPA_PSK; }
int espinel_wifi_auth_wpa2_psk(void) { return WIFI_AUTH_WPA2_PSK; }
int espinel_wifi_auth_wpa_wpa2_psk(void) { return WIFI_AUTH_WPA_WPA2_PSK; }
int espinel_wifi_auth_wpa3_psk(void) { return WIFI_AUTH_WPA3_PSK; }

static bool wifi_initialized, wifi_started;
static volatile int wifi_reason;

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_reason = ((wifi_event_sta_disconnected_t *)data)->reason;
    xEventGroupClearBits(events, GOT_IP(IF_WIFI));
    xEventGroupSetBits(events, LOST(IF_WIFI));
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(events, GOT_IP(IF_WIFI));
  } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
    xEventGroupClearBits(events, GOT_IP(IF_WIFI));
  }
}

/* The driver logs every step at INFO; keep its warnings. */
static void wifi_quiet(void) {
  static const char *const tags[] = {
    "wifi", "wifi_init", "phy_init", "pp", "net80211", "esp_netif_handlers",
  };
  for (size_t i = 0; i < sizeof tags / sizeof tags[0]; i++) esp_log_level_set(tags[i], ESP_LOG_WARN);
}

/* NVS keeps the PHY's calibration and the driver's settings. */
static esp_err_t nvs_init(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    err = nvs_flash_erase();
    if (err == ESP_OK) err = nvs_flash_init();
  }
  return err;
}

/* country: a two-letter code, or NULL to keep the driver's default */
int espinel_wifi_init(const char *country) {
  if (wifi_initialized) return ESP_OK;
  wifi_quiet();
  esp_err_t err = nvs_init();
  if (err == ESP_OK) err = net_init();
  if (err != ESP_OK) return err;
  netifs[IF_WIFI] = esp_netif_create_default_wifi_sta();
  if (!netifs[IF_WIFI]) return ESP_FAIL;
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if ((err = esp_wifi_init(&cfg)) != ESP_OK) return err;
  if ((err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event, NULL)) != ESP_OK ||
      (err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL)) != ESP_OK ||
      (err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, wifi_event, NULL)) != ESP_OK)
    return err;
  if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return err;
  if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return err;
  if (country && (err = esp_wifi_set_country_code(country, true)) != ESP_OK) return err;
  wifi_initialized = true;
  return ESP_OK;
}

int espinel_wifi_initialized(void) { return wifi_initialized; }

/* Reasons that another try will not change. Anything else (the AP not
   heard in a scan, a beacon timeout) is tried again until the time runs
   out. */
static bool wifi_refused(int reason) {
  switch (reason) {
  case WIFI_REASON_AUTH_FAIL:
  case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
  case WIFI_REASON_HANDSHAKE_TIMEOUT:
  case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
  case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
  case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return true;
  default: return false;
  }
}

int espinel_wifi_connect(const char *ssid, const char *password, int auth, int timeout_ms) {
  if (!wifi_initialized) return ESP_ERR_WIFI_NOT_INIT;
  if (!ssid) return ESP_ERR_INVALID_ARG;
  esp_wifi_disconnect();
  wifi_config_t conf = { 0 };
  strlcpy((char *)conf.sta.ssid, ssid, sizeof conf.sta.ssid);
  if (password) strlcpy((char *)conf.sta.password, password, sizeof conf.sta.password);
  conf.sta.threshold.authmode = (wifi_auth_mode_t)auth;
  conf.sta.pmf_cfg.capable = true;
  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &conf);
  if (err != ESP_OK) return err;
  if (!wifi_started) {
    if ((err = esp_wifi_start()) != ESP_OK) return err;
    wifi_started = true;
  }
  int64_t deadline = deadline_after(timeout_ms);
  xEventGroupClearBits(events, GOT_IP(IF_WIFI) | LOST(IF_WIFI));
  for (;;) {
    if ((err = esp_wifi_connect()) != ESP_OK) return err;
    EventBits_t bits =
        xEventGroupWaitBits(events, GOT_IP(IF_WIFI) | LOST(IF_WIFI), pdFALSE, pdFALSE, ticks_until(deadline));
    if (bits & GOT_IP(IF_WIFI)) return 0;
    if (!(bits & LOST(IF_WIFI))) break;
    xEventGroupClearBits(events, LOST(IF_WIFI));
    if (wifi_refused(wifi_reason)) {
      ESP_LOGW(TAG, "%s refused the connection (reason %d)", ssid, wifi_reason);
      return -1;
    }
    if (ticks_until(deadline) == 0) break;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  esp_wifi_disconnect();
  return -2;
}

int espinel_wifi_disconnect(void) {
  if (!wifi_started) return ESP_OK;
  esp_wifi_disconnect();
  esp_err_t err = esp_wifi_stop();
  wifi_started = false;
  xEventGroupClearBits(events, GOT_IP(IF_WIFI));
  return err;
}

/* PicoRuby's link states (CYW43's numbering): 0 down, 2 joined without an
   address, 3 up, 5 no Wi-Fi */
int espinel_wifi_link_status(void) {
  wifi_ap_record_t ap;
  if (!wifi_initialized) return 5;
  if (espinel_net_up(IF_WIFI)) return 3;
  if (wifi_started && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return 2;
  return 0;
}

#else /* no Wi-Fi on this SoC */

int espinel_wifi_auth_open(void) { return 0; }
int espinel_wifi_auth_wpa_psk(void) { return 2; }
int espinel_wifi_auth_wpa2_psk(void) { return 3; }
int espinel_wifi_auth_wpa_wpa2_psk(void) { return 4; }
int espinel_wifi_auth_wpa3_psk(void) { return 6; }
int espinel_wifi_init(const char *country) {
  (void)country;
  return ESP_ERR_NOT_SUPPORTED;
}
int espinel_wifi_initialized(void) { return 0; }
int espinel_wifi_connect(const char *s, const char *p, int a, int t) {
  (void)s;
  (void)p;
  (void)a;
  (void)t;
  return ESP_ERR_NOT_SUPPORTED;
}
int espinel_wifi_disconnect(void) { return ESP_OK; }
int espinel_wifi_link_status(void) { return 5; }

#endif

/* ---- QEMU's OpenCores Ethernet ---- */

#if CONFIG_ETH_USE_OPENETH

static void eth_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)data;
  if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP) {
    xEventGroupSetBits(events, GOT_IP(IF_ETH));
  } else if (base == IP_EVENT && id == IP_EVENT_ETH_LOST_IP) {
    xEventGroupClearBits(events, GOT_IP(IF_ETH));
  } else if (base == ETH_EVENT && id == ETHERNET_EVENT_DISCONNECTED) {
    xEventGroupClearBits(events, GOT_IP(IF_ETH));
  }
}

static esp_err_t eth_start(void) {
  esp_err_t err = net_init();
  if (err != ESP_OK) return err;
  esp_netif_config_t netif_conf = ESP_NETIF_DEFAULT_ETH();
  netifs[IF_ETH] = esp_netif_new(&netif_conf);
  if (!netifs[IF_ETH]) return ESP_FAIL;
  eth_mac_config_t mac_conf = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_conf = ETH_PHY_DEFAULT_CONFIG();
  phy_conf.autonego_timeout_ms = 100;
  esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_conf);
  esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_conf);
  if (!mac || !phy) return ESP_FAIL;
  esp_eth_config_t conf = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t eth;
  if ((err = esp_eth_driver_install(&conf, &eth)) != ESP_OK) return err;
  if ((err = esp_netif_attach(netifs[IF_ETH], esp_eth_new_netif_glue(eth))) != ESP_OK) return err;
  if ((err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, eth_event, NULL)) != ESP_OK ||
      (err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, eth_event, NULL)) != ESP_OK ||
      (err = esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, eth_event, NULL)) != ESP_OK)
    return err;
  return esp_eth_start(eth);
}

int espinel_eth_connect(int timeout_ms) {
  static bool started;
  if (!started) {
    esp_err_t err = eth_start();
    if (err != ESP_OK) return err;
    started = true;
  }
  EventBits_t bits =
      xEventGroupWaitBits(events, GOT_IP(IF_ETH), pdFALSE, pdFALSE, ticks_until(deadline_after(timeout_ms)));
  return bits & GOT_IP(IF_ETH) ? 0 : -2;
}

#else

int espinel_eth_connect(int timeout_ms) {
  (void)timeout_ms;
  return ESP_ERR_NOT_SUPPORTED;
}

#endif
