# Security

## Reporting issues

If you discover a vulnerability, please open a private security advisory on GitHub or contact the maintainers. Do not file public issues for unfixed exploit details.

## Credentials and TLS

- **Bundled dev TLS** in the repo is self-signed and **not secret**; treat it like a shared lab key and replace it (JSON on LittleFS, authenticated `/api/tls/config`, or your own PKI) before any sensitive deployment. (`/api/https/config` is a legacy alias.)
- Never commit **production** TOTP secrets or **production** private keys.
- **Provisioning** on the setup hotspot: the UI sets **device time from your browser** (`/api/totp/time/provision`). **Only `/api/wifi/config`** is readable/writable there **without** a session; all other management APIs still require **TOTP session** (same trust model as LAN for policy/TLS/audit).
- **Setup hotspot** uses the same **HTTPS** UI (self-signed **`https://<ap-ip>/`**). After **STA** succeeds, use **TOTP + session** as usual; **Wi-Fi changes on LAN** also require that session.
- **Wi-Fi PSK** is stored in **plaintext** in `config.json` on the device (same sensitivity as the TLS private key there). Prefer physical access control and LittleFS as your trust boundary; rotate the Wi-Fi password if the filesystem is exposed.

## Hardware and line safety

Telephone lines and FXS ports can carry hazardous voltages. Follow qualified guidance for isolation, protection, and compliance in your jurisdiction. This software does not substitute for proper hardware design review.
