# HTTPS for Pockettransfer

Consoles need a public **HTTPS** origin. This project supports:

| Method | When to use |
|--------|-------------|
| **[Nginx Proxy Manager](nginx-proxy-manager.md)** | **Recommended** — e.g. `pockettransfer.net` on NPM, app on `:8080` |
| **Built-in Caddy** | `docker compose --profile tls` if you are not using NPM |

## Nginx Proxy Manager (pockettransfer.net)

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

The 3DS client loads `romfs:/cacert.pem`. `make` runs `clients/3ds/tools/fetch_cacert.py`, which stores the live chain from **pockettransfer.net** plus **GTS Root R4** (Google Trust Services / WE1). Do not use ISRG Root X1 for that hostname.

Switch `switch-curl` typically uses system CAs; you can still ship `cacert.pem` in romfs.

## Device auth

Consoles send `Authorization: Bearer pt_...` or `X-Device-Token`. Create an account or log in on the console; it stores the token. Pairing codes on **Devices** are optional.

## Forwarded headers

The ASP.NET app trusts `X-Forwarded-For` and `X-Forwarded-Proto` from NPM so cookies and redirects work behind HTTPS.
