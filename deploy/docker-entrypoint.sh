#!/bin/sh
set -e

# Docker volume at /app/Data is root-owned on first mount; app runs as APP_UID.
mkdir -p /app/Data/keys
chown -R "${APP_UID}:${APP_UID}" /app/Data

exec gosu "${APP_UID}:${APP_UID}" dotnet Pockettransfer.Server.dll "$@"
