# HTTPS for Pockettransfer

Consoles need a public **HTTPS** origin. This project supports:

| Method | When to use |
|--------|-------------|
| **[Nginx Proxy Manager](nginx-proxy-manager.md)** | **Recommended** — e.g. `bank.saltbox.cc` on NPM, app on `:8080` |
| **Built-in Caddy** | `docker compose --profile tls` if you are not using NPM |

## Nginx Proxy Manager (bank.saltbox.cc)

Pockettransfer listens on **HTTP port 8080** only. NPM requests Let's Encrypt and proxies to the Docker LXC.

See **[deploy/nginx-proxy-manager.md](../deploy/nginx-proxy-manager.md)**.

## TLS 1.2 for 3DS (`3ds-curl` / mbedtls)

Keep **TLS 1.2** enabled on the proxy. In NPM, open the proxy host **Settings gear → Custom Nginx Configuration**:

```nginx
ssl_protocols TLSv1.2 TLSv1.3;
```

Prefer cipher suites with **AES-GCM** (NPM/OpenSSL defaults are usually fine).

Avoid TLS 1.3-only configs.

## 3DS CA bundle

The 3DS client loads `romfs:/cacert.pem`. NPM/Let's Encrypt chains to **ISRG Root X1**:

```bash
curl -fsSL https://letsencrypt.org/certs/isrgrootx1.pem -o clients/3ds/romfs/cacert.pem
```

Switch `switch-curl` typically uses system CAs; you can still ship `cacert.pem` in romfs.

## Device auth

Consoles send `Authorization: Bearer pt_...` or `X-Device-Token`. Pair from **Devices** on `https://bank.saltbox.cc`.

## Forwarded headers

The ASP.NET app trusts `X-Forwarded-For` and `X-Forwarded-Proto` from NPM so cookies and redirects work behind HTTPS.
