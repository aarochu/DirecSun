#pragma once

// Wi-Fi and mDNS (FD section 15.1). Station mode with DHCP by default, an
// optional static address, or a SoftAP chosen at build time (DirecSun network
// menu). Never switches modes at run time.

namespace direcsun::comms {

// Brings the network up without waiting for a connection. Returns false if Wi-Fi
// could not be started (for example, no SSID configured); the reason is logged.
bool wifi_start();

}  // namespace direcsun::comms
