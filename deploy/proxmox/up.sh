#!/usr/bin/env bash
# Start Pockettransfer via Docker Compose (Proxmox Docker LXC).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

if [[ ! -f .env ]]; then
  cp .env.example .env
  echo "Created .env from .env.example — edit POSTGRES_PASSWORD before production use."
fi

if ! docker info >/dev/null 2>&1; then
  echo "Docker is not running. Use the Proxmox Docker LXC from community-scripts.org" >&2
  exit 1
fi

exec docker compose up -d --build "$@"
