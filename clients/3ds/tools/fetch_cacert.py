#!/usr/bin/env python3
"""Build romfs/cacert.pem from the live HTTPS chain plus Google Trust Services roots.

pockettransfer.net is served with Google Trust Services (WE1 / GTS Root R4),
not Let's Encrypt. ISRG Root X1 will not verify that chain on 3DS mbedtls.
"""
from __future__ import annotations

import argparse
import ssl
import subprocess
import sys
import urllib.request
from pathlib import Path

HOST_DEFAULT = "pockettransfer.net"
GTS_R4 = "https://pki.goog/repo/certs/gtsr4.pem"
GS_R1 = "https://secure.globalsign.com/cacert/root-r1.crt"
UA = "PocketTransferCacert/1.0"


def fetch_url(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=30) as resp:
        return resp.read()


def handshake_certs(host: str) -> list[str]:
    proc = subprocess.run(
        ["openssl", "s_client", "-connect", f"{host}:443", "-servername", host, "-showcerts"],
        input=b"Q\n",
        capture_output=True,
        check=False,
    )
    text = proc.stdout.decode("utf-8", "replace")
    certs: list[str] = []
    cur: list[str] = []
    inside = False
    for line in text.splitlines():
        if "BEGIN CERTIFICATE" in line:
            inside = True
            cur = [line]
        elif "END CERTIFICATE" in line and inside:
            cur.append(line)
            certs.append("\n".join(cur) + "\n")
            inside = False
        elif inside:
            cur.append(line)
    if not certs:
        raise SystemExit(
            f"no certificates from {host}:443 (openssl exit {proc.returncode})\n"
            f"{proc.stderr.decode('utf-8', 'replace')[:400]}"
        )
    return certs


def pem_blocks(blob: bytes) -> list[str]:
    text = blob.decode("utf-8", "replace")
    certs: list[str] = []
    cur: list[str] = []
    inside = False
    for line in text.splitlines():
        if "BEGIN CERTIFICATE" in line:
            inside = True
            cur = [line]
        elif "END CERTIFICATE" in line and inside:
            cur.append(line)
            certs.append("\n".join(cur) + "\n")
            inside = False
        elif inside:
            cur.append(line)
    return certs


def openssl_field(pem: str, flag: str) -> str:
    proc = subprocess.run(
        ["openssl", "x509", "-noout", flag],
        input=pem,
        capture_output=True,
        text=True,
        check=False,
    )
    return proc.stdout.strip()


def as_pem(blob: bytes) -> str:
    text = blob.decode("utf-8", "replace")
    if "BEGIN CERTIFICATE" in text:
        blocks = pem_blocks(blob)
        return "".join(blocks)
    proc = subprocess.run(
        ["openssl", "x509", "-inform", "DER", "-outform", "PEM"],
        input=blob,
        capture_output=True,
        check=True,
    )
    return proc.stdout.decode("utf-8")


def summarize(pem: str) -> str:
    return f"{openssl_field(pem, '-subject')} {openssl_field(pem, '-issuer')}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=HOST_DEFAULT)
    ap.add_argument("-o", "--output", type=Path, required=True)
    args = ap.parse_args()

    parts: list[str] = []
    chain = handshake_certs(args.host)
    print(f"live chain from {args.host}: {len(chain)} certs")
    for i, pem in enumerate(chain):
        subj = openssl_field(pem, "-subject")
        iss = openssl_field(pem, "-issuer")
        print(f"  [{i}] {subj} {iss}")
        if i == 0:
            continue
        # Skip Google's GlobalSign-cross-signed R4; the self-signed root is added below.
        if "GTS Root R4" in subj and subj != iss:
            continue
        parts.append(pem)

    for url, label in ((GTS_R4, "GTS Root R4"), (GS_R1, "GlobalSign Root CA")):
        pems = pem_blocks(as_pem(fetch_url(url)).encode())
        if not pems:
            raise SystemExit(f"no PEM in {url}")
        print(f"{label}:", summarize(pems[0]))
        parts.extend(pems)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    body = "".join(parts)
    if not body.endswith("\n"):
        body += "\n"
    args.output.write_text(body)
    print("wrote", args.output, "bytes", args.output.stat().st_size)

    # Confirm the host verifies against the bundle we just wrote.
    ctx = ssl.create_default_context(cafile=str(args.output))
    socket_connect(args.host, ctx)
    print("verify ok")
    return 0


def socket_connect(host: str, ctx: ssl.SSLContext):
    import socket

    raw = socket.create_connection((host, 443), timeout=20)
    sock = ctx.wrap_socket(raw, server_hostname=host)
    sock.close()
    return True


if __name__ == "__main__":
    raise SystemExit(main())
