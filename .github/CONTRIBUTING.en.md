# Contributing

繁體中文：[CONTRIBUTING.md](CONTRIBUTING.md)

Issues and pull requests are welcome here, in Chinese or English. This is not the ArduPilot project; please do not take RTL8735B issues to ArduPilot.

## Reporting problems

When you open an issue, pick the matching template and include as much as you can of:

- the board and the board type (`--board`), and your overlay if you use one (with the credentials removed);
- the commit of this repository (`git rev-parse --short HEAD`);
- the build command, and the part of the output where it went wrong, or the boot messages from the LOG UART.

**Do not attach images or settings files that contain Wi-Fi credentials.** The credentials are compiled into the firmware, so anyone who has the image may be able to read them.

"I followed the README and it worked on my board" is worth reporting too; that is how the feature status table gets updated.

Do not open a public issue for security problems (for example something that lets others control the flight controller or read the credentials). Report them privately with "Report a vulnerability" on this repository's Security tab.

## Sending pull requests

- Follow ArduPilot's conventions: each commit touches only one subsystem (for example `AP_HAL_RTL8735B:`, `hwdef:`, `waf:`, `Tools:`), and the commit message is in English, in the form `Subsystem: summary`.
- The home page, CONTRIBUTING, the HAL README and Getting started come in Traditional Chinese (Taiwan) and English: when you change one version, change the other in the same commit. Other READMEs and code comments are in Traditional Chinese (Taiwan).
- Run the tests before sending; if you changed the HAL or anything build-related, add `--build` to also cross-build once (this needs the SDK, see *Getting and building* in the HAL README):

  ```bash
  Tools/scripts/rtl8735b_test.sh
  Tools/scripts/rtl8735b_test.sh --build
  ```

  PRs run the same host tests and the commit convention check on GitHub Actions; for PRs from outside contributors they start only after the maintainer approves, and the cross build does not run automatically. When you fix a bug, first write a test that fails before the fix; see `libraries/AP_HAL_RTL8735B/tests/README.md` (in Chinese).

- In the PR, say clearly which board you tried it on and how; if you only built it, just write "build only".
- The scope is the HAL plus what this chip needs to fit into ArduPilot; features for a particular vehicle belong in your own project.
- Keep changes to ArduPilot's shared code (outside the HAL) as small as possible; if you have to change it, list the change in the HAL README section *Changes to ArduPilot shared code*.
- When code is adapted from another public source, name the source and a fixed commit in the file header.
