# Nginx Proxy Manager + bank.saltbox.cc

Use **Nginx Proxy Manager (NPM)** for HTTPS in front of Pockettransfer. Do **not** start the built-in Caddy container (`--profile tls`) when NPM handles TLS.

## Architecture

```text
Internet → NPM (443, bank.saltbox.cc) → http://POCKETTRANSFER_LXC_IP:8080 → web container
                ↓
         Let's Encrypt cert
```

Pockettransfer LXC runs only:

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

In Nginx Proxy Manager → **Hosts** → **Proxy Hosts** → **Add Proxy Host**:

| Field | Value |
|-------|--------|
| Domain names | `bank.saltbox.cc` |
| Scheme | `http` |
| Forward hostname / IP | Pockettransfer LXC IP (e.g. `192.168.1.50`) |
| Forward port | `8080` |
| Cache assets | off |
| Block common exploits | on |
| Websockets support | on (harmless; helps if you add live UI later) |

**SSL** tab:

- SSL certificate: **Request a new SSL Certificate**
- Force SSL: on
- HTTP/2: on
- HSTS: optional for test domain

**Advanced** tab (3DS needs TLS 1.2 — paste if your NPM build allows custom config):

```nginx
ssl_protocols TLSv1.2 TLSv1.3;
```

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

| Symptom | Check |
|---------|--------|
| NPM **502 Bad Gateway** | Pockettransfer up? `curl http://LXC_IP:8080/health` from NPM host. |
| Cert request fails | DNS for `bank.saltbox.cc` points to NPM; ports 80/443 reach NPM. |
| Browser works, 3DS SSL fail | TLS 1.2 enabled in NPM Advanced; `cacert.pem` in 3DS romfs. |
| Login works, API 401 on console | Device paired? Token in `config.json` matches **Devices** list. |
| Wrong links / mixed content | NPM sends `X-Forwarded-Proto`; Pockettransfer already trusts forwarded headers. |

## 7. Optional: lock down 8080

Once NPM works, restrict Pockettransfer **8080** to NPM's IP only (Proxmox firewall or `docker-compose` bind):

```yaml
ports:
  - "127.0.0.1:8080:8080"   # only if NPM runs on the same LXC
```

If NPM is on a **different** LXC, keep `8080:8080` but firewall so only the NPM subnet can connect.
