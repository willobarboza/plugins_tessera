# Proxmox

A Proxmox node's status, CPU, memory and disk on a tile, and a list of guests or services that says which are down.

## Set up

1. Add Home Assistant's **Proxmox VE** integration, so the node and its guests are entities. For services, the
   **Uptime Kuma** or **Ping** integration works the same way.
2. Add the plugin to a screen in Tessera. The screen builds once.
3. In Layout, place **Proxmox node** or **Status list** from the library's Plugins group.
4. In the inspector, choose the entities:
   - **Proxmox node**: a status (a binary sensor, or a sensor that says running, online or up), and sensors for CPU,
     memory and disk. A sensor in % shows as a bar that turns amber at 75% and red at 90%.
   - **Status list**: up to eight VMs, containers or services. The ones that are down come first.

## Good to know

- A binary sensor or a switch is up when it is on, unless its device class is `problem`.
- A sensor is up when it says `running`, `online`, `up` or `on`, down for `stopped`, `offline` or `down`, and pending
  for `pending`, `paused` or `maintenance`.
- Changing an entity in the inspector needs no build.
