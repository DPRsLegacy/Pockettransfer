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

## 5. TLS cert (`romfs/cacert.pem`)

The 3DS has no system CA store. `make` runs `tools/fetch_cacert.py`, which dumps the live chain from **pockettransfer.net** and appends **GTS Root R4**. That site uses Google Trust Services (WE1), not Let's Encrypt, so ISRG Root X1 will not work.

```bash
cd clients/3ds
python3 tools/fetch_cacert.py -o romfs/cacert.pem
curl --cacert romfs/cacert.pem -sf "https://pockettransfer.net/health" && echo OK
```

## 6. Build

First build downloads Gen 7 PC box icons (once) into `tools/sprite-cache/` and packs `romfs/pkm_icons.t3x`. Needs network that first time.

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

Host is always `https://pockettransfer.net`.

On first launch the app opens **Create account** / **Log in**. The 3DS software keyboard asks for username and password. Register and login both return a device token; the app writes it to `sdmc:/3ds/pockettransfer/config.json`. No pairing code needed.

Username: 3–32 characters, lowercase letters, numbers, underscore. Password: 8+ characters.

Use **Account** in the main menu to switch users. **Pair from website** remains as a fallback.

TLS notes: [shared/tls.md](../../shared/tls.md).

## 9. Debug log

The app appends to **`sdmc:/pockettransfer.log`** (SD card root). Pull the SD card or use FTPD and open that file after a failure. It records boot, HTTP/TLS errors, and save-mount results. Passwords are not written. If the file grows past 512 KB it is truncated on the next launch.
