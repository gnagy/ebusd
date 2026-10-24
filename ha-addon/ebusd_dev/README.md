# eBUSd (dev): a local Home Assistant app for the gnagy/ebusd fork

A copy of the [eBUSd app](https://github.com/LukasGrebe/ha-addons/tree/main/ebusd) 26.1.10 by Lukas
Grebe (GPL-3.0, see `LICENSE`), changed only to run this fork's `ebusd` and `ebusctl` from `bin/`:

- `config.yaml`: name, slug `ebusd_dev`, version, amd64 only, and no `image:`, so Supervisor builds it
  on the box as the local app `local_ebusd_dev`.
- `Dockerfile`: copies `bin/ebusd` and `bin/ebusctl` over upstream's.

`../build.sh` fills `bin/` and sets the version, `../install.sh <host>` copies this folder to
`/addons/ebusd_dev/` on a Home Assistant host. Everything else, including `run.sh`, is the original:

---

# Home Assistant App: eBUSd

A transparent wrapper of [ebusd](https://ebusd.eu) for Home Assistant.
Tracks upstream [john30/ebusd](https://github.com/john30/ebusd) releases automatically.

## What this app does

- Runs ebusd as a Home Assistant supervised app (container).
- Exposes ebusd's own command-line options as HA configuration fields.
- Auto-configures the MQTT connection to the HA MQTT broker.
- Seeds `mqtt-hassio.cfg` (MQTT–Home Assistant integration config) into the app config folder on first start.
- Optionally stores every eBUS telegram ebusd receives in an SQLite database.

## Installation

See the [repository readme](https://github.com/LukasGrebe/ha-addons#how-to-install) for how to add this repository to Home Assistant.

## Versioning

This app version mirrors [john30/ebusd releases](https://github.com/john30/ebusd/releases) — `MAJOR.MINOR` matches ebusd, `.PATCH` is reserved for app-specific changes.

For the changelog, see the upstream [ebusd releases page](https://github.com/john30/ebusd/releases).

## Support

**Configuration and device issues** — this app only runs ebusd; CSV message definition files are not managed here.
Please see the official [ebusd project](https://ebusd.eu) and [community](https://github.com/john30/ebusd/discussions).

**Issues specific to running ebusd via HA Supervisor** — use [Issues](https://github.com/LukasGrebe/ha-addons/issues) or [Pull Requests](https://github.com/LukasGrebe/ha-addons/pulls).
