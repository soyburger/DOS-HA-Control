#!/bin/sh
# Sets up ha_bridge.py to run as a systemd service on the Raspberry Pi.
# Run this ON THE PI, from inside a clone of this repo:
#   git clone https://github.com/soyburger/DOS-HA-Control.git
#   cd DOS-HA-Control
#   sudo bash bridge/deploy/install.sh
set -e

if [ "$(id -u)" -ne 0 ]; then
    echo "Run this with sudo (it installs a systemd service)." >&2
    exit 1
fi

REAL_USER="${SUDO_USER:-$(logname)}"
REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BRIDGE_DIR="$REPO_DIR/bridge"
VENV_DIR="$BRIDGE_DIR/venv"

echo "Repo:  $REPO_DIR"
echo "User:  $REAL_USER"

apt-get update
apt-get install -y python3-venv python3-pip

if [ ! -d "$VENV_DIR" ]; then
    python3 -m venv "$VENV_DIR"
fi
"$VENV_DIR/bin/pip" install -q -r "$BRIDGE_DIR/requirements.txt"

if [ ! -f "$BRIDGE_DIR/config.ini" ]; then
    cp "$BRIDGE_DIR/config.example.ini" "$BRIDGE_DIR/config.ini"
    echo "*** Created bridge/config.ini from the example -- edit it now with"
    echo "*** your Home Assistant URL/token and your entities before starting"
    echo "*** the service (or the bridge will run but every HA call will fail)."
fi

# Access to /dev/ttyUSB0 without sudo.
usermod -a -G dialout "$REAL_USER"

cat > /etc/systemd/system/ha-bridge.service <<EOF
[Unit]
Description=Home Assistant DOS bridge
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=$REAL_USER
WorkingDirectory=$BRIDGE_DIR
ExecStart=$VENV_DIR/bin/python3 $BRIDGE_DIR/ha_bridge.py $BRIDGE_DIR/config.ini
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable ha-bridge.service

echo ""
echo "Installed. Edit $BRIDGE_DIR/config.ini if you haven't, then:"
echo "  sudo systemctl start ha-bridge"
echo "  sudo systemctl status ha-bridge"
echo "  journalctl -u ha-bridge -f"
echo ""
echo "NOTE: you were just added to the 'dialout' group for serial port"
echo "access -- log out and back in (or reboot) before starting the"
echo "service, or the group change won't take effect yet."
