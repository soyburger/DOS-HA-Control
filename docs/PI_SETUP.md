# Setting up the Raspberry Pi Zero W as the bridge

## 1. Flash the SD card

Use the official **Raspberry Pi Imager** (https://www.raspberrypi.com/software/)
on your Mac.

1. Choose OS: **Raspberry Pi OS Lite (64-bit)** — no desktop needed, this
   runs headless.
2. Choose storage: your microSD card.
3. Click the **gear icon** (⚙) / "Edit Settings" before writing — this is
   the important part, it lets the Pi boot straight onto your WiFi with
   SSH already on, no monitor/keyboard needed:
   - Set hostname, e.g. `ha-bridge`
   - Enable SSH, "Use password authentication" (or your public key if you
     have one set up)
   - Set a username/password
   - Configure your WiFi SSID/password and correct WiFi country
4. Write the image.

## 2. First boot

Put the card in the Zero W, power it on, and give it a minute or two to
boot and join your WiFi. Then from your Mac:

```bash
ssh <username>@ha-bridge.local
```

If `.local` mDNS resolution doesn't work on your network, find its IP from
your router's DHCP client list instead and `ssh <username>@<ip>`.

## 3. Wire up the serial adapter

With the Pi powered off (safest), connect: Pi's **USB** micro-USB port
(not "PWR IN") → OTG adapter → your USB hub → the Prolific USB-serial
adapter → null-modem cable → the 486's COM port. Power the Pi back on.

Confirm the adapter is recognized:

```bash
dmesg | grep -i pl2303
ls /dev/ttyUSB*
```

You should see `/dev/ttyUSB0`.

## 4. Get the code onto the Pi and install the bridge as a service

```bash
git clone https://github.com/soyburger/DOS-HA-Control.git
cd DOS-HA-Control
sudo bash bridge/deploy/install.sh
```

This creates a Python venv, installs dependencies, copies
`config.example.ini` to `config.ini` if it doesn't exist yet, adds you to
the `dialout` group (needed for serial port access), and installs+enables
a systemd service (`ha-bridge`) that starts the bridge on boot.

## 5. Configure it

Edit `bridge/config.ini`:

```ini
[home_assistant]
base_url = http://<your-ha-ip-or-hostname>:8123
token = <your long-lived access token>

[tcp]
enabled = false

[serial]
enabled = true
port = /dev/ttyUSB0
baud = 9600

[entities]
light.kitchen = Kitchen Light
switch.office_fan = Office Fan
```

Get a long-lived access token from Home Assistant: your profile page (click
your name, bottom left) → Security tab → "Long-Lived Access Tokens" →
Create Token.

## 6. Start it

The `dialout` group membership from step 4 needs a fresh login to take
effect, so reboot once:

```bash
sudo reboot
```

Then, after it comes back up:

```bash
sudo systemctl start ha-bridge
sudo systemctl status ha-bridge
journalctl -u ha-bridge -f
```

You should see it log that it's listening on the serial port. Leave the
`journalctl -f` running and, on the DOS side, launch `HACLIENT.EXE` — you
should see the bridge log the incoming `PING`/`LIST` commands in real time.

## Troubleshooting

- **No `/dev/ttyUSB0`**: check `dmesg | tail -30` right after plugging the
  adapter in — look for USB enumeration errors. Try a different port on
  the hub.
- **Permission denied opening the serial port**: you're not in the
  `dialout` group yet, or haven't logged back in since being added to it
  (reboot fixes this).
- **Bridge starts but every command errors**: almost always a bad
  `base_url` or `token` in `config.ini` — check `journalctl -u ha-bridge`
  for the specific HTTP error.
- **DOS client times out on PING**: double check the null-modem cable is
  actually crossed (not a straight-through serial cable), and that `BAUD=`
  in `HACONFIG.INI` matches `baud =` in `config.ini` (both default to 9600).
