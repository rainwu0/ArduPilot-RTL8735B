# Wi-Fi

[繁體中文](wifi.zh-TW.md)

How the HAL uses the chip's Wi-Fi: the settings file, the MAVLink ports, how UDP sends, the messages it shows, and its security limits. Why some of it works this way is in [Design notes](design.md).

## Settings file

The board joins a hotspot in station (STA) mode. The hotspot name and password come from a local settings file, named at build time by the environment variable `RTL8735B_WIFI_CONFIG`; without it, Wi-Fi is not enabled. Where this goes in a build is in [Build the firmware](building.md).

```text
ssid=your-hotspot-name
password=your-hotspot-password
```

- Everything after the equals sign is used exactly as written: quotes, and spaces around the equals sign or at the end of a line, become part of the name or password. Spaces inside the name are fine.
- A stray quote or space gives no warning at build time. It shows later as a wrong N in `WiFi: joining (SSID length N)`: N is the number of bytes in the name, 1 for each letter or digit and 3 for each Chinese character.
- The password is 8–63 characters, or 64 hexadecimal characters, of printable ASCII only (letters, digits, symbols and spaces). For an open network, `password=` is left empty. A wrong length or character fails the build, which says why.
- The file is read at build time: after changing it, building again is enough, without configuring again.
- The build prints `Wi-Fi STA: 帳密取自 <path>` ("credentials taken from …") when it uses a settings file, and `Wi-Fi STA: 未設定` ("not set") when there is none.
- The credentials are written only into the build directory, and are compiled into the firmware as a constant array: anyone who has an image built with them may be able to read them.

## MAVLink ports

Wi-Fi is SERIAL2 in the board types, with MAVLink2 as its default protocol.

| Transport | Board | Ground station | Chosen by |
|---|---|---|---|
| UDP | Listens on local port 14555. Sends to the ground station that sends to that port; until there is one, broadcasts the HEARTBEAT to port 14550 on the subnet | Listens on port 14550 | Default |
| TCP server | Listens on the port in the setting, for example 5760 | Connects to the board's IP and that port | `define HAL_RTL8735B_WIFI_TCP_PORT 5760` in an overlay |

- TCP serves one ground station at a time.
- The Wi-Fi MAVLink channel is created after the barometer is initialized. Without a barometer, boot stops at `Config Error: Baro: unable to initialise driver`: Wi-Fi still joins the hotspot, but neither UDP nor TCP sends or receives MAVLink ([why](design.md#wi-fi-mavlink-waits-for-the-barometer)).

## How UDP sends

- Several whole MAVLink frames are packed into one datagram of up to 1,472 bytes. When a datagram is not full, the driver waits at most 10 ms, so each message goes out at most about 10 ms later (`WiFiCoalesce.h`). HEARTBEAT broadcasts sent before a ground station is known still go one frame per datagram. TCP is not affected.
- When a send fails because the Wi-Fi driver is out of packet buffers (`ENOBUFS`, `ENOMEM`), that datagram is kept and sent again on the next step, about 1 ms later, for up to 500 ms from the first failure, and only then dropped. Other send failures are dropped at once (`WiFiTxRetry.h`).
- A retry can hold the link for up to 500 ms, and commands and parameter replies queue behind it during that time; the 16 KiB send buffer holds that much MAVLink. If the datagram still cannot be sent after 500 ms, the whole datagram is dropped, which can be a dozen or more messages.

## Messages

The HAL reports the Wi-Fi connection as STATUSTEXT. Before Wi-Fi is up, they reach only a ground station on another link, such as SERIAL0 on the board's USB serial port.

| Message | Meaning |
|---|---|
| `WiFi: joining (SSID length N)` | Joining the hotspot in the settings file; N is the number of bytes in its name |
| `WiFi: up, local <IP>:<port>` | Joined: the board's IP address and local MAVLink port |
| `WiFi: join failed (…)` | Could not join the hotspot; the board retries by itself |
| `WiFi: link lost (…), reconnecting` | The board was connected and lost the hotspot; it joins again by itself |
| `WiFi: no STA credentials in this image; WiFi MAVLink port disabled` | The image was built without a settings file |

## Security limits

- Credentials: compiled into the image, see *Settings file*. Images for sharing, or for attaching to an issue, can be built without `RTL8735B_WIFI_CONFIG`.
- MAVLink over UDP has no authentication. While there is no ground station yet, or after the ground station has sent nothing for more than 3 seconds, any source that sends packets to the local port is taken as the new ground station, including other flight controllers or programs on the same subnet, and its RC override is accepted too. With no ground station, the HEARTBEAT is broadcast to the whole subnet.
- MAVLink over TCP has no authentication either. Once the ground station has sent nothing for more than 3 seconds, a new connection from any host on the subnet replaces it.
- On a network that only you control, no other device can reach the board's MAVLink port. How to report a security problem is in [SECURITY](../../../.github/SECURITY.md).
