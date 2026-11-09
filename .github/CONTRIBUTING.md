# Contributing

[繁體中文](CONTRIBUTING.zh-TW.md)

Issues and pull requests are welcome here, in Chinese or English. This repository is not the ArduPilot project; problems with the RTL8735B port are handled here.

## Reporting problems

When you open an issue, pick the matching template and include as much as you can of:

- the board and the board type (`--board`), and your overlay if you use one;
- the commit of this repository (`git rev-parse --short HEAD`);
- the build command, and the part of the output where it went wrong, or the messages from the ground station or the boot messages on the USB serial port.

An image built with a Wi-Fi settings file contains the credentials, and anyone who has it may be able to read them. Images attached to an issue are built without `RTL8735B_WIFI_CONFIG`, and overlays and settings files are attached with the credentials removed.

"I followed the documentation and it worked on my board" is worth reporting too; that is how the feature status gets updated.

Security problems, for example something that lets others control the flight controller or read the credentials, are reported privately as described in [SECURITY](SECURITY.md).

## Sending pull requests

- Follow ArduPilot's conventions: each commit touches only one subsystem (for example `AP_HAL_RTL8735B:`, `hwdef:`, `waf:`, `Tools:`), and the commit message is in English, in the form `Subsystem: summary`.
- Run the tests before sending. If you changed the HAL or anything build-related, add `--build` to also cross-build once (this needs the SDK, see [Build the firmware](/libraries/AP_HAL_RTL8735B/docs/building.md)):

  ```bash
  Tools/scripts/rtl8735b_test.sh
  Tools/scripts/rtl8735b_test.sh --build
  ```

  PRs run the same host tests and the commit convention check on GitHub Actions; for PRs from outside contributors they start only after the maintainer approves, and the cross build does not run automatically. When you fix a bug, first write a test that fails before the fix; see `libraries/AP_HAL_RTL8735B/tests/README.md` (in Chinese).
- In the PR, say which board you tried it on and how; if you only built it, write "build only".
- The scope is the HAL plus what this chip needs to fit into ArduPilot; features for a particular vehicle belong in your own project.
- Keep changes to ArduPilot's shared code (outside the HAL) as small as possible, and list each one in *Changes to ArduPilot shared code* in the [HAL README](/libraries/AP_HAL_RTL8735B/README.md).
- When code is adapted from another public source, name the source and a fixed commit in the file header.

## Documentation

- Documents that come in two languages have the English version under the default file name and the Traditional Chinese (Taiwan) version under `.zh-TW.md`, with the same structure and content; each links to the other at the top. When you change one, change the other in the same commit.
- Code comments, `BOARD_TESTS.md` and `tests/README.md` are in Traditional Chinese (Taiwan).
- Where things go, under `libraries/AP_HAL_RTL8735B/`:
  - facts about one board: `hwdef/<board type>/README.md`, in the sections of an ArduPilot board README;
  - how to do something: `docs/building.md`, `docs/flashing.md`, `docs/custom-boards.md`;
  - settings and behavior: the reference documents in `docs/`;
  - why the port works as it does: `docs/design.md`;
  - the first run for a new user: `GETTING_STARTED.md`.
- A row in `docs/feature-status.md` becomes "Verified on a board" only with a record in `BOARD_TESTS.md`.
- For changes users can see (behavior, overlay syntax, parameters, build or flashing steps, document names), add an entry under `[Unreleased]` in `libraries/AP_HAL_RTL8735B/CHANGELOG.md` and `CHANGELOG.zh-TW.md`, in the Keep a Changelog sections (Added, Changed, Fixed, Removed and so on).
