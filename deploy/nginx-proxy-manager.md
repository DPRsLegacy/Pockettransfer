# Nginx Proxy Manager + bank.saltbox.cc

Use **Nginx Proxy Manager (NPM)** for HTTPS in front of Pockettransfer. Do **not** start the built-in Caddy container (`--profile tls`) when NPM handles TLS.

## Architecture notes

NPM may run **natively on Debian** in one LXC, with Pockettransfer in **Docker** on the same or another LXC.

| NPM location | Pockettransfer location | Forward hostname | Forward port |
|--------------|-------------------------|------------------|--------------|
| Debian LXC (native) | **Same** LXC, Docker | `127.0.0.1` | `8080` |
| Debian LXC (native) | **Different** Docker LXC | Docker LXC **LAN IP** | `8080` |
| Docker container | Docker (same host) | `172.17.0.1` or container name on shared network | `8080` |

Native NPM on Debian uses the **host network** — `127.0.0.1:8080` works when Pockettransfer publishes `8080` on that same machine. It does **not** apply when NPM is inside Docker.

**Verify from the NPM LXC shell** (the same machine where NPM runs):

```bash
curl -sf http://127.0.0.1:8080/health && echo OK    # same LXC
# or
curl -sf http://DOCKER_LXC_IP:8080/health && echo OK  # different LXC
```

Use whichever IP works here in NPM → Proxy Host → Details.


```bash
./deploy/proxmox/up.sh    # postgres + web on :8080, no Caddy
```

## 1. DNS

Create an **A record** (or CNAME):

| Name | Value |
|------|--------|
| `bank.saltbox.cc` | Public IP that reaches your **NPM** host |

For testing behind NAT, use your public IP and port-forward **80** and **443** to the NPM container/LXC.

## 2. Start Pockettransfer (Docker LXC)

On the Pockettransfer LXC:

```bash
cp .env.example .env
# set POSTGRES_PASSWORD; BANK_DOMAIN=bank.saltbox.cc is for your reference
./deploy/proxmox/up.sh
curl -sf http://127.0.0.1:8080/health && echo OK
```

From the **NPM host**, verify it can reach the app:

```bash
curl -sf http://POCKETTRANSFER_LXC_IP:8080/health && echo OK
```

Allow **TCP 8080** from the NPM LXC/IP to the Pockettransfer LXC (Proxmox firewall or internal LAN only).

## 3. NPM proxy host

In Nginx Proxy Manager → **Hosts** → **Proxy Hosts** → **Add Proxy Host**. Your UI has **Details**, **Custom locations**, **SSL**, and a **Settings (gear)** for custom nginx config.

### Details

| Field | Value |
|-------|--------|
| Domain names | `bank.saltbox.cc` |
| Scheme | **`http`** ← must not be `https` |
| Forward hostname / IP | Pockettransfer LXC IP (e.g. `192.168.1.210`) |
| Forward port | `8080` |
| Cache assets | off |
| Block common exploits | on |
| Websockets support | on (harmless; helps if you add live UI later) |

### Custom locations

Leave empty. Do **not** add a location here unless you need a special path. The Details tab already forwards the whole site to `:8080`.

### SSL

- SSL certificate: **Request a new SSL Certificate**
- Force SSL: on
- HTTP/2: on
- HSTS: optional for a test domain

### Settings gear → Custom Nginx Configuration (TLS 1.2)

Open the **gear / Settings** on this proxy host (not Custom locations). Paste this into **Custom Nginx Configuration**:

```nginx
# 3DS homebrew (3ds-curl / mbedtls) needs TLS 1.2, not TLS 1.3-only.
ssl_protocols TLSv1.2 TLSv1.3;
ssl_ciphers ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305;
ssl_prefer_server_ciphers off;
client_max_body_size 10m;
proxy_read_timeout 120s;
proxy_send_timeout 120s;
```

If NPM rejects `ssl_protocols` in that box (some versions only allow `location` snippets), leave the box empty. NPM’s default SSL already includes TLS 1.2; just do **not** turn on any “TLS 1.3 only” option.

Save. Open `https://bank.saltbox.cc` and register an account.

## 4. Console config

**3DS** — `sdmc:/3ds/pockettransfer/config.json`:

```json
{
  "host": "https://bank.saltbox.cc",
  "token": "pt_your_device_token_after_pairing"
}
```

**Switch** — `sdmc:/switch/pockettransfer/config.json`:

```json
{
  "host": "https://bank.saltbox.cc",
  "token": "pt_your_device_token_after_pairing"
}
```

Pair: web UI → **Devices** → generate code → `pair.txt` on SD → Pair in the homebrew app.

## 5. 3DS CA bundle

Before building the 3DS `.3dsx`, pin Let's Encrypt root:

```bash
curl -fsSL https://letsencrypt.org/certs/isrgrootx1.pem -o clients/3ds/romfs/cacert.pem
```

NPM's Let's Encrypt chain uses ISRG Root X1, which 3DS must trust via that file.

## 6. Troubleshooting

### 502 Bad Gateway

HTTPS works but NPM cannot reach Pockettransfer. The proxy host is fine; the **backend** is down or unreachable.

**Wrong upstream scheme (most common after SSL is fixed):** if logs show:

```text
upstream: "https://192.168.1.210:8080/"
SSL_do_handshake() failed ... wrong version number
```

NPM is using **https** to the backend, but Pockettransfer only speaks **http** on `:8080`. In Proxy Host → **Details**, set **Scheme** to **`http`** (not `https`). Save and reload.

**On the Pockettransfer LXC** (SSH into the Docker LXC):

```bash
cd ~/Pockettransfer   # or wherever you cloned the repo
docker compose ps
docker compose logs web --tail 80
curl -sf http://127.0.0.1:8080/health && echo OK
```

| `docker compose ps` | Action |
|---------------------|--------|
| `web` not running / restarting | `docker compose logs web` — often missing `.env` or bad `POSTGRES_PASSWORD` |
| nothing running | `cp .env.example .env`, set password, `./deploy/proxmox/up.sh` |
| `web` up but curl fails | wait 30s for startup, check logs for DB errors |

**From the NPM host** (must succeed before the website will work):

```bash
curl -sf http://POCKETTRANSFER_LXC_IP:8080/health && echo OK
```

Replace `POCKETTRANSFER_LXC_IP` with the **LAN IP** of the Pockettransfer LXC (e.g. `192.168.1.50`), **not** `127.0.0.1` unless NPM runs on the same LXC.

**NPM Details tab — common mistakes**

| Wrong | Right |
|-------|--------|
| Forward IP = public IP `107.x.x.x` | LAN IP or `127.0.0.1` (if same LXC as Docker) |
| Forward IP = Docker LXC IP but NPM is on **same** LXC | **`127.0.0.1`** |
| Scheme = `https` | Scheme = **`http`** |
| Forward port = `80` or `443` | Forward port = **`8080`** |

**Same LXC (native NPM + Docker Pockettransfer):** your `docker ps` shows `0.0.0.0:8080->8080`. NPM should forward to `http://127.0.0.1:8080`.

**Two LXCs:** run `curl http://DOCKER_LXC_IP:8080/health` from the **NPM LXC** (not from the Docker LXC). Put that IP in NPM.

**Firewall:** allow TCP **8080** from the NPM LXC IP to the Pockettransfer LXC (Proxmox → LXC → Firewall, or host iptables).

**Custom nginx config:** if you added TLS directives and still get 502, remove them temporarily. Use only:

```nginx
client_max_body_size 10m;
proxy_read_timeout 120s;
proxy_send_timeout 120s;
```

### Other issues

| Symptom | Check |
|---------|--------|
| NPM **Default Site** (Congratulations) | Proxy host missing or domain typo in NPM |
| Cert request fails | DNS → NPM; port 80 open for Let's Encrypt |
| Browser works, 3DS SSL fail | `cacert.pem` in 3DS romfs; TLS 1.2 on NPM |
| Login works, API 401 on console | Device paired? Token in `config.json` |
| Register OK but login fails / loops back to login | Rebuild web container (`docker compose up -d --build`). NPM **Scheme** must be `http`. Cookies need HTTPS at the browser — use **Full (strict)** Cloudflare SSL if applicable |
| "Invalid username or password" | Log in with your **username** (lowercase), not email — unless the account was created with the old email form |

## 7. Optional: lock down 8080

Once NPM works, restrict Pockettransfer **8080** to NPM's IP only (Proxmox firewall or `docker-compose` bind):

```yaml
ports:
  - "127.0.0.1:8080:8080"   # only if NPM runs on the same LXC
```

If NPM is on a **different** LXC, keep `8080:8080` but firewall so only the NPM subnet can connect.
