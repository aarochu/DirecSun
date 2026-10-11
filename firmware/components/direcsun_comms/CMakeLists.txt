# Ground link: Wi-Fi station (DHCP, mDNS, optional static address) or SoftAP,
# and the esp_http_server WebSocket on port 80 at /ws, the main UI's endpoint.
# The protocol itself (parsing, encoding, the wire sign) is in direcsun_core so
# it is host-tested; this component only moves frames. It reaches the control
# task through the Backend interface, so it never depends on main.
idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES direcsun_core
    PRIV_REQUIRES esp_http_server esp_wifi esp_netif esp_event esp_timer lwip espressif__mdns
)
