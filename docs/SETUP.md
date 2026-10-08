# PS5 Tailscale v1.0.3 Beta — Easy Setup Guide

PS5 Tailscale gives your jailbroken PS5 its own private Tailscale IP address, usually something like:

`100.x.x.x`

You can then use that address with Remote Play apps such as **Chiaki-ng** or **PXPlay / PSPlay** without setting up normal router port forwarding.

> **Beta notice:** PS5 Tailscale is still experimental homebrew. Normal use and Remote Play are working, but long Rest Mode sessions can still behave differently between consoles. If Rest Mode stops working, the release includes tools that can collect logs for troubleshooting.

---

## What You Need

Before you start, make sure you have:

- A jailbroken PS5 that can run ELF payloads.
- `ps5tailscale-v1.0.3.elf` from the v1.0.3 Beta release.
- A working ELF loader or Payload Manager on the PS5.
- A free or paid Tailscale account.
- A second device such as a phone, Steam Deck, Windows PC, Linux PC, or Mac.
- Tailscale installed on that second device.
- Chiaki-ng or PXPlay / PSPlay if you want to use Remote Play.

You do **not** need traditional router port forwarding.

You also do **not** need a Tailscale subnet router just to connect to PS5 Tailscale.

---

## Step 1 — Enable Remote Play on the PS5

On your PS5, go to:

**Settings → System → Remote Play**

Turn on:

**Enable Remote Play**

If you also want to use Rest Mode, go to:

**Settings → System → Power Saving → Features Available in Rest Mode**

Turn on:

- **Stay Connected to the Internet**
- **Enable Turning On PS5 from Network**

---

## Step 2 — Start PS5 Tailscale

1. Start your PS5 jailbreak.
2. Start your ELF loader or Payload Manager.
3. Run:

   `ps5tailscale-v1.0.3.elf`

4. Wait for PS5 Tailscale to start.

The first launch also installs or repairs the **Tailscale** shortcut in the PS5 **Media** section.

After that, you can open:

**Media → Tailscale**

to view the PS5 Tailscale page.

> Run the PS5 Tailscale ELF only once per PS5 boot. Do not start another copy while it is already running. If you need to restart it, reboot the PS5 first.

---

## Step 3 — Create a Tailscale Auth Key

On your phone or computer:

1. Sign in to your Tailscale account.
2. Open the Tailscale Admin Console.
3. Go to the Settings > **Keys / Auth Keys**.
4. Create a new auth key.
5. Copy the key.

The key normally starts with something similar to:

`tskey-auth-...`

Keep this key private. Do not post it publicly or send it to anyone.

---

## Step 4 — Register the PS5 with Tailscale

Open the **PS5 Tailscale** page.

You can open it from:

**Media → Tailscale**

or from another device on the same home network using:

`http://YOUR-PS5-LAN-IP:8085/`

Use this page only from a trusted home network. The dashboard uses plain HTTP, so do not expose port `8085` to the public internet and do not enter an auth key over an untrusted network.

For example:

`http://192.168.0.123:8085/`

Your own PS5 LAN address may be different.

On the registration page:

1. Paste your Tailscale auth key.
2. Choose **Save & Register** or the registration option shown on the page.
3. Wait until the page says **Connected**.

After the PS5 is connected and appears correctly in your Tailscale account, you can use **Remove Saved Auth Key** on the dashboard. This keeps the registered Tailscale identity/state while removing the reusable enrollment key from the PS5.

When registration finishes, the PS5 will show a Tailscale IP similar to:

`100.x.x.x`

Write this address down. This is the address you will use for Remote Play through Tailscale.

The PS5 Tailscale ELF automatically installs the certificate file it needs, so there is no separate certificate setup step for normal users.

---

## Step 5 — Connect Your Phone, PC, or Steam Deck to Tailscale

Install Tailscale on the device that will run Remote Play.

Sign in to the **same Tailscale account / tailnet** used by the PS5.

Make sure Tailscale says **Connected**.

You should now see both devices in your Tailscale device list:

- your PS5
- your phone, PC, Steam Deck, or other Remote Play device

---

## Step 6 — Register Remote Play Normally

Do the first Remote Play registration while you are at home with the PS5 turned on.

On the PS5, go to:

**Settings → System → Remote Play → Link Device**

The PS5 will show an 8-digit code.

Use that code to register the PS5 in **Chiaki-ng** or **PXPlay / PSPlay**.

Once registration is finished, you do not normally need to repeat it.

---

# Using Chiaki-ng

## Add the PS5 Tailscale Address

In Chiaki-ng, create or edit a manual Remote Play connection.

For the host/address, enter only your PS5 Tailscale IP:

`100.x.x.x`

Do **not** enter:

- `http://`
- your public internet IP
- `:8085`

Port `8085` is only for the PS5 Tailscale status page.

Select the PS5 registration/profile you already created, save the connection, and start Remote Play.

---

# Using PXPlay / PSPlay

1. Open your registered PS5 in PXPlay / PSPlay.
2. Choose **Remote Connect**.
3. Choose the **Manual** connection option.
4. Enter your PS5 Tailscale IP:

   `100.x.x.x`

5. Start the connection.

Do not use your home public IP and do not add port `8085`.

---

# Normal Use After Setup

After everything is registered, normal use is much shorter.

### On the PS5

1. Jailbreak the PS5.
2. Start the ELF loader / Payload Manager.
3. Run `ps5tailscale-v1.0.3.elf` once.
4. Wait until PS5 Tailscale shows **Connected**.

### On your Remote Play device

1. Start Tailscale.
2. Make sure it says **Connected**.
3. Open Chiaki-ng or PXPlay / PSPlay.
4. Connect using the saved `100.x.x.x` PS5 Tailscale address.

---

# Rest Mode

Rest Mode support is included in v1.0.3 Beta, but it should still be considered experimental.

For Rest Mode, make sure these PS5 settings are enabled:

- **Stay Connected to the Internet**
- **Enable Turning On PS5 from Network**

PS5 Tailscale has successfully remained available through Rest Mode in testing, including a report of more than six hours from an external tester. However, another test console experienced a long-Rest failure, so results may vary.

### Waking the PS5

PXPlay / PSPlay has successfully been used to wake the PS5 through the Remote Play path in testing.

Chiaki-ng works for Remote Play after the PS5 is awake, but **automatic wake from a manual Chiaki-ng Tailscale connection is still experimental**. If Chiaki-ng does not wake the console, wake the PS5 using another known-working Remote Play wake method first, then connect through the Tailscale IP.

You do **not** need an always-on PC or server for the normal v1.0.3 setup described in this guide.

---

# Troubleshooting

## PS5 Tailscale does not show Connected

Check that:

- the PS5 has internet access
- the auth key is still valid
- the PS5 appears in the Tailscale Admin Console
- device approval is completed if your Tailscale account requires it

If registration is stuck, create a new auth key and try again after a clean PS5 reboot.

---

## I cannot reach the PS5 Tailscale IP

Check that:

- PS5 Tailscale says **Connected**
- Tailscale is connected on your phone/PC/Steam Deck
- both devices are on the same Tailscale tailnet
- the PS5 is not fully powered off

The address should look like:

`100.x.x.x`

---

## Chiaki-ng or PXPlay cannot connect

Make sure you are using the PS5's **Tailscale IP**, not its normal LAN address.

Correct when connecting through Tailscale:

`100.x.x.x`

Do not use:

`192.168.x.x`

when you are away from home.

Also make sure the PS5 was already registered with the Remote Play app using the normal 8-digit PS5 Link Device code.

---

## Rest Mode stopped working

If PS5 Tailscale becomes unreachable after being in Rest Mode, **do not immediately launch the payload again**.

The v1.0.3 release ZIP includes diagnostic collectors that can save useful logs before they are lost.

### Windows

Run:

`collect-field-diagnostics.ps1`

### Android / Termux / Steam Deck / Linux

Run:

`collect-field-diagnostics.sh`

The tool will ask for your PS5's normal LAN IP address and save the available diagnostic files for sharing with the developer.

If the PS5 must be woken before the logs can be collected, wake it first but avoid launching another PS5 Tailscale payload until the logs have been saved.

---

# Resetting Tailscale Registration

If you want to move the PS5 to another Tailscale account or start registration again:

1. Open the PS5 Tailscale page.
2. Choose **Reset registration** / **Remove authentication**.
3. Reboot the PS5 when instructed.
4. Start PS5 Tailscale again.
5. Create a new Tailscale auth key.
6. Register the PS5 again.

---

# Quick Setup Checklist

1. Enable PS5 Remote Play.
2. Run `ps5tailscale-v1.0.3.elf`.
3. Open **Media → Tailscale**.
4. Create a Tailscale auth key.
5. Register the PS5.
6. Wait for **Connected**.
7. Write down the PS5 `100.x.x.x` address.
8. Connect your phone/PC/Steam Deck to the same Tailscale account.
9. Register the PS5 with Chiaki-ng or PXPlay using the normal PS5 Link Device code.
10. Use the PS5 `100.x.x.x` address as the Remote Play host.

That is the normal setup. No router port forwarding is required.

---

# v1.0.3 Beta Download Verification

This section is optional. It is for users who want to verify their download.

The GitHub Release notes list the official SHA256 values for both the release ZIP and the ELF. The `SHA256SUMS.txt` file inside the release ZIP records the ELF SHA256 so the payload can be checked after extracting the package.

Compare the published ZIP hash with the ZIP you downloaded, and compare the ELF hash with `ps5tailscale-v1.0.3.elf`. Do not use a hash copied from an old test build or development issue because pre-release candidates may have different hashes.
