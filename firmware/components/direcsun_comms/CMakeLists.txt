# Ground link: Wi-Fi station (DHCP, mDNS, optional SoftAP), esp_http_server
# WebSocket on port 81, schema v1 JSON. Empty until Set 3; the REQUIRES list
# makes every networking dependency compile and link now.
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES direcsun_core esp_http_server esp_wifi espressif__mdns espressif__cjson
)
