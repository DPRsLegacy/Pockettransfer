# Pockettransfer on Proxmox (Docker LXC)

Run Pockettransfer inside the **Docker LXC** from [Proxmox VE Helper Scripts](https://community-scripts.org/scripts?q=docker&preview=docker). That LXC already has Docker; you only deploy this repo with Compose.

## 1. Create the Docker LXC (once)

On the Proxmox host, use the community **Docker** script from the helper site above. Typical choices:

| Setting | Suggestion |
|---------|------------|
| RAM | **2 GB** minimum (4 GB if you build images on the LXC) |
| CPU | 2 cores |
| Disk | 20 GB+ |
| Features | Script usually enables **nesting** / **keyctl** for Docker |

Note the LXC IP (e.g. `192.168.1.50`).

## 2. Copy the project into the LXC

SSH into the LXC (not the Proxmox host):

```bash
ssh root@192.168.1.50
apt update && apt install -y git
git clone https://github.com/YOUR_USER/Pockettransfer.git
cd Pockettransfer
```

Or `scp -r` the folder from your dev machine.

## 3. Configure environment

```bash
cp .env.example .env
nano .env
```

Set at least:

```env
POSTGRES_PASSWORD=a-long-random-secret
BANK_DOMAIN=bank.saltbox.cc
```

Optional: `PT_ADMIN_USERNAMES=yourname` so that account is always an admin. If unset, the oldest user becomes admin.

## 4. Start the stack

From the repo root on the LXC:

```bash
chmod +x deploy/proxmox/up.sh
./deploy/proxmox/up.sh
```

Check:

```bash
docker compose ps
curl -sf http://127.0.0.1:8080/health && echo OK
```

- **Direct test:** `http://LXC_IP:8080`
- **Public URL (recommended):** [Nginx Proxy Manager](../nginx-proxy-manager.md) → `https://bank.saltbox.cc`

Do **not** use `--profile tls` if NPM handles HTTPS.

## 5. What runs where

```text
Proxmox host
 ├── Docker LXC → Pockettransfer (postgres + web :8080)
 └── NPM LXC/host → https://bank.saltbox.cc → proxy to :8080
```

Optional: built-in Caddy (`--profile tls`) if you are **not** using NPM.

Pokémon bytes live in **PostgreSQL** inside the `postgres` container volume. Saves uploaded from consoles stay in **web container memory** only for the session.

## 6. Console clients

Host is hardcoded to `https://bank.saltbox.cc`. First launch: create an account or log in on the console keyboard; the app stores the device token.

See [../nginx-proxy-manager.md](../nginx-proxy-manager.md).

## 7. Updates

```bash
cd ~/Pockettransfer
git pull
./deploy/proxmox/up.sh
```

## 8. Troubleshooting

| Problem | Fix |
|---------|-----|
| `docker: command not found` | Re-run the [Docker LXC script](https://community-scripts.org/scripts?q=docker&preview=docker) or install Docker inside the LXC. |
| Build OOM during `docker compose build` | Raise LXC RAM to 4 GB, or build on another machine and push to a registry. |
| Can't reach `:8080` from LAN | Proxmox firewall: allow TCP 8080 to the LXC; check `docker compose ps`. |
| Let's Encrypt fails | DNS `bank.saltbox.cc` → NPM; ports 80/443 → NPM (not Pockettransfer LXC). |
| 3DS SSL errors | NPM Advanced: `ssl_protocols TLSv1.2 TLSv1.3`; ISRG Root X1 in `cacert.pem`. |

## 9. Firewall (Proxmox / LXC)

Allow on the LXC (or via Proxmox rules):

- **8080** — reachable from NPM LXC (LAN); not required on public internet if NPM proxies internally
- **80, 443** — on the **NPM** host only (not Pockettransfer LXC)

Keep PostgreSQL **not** published to the host (Compose already keeps it internal).
