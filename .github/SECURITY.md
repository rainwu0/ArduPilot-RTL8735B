# Security policy

[繁體中文](SECURITY.zh-TW.md)

This policy covers this port: the RTL8735B HAL, its board types and its build scripts in this repository.

## Supported versions

Development happens on `main`, and fixes go there and into the next version. Each version's changes are in the [CHANGELOG](/libraries/AP_HAL_RTL8735B/CHANGELOG.md).

## Reporting a vulnerability

Report it privately through GitHub: [Report a vulnerability](https://github.com/rainwu0/ArduPilot-RTL8735B/security/advisories/new), on this repository's Security tab. The report and the discussion stay private until the advisory is published.

Include, as far as you can:

- the commit of this repository (`git rev-parse --short HEAD`) and the board type;
- what an attacker needs (for example: on the same Wi-Fi subnet, or holding the image file);
- the steps to reproduce it, and what happens.

An image built with a Wi-Fi settings file contains the credentials. Attach images built without `RTL8735B_WIFI_CONFIG`, and overlays and settings files with the credentials removed.

A problem that also affects ArduPilot on other platforms belongs to the ArduPilot project as well; a problem in the Realtek SDK belongs to Realtek.

## Known limitations

These are documented behaviors of the port, listed in the security limits of [Wi-Fi](/libraries/AP_HAL_RTL8735B/docs/wifi.md#security-limits):

- MAVLink over Wi-Fi has no authentication. Over UDP, while there is no ground station, or after it has sent nothing for more than 3 seconds, any source that sends to the board's port is taken as the new ground station, and its RC override is accepted. Over TCP, once the ground station has sent nothing for more than 3 seconds, a new connection from any host on the subnet replaces it.
- With no ground station, the HEARTBEAT is broadcast to the whole subnet.
- The Wi-Fi credentials are compiled into the image as a constant array; anyone who has the image may be able to read them.

A way to go beyond these, for example reading the credentials without the image file, or affecting the board from outside the subnet, is reported privately as above. A request to change one of these behaviors, for example to add authentication, is a public [issue](https://github.com/rainwu0/ArduPilot-RTL8735B/issues).
