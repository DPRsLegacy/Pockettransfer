# Pockettransfer

Self-hosted Pokémon Bank / HOME-style storage for **modded 3DS and Switch**. PKHeX.Core on the server handles save writes and legality; banked Pokémon live in **PostgreSQL**.

**Demo:** [bank.saltbox.cc](https://bank.saltbox.cc)

## Server: Proxmox Docker LXC + Nginx Proxy Manager

1. **Pockettransfer** runs in the [Docker LXC](https://community-scripts.org/scripts?q=docker&preview=docker) (`./deploy/proxmox/up.sh` → port **8080**).
2. **Nginx Proxy Manager** terminates HTTPS for **`https://bank.saltbox.cc`** and proxies to the LXC.

| Component | Guide |
|-----------|--------|
| Docker LXC deploy | [deploy/proxmox/README.md](deploy/proxmox/README.md) |
| NPM + `bank.saltbox.cc` | **[deploy/nginx-proxy-manager.md](deploy/nginx-proxy-manager.md)** |

Do **not** use `docker compose --profile tls` when NPM handles certificates.

Console `config.json` example: [deploy/config.example.json](deploy/config.example.json)

Health (on LXC): `curl http://127.0.0.1:8080/health` · Public: `https://bank.saltbox.cc/health`

## Local dev (SQLite)

```bash
export PATH="$HOME/.dotnet:$PATH"
dotnet run --project server
```

## Web UI

Open **`https://bank.saltbox.cc`** (or your NPM URL) in a browser on PC or phone:

1. **Register** with a username and password (3–32 chars, lowercase letters, numbers, underscore).
2. **Log in** → **My Bank** to browse boxes, search Pokémon, and view PKHeX legality reports.
3. **Devices** to pair 3DS/Switch · **Saves** to upload a save file from the browser.

## Console clients

CFW required. Host is always `https://bank.saltbox.cc`. First launch: create an account or log in with the console keyboard — the app stores the device token itself.

- 3DS: [clients/3ds](clients/3ds)
- Switch: [clients/switch](clients/switch)

Title IDs: [shared/GAMES.md](shared/GAMES.md) · TLS: [shared/tls.md](shared/tls.md)

## License

Server uses PKHeX.Core (**GPL-3.0-or-later**). See [LICENSE](LICENSE).
