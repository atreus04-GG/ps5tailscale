# Troubleshooting

## Dashboard does not show Connected

Do not enter Rest Mode yet. Give initial registration a little time and refresh the page. If it remains stuck, confirm the PS5 has normal internet access and that the auth key is still valid.

After a failed registration attempt, prefer a clean PS5 reboot before trying another PS5Tailscale payload.

## I cannot reach the `100.x.x.x` address

Check that the PS5 dashboard says **Connected**, the client device is also connected to Tailscale, and both devices are on the same tailnet.

If the PS5 is fully powered off, its PS5-resident Tailscale node cannot answer.

## Chiaki-ng or PXPlay cannot connect

When connecting through Tailscale, use the PS5's Tailscale IP (`100.x.x.x`) as the Remote Play host rather than the home LAN address.

The Remote Play client still needs to be registered with the PS5 using Sony's normal Link Device process.

If the PS5 is asleep, remember that automatic manual-Chiaki wake is not guaranteed in v1.0.3 Beta. Wake the console with a known working method first, then retry the Remote Play connection.

## Rest Mode stopped working

Do not immediately run the payload again. Follow [REST_MODE.md](REST_MODE.md) and collect the field diagnostics before restarting PS5Tailscale when practical.

## I need to restart PS5Tailscale

Do not start a second copy on the same boot. Reboot the PS5, start the jailbreak / ELF loader again, and then launch PS5Tailscale once.

## I need to register the PS5 again

Use **Reset registration** from the PS5Tailscale dashboard, then reboot when instructed. After the next clean launch, create a new auth key and register again.
