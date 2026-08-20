# 3DS client

Homebrew `.3dsx` (Homebrew Launcher) or `.cia` (HOME Menu via FBI). Needs **CFW**.

These steps assume **Debian/Ubuntu or WSL2** (same commands). Other OS: [devkitPro pacman wiki](https://devkitpro.org/wiki/devkitPro_pacman).

## 1. Tools

```bash
sudo apt update
sudo apt install -y wget curl make openssl ca-certificates python3
```

On **WSL**, create this symlink if `/etc/mtab` is missing (devkitPro’s installer needs it):

```bash
sudo ln -s /proc/self/mounts /etc/mtab
```

## 2. Install devkitPro pacman

Use the official script. The `-U "dkp-apt"` user-agent is required; without it the download may be blocked.

```bash
wget -U "dkp-apt" https://apt.devkitpro.org/install-devkitpro-pacman
chmod +x ./install-devkitpro-pacman
sudo ./install-devkitpro-pacman
```

That adds the apt repo, installs `devkitpro-pacman`, and puts the toolchain under `/opt/devkitpro`.

## 3. Install the 3DS toolchain and libraries

```bash
sudo dkp-pacman -Syu --noconfirm
sudo dkp-pacman -S --noconfirm 3ds-dev 3ds-curl
```

- `3ds-dev` — **devkitARM**, **libctru**, **citro2d**, **citro3d**
- `3ds-curl` — **libcurl** + **mbedtls** (HTTPS)

## 4. Load environment variables

```bash
source /etc/profile.d/devkit-env.sh
echo "$DEVKITARM"   # expect /opt/devkitpro/devkitARM
```

If `DEVKITARM` is empty in a **new** terminal, add this to `~/.bashrc` and open a new shell:

```bash
echo 'source /etc/profile.d/devkit-env.sh' >> ~/.bashrc
source ~/.bashrc
```

## 5. Get `cacert.pem` from Nginx Proxy Manager

The 3DS has no system CA store. The app loads `romfs:/cacert.pem` and uses it as `CURLOPT_CAINFO`. You must ship the CA that signed the cert **NPM presents on 443**.

`romfs/cacert.pem.example` is a placeholder. Replace it with a real PEM **before** `make`.

Work from the **repo root**. Set your public NPM hostname:

```bash
cd /path/to/Pockettransfer
HOST=bank.saltbox.cc    # change if your NPM domain is different
```

### 5a. Dump the chain NPM serves (do this)

This talks to NPM over HTTPS and saves every certificate in the handshake (leaf + Let’s Encrypt intermediate):

```bash
echo Q | openssl s_client -connect "${HOST}:443" -servername "$HOST" -showcerts 2>/dev/null \
  | awk '/-----BEGIN CERTIFICATE-----/,/-----END CERTIFICATE-----/' \
  > /tmp/npm-certs.pem

grep -c "BEGIN CERTIFICATE" /tmp/npm-certs.pem
# 2–4 is normal (leaf + intermediates; new Let's Encrypt chains can be 4).
# 0 means TLS failed — fix NPM/DNS first.
```

Let’s Encrypt does **not** send the root in the handshake. Append **ISRG Root X1** (the root NPM’s LE certs chain to):

```bash
{
  cat /tmp/npm-certs.pem
  curl -fsSL https://letsencrypt.org/certs/isrgrootx1.pem
} > clients/3ds/romfs/cacert.pem
```

Check that PC curl trusts your site **with that same file** (same check the 3DS will do):

```bash
curl --cacert clients/3ds/romfs/cacert.pem -sf "https://${HOST}/health" && echo OK
```

If this fails, the 3DS will fail too. Confirm NPM is serving TLS 1.2 (not TLS 1.3-only). See [deploy/nginx-proxy-manager.md](../../deploy/nginx-proxy-manager.md).

### 5b. Optional: copy files off the NPM host

Use this if `openssl s_client` from your PC cannot reach the box (hairpin NAT, etc.). SSH into the **NPM** machine.

**Docker NPM** (container name may differ):

```bash
docker ps | grep -i nginx
# then, try:
docker exec -it CONTAINER_NAME ls /etc/letsencrypt/live
docker exec -it CONTAINER_NAME cat /etc/letsencrypt/live/npm-1/chain.pem
# copy that PEM to your PC as clients/3ds/romfs/cacert.pem
# still append ISRG Root X1 as in 5a
```

Paths vary (`npm-1`, `npm-2`, …). `chain.pem` is the intermediate; `fullchain.pem` is leaf + intermediate. Prefer `chain.pem` + ISRG Root X1.

**Native NPM on Debian:** search Let’s Encrypt live dirs:

```bash
sudo find / -name 'chain.pem' 2>/dev/null | grep letsencrypt
```

### Custom (non–Let’s Encrypt) certs in NPM

Skip ISRG Root X1. Put the **CA that signed your NPM cert** into `clients/3ds/romfs/cacert.pem`. If the cert is self-signed, put that certificate itself. Then re-run the `curl --cacert … /health` check.

## 6. Build

```bash
cd clients/3ds
source /etc/profile.d/devkit-env.sh
make
make cia
```

`make cia` also builds the `.3dsx`. First CIA build downloads **makerom** and **bannertool** into `cia/tools/` (gitignored).

Outputs (this folder):

| File | Role |
|------|------|
| `pockettransfer.3dsx` | Homebrew Launcher app |
| `pockettransfer.cia` | HOME Menu install (FBI) |
| `pockettransfer.elf` | Debug symbols |
| `pockettransfer.smdh` | Homebrew menu icon/metadata |

Title ID: `000400000A054E00` (UniqueId `0xA054E`).

Rebuild from scratch:

```bash
make clean && make cia
```

## 7. Install on the 3DS

**Homebrew Launcher** — copy `pockettransfer.3dsx` to the SD card, for example:

```
sdmc:/3ds/pockettransfer.3dsx
```

**HOME Menu** — copy `pockettransfer.cia` to the SD card and install it with **FBI** (or similar). Delete the old title first if you reinstall after a UniqueId change.

From WSL, if the SD card is `E:` on Windows:

```bash
cp pockettransfer.3dsx /mnt/e/3ds/pockettransfer.3dsx
cp pockettransfer.cia /mnt/e/cias/pockettransfer.cia
```

Launch from the Homebrew Launcher or HOME Menu. First run creates `sdmc:/3ds/pockettransfer/`.

## 8. Account (on the 3DS)

Host is always `https://bank.saltbox.cc`.

On first launch the app opens **Create account** / **Log in**. The 3DS software keyboard asks for username and password. Register and login both return a device token; the app writes it to `sdmc:/3ds/pockettransfer/config.json`. No pairing code needed.

Username: 3–32 characters, lowercase letters, numbers, underscore. Password: 8+ characters.

Use **Account** in the main menu to switch users. **Pair from website** remains as a fallback.

TLS notes: [shared/tls.md](../../shared/tls.md).

## 9. Debug log

The app appends to **`sdmc:/pockettransfer.log`** (SD card root). Pull the SD card or use FTPD and open that file after a failure. It records boot, HTTP/TLS errors, and save-mount results. Passwords are not written. If the file grows past 512 KB it is truncated on the next launch.
