#include "wifi.hpp"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "sdkconfig.h"

namespace direcsun::comms {

namespace {

constexpr const char *TAG = "wifi";

esp_netif_t *s_netif = nullptr;

void start_mdns()
{
    esp_err_t err = mdns_init();
    if (err == ESP_OK) err = mdns_hostname_set(CONFIG_DIRECSUN_HOSTNAME);
    if (err == ESP_OK) err = mdns_instance_name_set("DirecSun box");
    if (err == ESP_OK) err = mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS failed (%s): use the address printed above instead of %s.local", esp_err_to_name(err),
                 CONFIG_DIRECSUN_HOSTNAME);
    } else {
        ESP_LOGI(TAG, "mDNS hostname %s.local", CONFIG_DIRECSUN_HOSTNAME);
    }
}

#if !CONFIG_DIRECSUN_WIFI_SOFTAP

constexpr uint64_t kReconnectDelayUs = 1000000;  // retry 1 s after a disconnect
esp_timer_handle_t s_reconnect_timer = nullptr;

void log_urls(const esp_netif_ip_info_t &ip)
{
    ESP_LOGI(TAG, "connected, address " IPSTR, IP2STR(&ip.ip));
    ESP_LOGI(TAG, "WebSocket: ws://%s.local/ws  or  ws://" IPSTR "/ws", CONFIG_DIRECSUN_HOSTNAME, IP2STR(&ip.ip));
}

void reconnect(void *)
{
    esp_wifi_connect();
}

// Runs on the default event loop task: never block here (DirecSun1 slept in its
// handler; the reconnect is now a one-shot timer instead).
void on_event(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "joining \"%s\"", CONFIG_DIRECSUN_WIFI_SSID);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto *d = static_cast<const wifi_event_sta_disconnected_t *>(data);
        ESP_LOGW(TAG, "disconnected (reason %d), retrying in 1 s", d != nullptr ? d->reason : -1);
        if (s_reconnect_timer != nullptr && !esp_timer_is_active(s_reconnect_timer)) {
            esp_timer_start_once(s_reconnect_timer, kReconnectDelayUs);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *e = static_cast<const ip_event_got_ip_t *>(data);
        log_urls(e->ip_info);
    }
}

bool start_station()
{
    if (std::strlen(CONFIG_DIRECSUN_WIFI_SSID) == 0) {
        ESP_LOGE(TAG, "no Wi-Fi network configured: set CONFIG_DIRECSUN_WIFI_SSID and "
                      "CONFIG_DIRECSUN_WIFI_PASSWORD in firmware/sdkconfig.defaults.local, delete "
                      "firmware/sdkconfig, and rebuild. The box stays offline and safe.");
        return false;
    }
    s_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_netif, CONFIG_DIRECSUN_HOSTNAME);

    const esp_timer_create_args_t timer_args = {
        .callback = reconnect,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_reconnect",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconnect_timer));

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr));

    wifi_config_t config = {};
    std::strncpy(reinterpret_cast<char *>(config.sta.ssid), CONFIG_DIRECSUN_WIFI_SSID, sizeof config.sta.ssid);
    std::strncpy(reinterpret_cast<char *>(config.sta.password), CONFIG_DIRECSUN_WIFI_PASSWORD,
                 sizeof config.sta.password);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));

#if CONFIG_DIRECSUN_WIFI_STATIC_IP
    esp_netif_ip_info_t ip = {};
    if (esp_netif_str_to_ip4(CONFIG_DIRECSUN_WIFI_IP, &ip.ip) != ESP_OK ||
        esp_netif_str_to_ip4(CONFIG_DIRECSUN_WIFI_GATEWAY, &ip.gw) != ESP_OK ||
        esp_netif_str_to_ip4(CONFIG_DIRECSUN_WIFI_NETMASK, &ip.netmask) != ESP_OK) {
        ESP_LOGE(TAG, "static address settings do not parse; using DHCP instead");
    } else {
        esp_netif_dhcpc_stop(s_netif);
        ESP_ERROR_CHECK(esp_netif_set_ip_info(s_netif, &ip));
        ESP_LOGI(TAG, "static address " IPSTR, IP2STR(&ip.ip));
    }
#endif

    ESP_ERROR_CHECK(esp_wifi_start());
    // Latency over power: power save holds frames for the beacon interval.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    return true;
}

#else  // CONFIG_DIRECSUN_WIFI_SOFTAP

bool start_softap()
{
    s_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    wifi_config_t config = {};
    std::strncpy(reinterpret_cast<char *>(config.ap.ssid), CONFIG_DIRECSUN_SOFTAP_SSID, sizeof config.ap.ssid);
    config.ap.ssid_len = static_cast<uint8_t>(std::strlen(CONFIG_DIRECSUN_SOFTAP_SSID));
    std::strncpy(reinterpret_cast<char *>(config.ap.password), CONFIG_DIRECSUN_SOFTAP_PASSWORD,
                 sizeof config.ap.password);
    config.ap.channel = CONFIG_DIRECSUN_SOFTAP_CHANNEL;
    config.ap.max_connection = 3;
    const std::size_t pass_len = std::strlen(CONFIG_DIRECSUN_SOFTAP_PASSWORD);
    if (pass_len == 0) {
        config.ap.authmode = WIFI_AUTH_OPEN;
    } else if (pass_len >= 8) {
        config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ESP_LOGE(TAG, "SoftAP password must be 8 or more characters (or empty for an open network)");
        return false;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    esp_netif_ip_info_t ip = {};
    esp_netif_get_ip_info(s_netif, &ip);
    ESP_LOGI(TAG, "SoftAP \"%s\" on channel %d, address " IPSTR, CONFIG_DIRECSUN_SOFTAP_SSID,
             CONFIG_DIRECSUN_SOFTAP_CHANNEL, IP2STR(&ip.ip));
    ESP_LOGI(TAG, "WebSocket: ws://" IPSTR "/ws  or  ws://%s.local/ws", IP2STR(&ip.ip), CONFIG_DIRECSUN_HOSTNAME);
    return true;
}

#endif

}  // namespace

bool wifi_start()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
#if CONFIG_DIRECSUN_WIFI_SOFTAP
    const bool ok = start_softap();
#else
    const bool ok = start_station();
#endif
    if (ok) start_mdns();
    return ok;
}

}  // namespace direcsun::comms
