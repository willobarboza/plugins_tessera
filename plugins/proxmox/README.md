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

## The control card

Choose the node's or guest's **Start**, **Shut down** and **Restart** buttons in the node tile's inspector. A tap on
the tile then opens its card: the status large, the last 24 hours of CPU or memory as a chart (tap the chart to switch;
it reads Home Assistant's long-term statistics, so the sensor needs a state class), and the keys. A key asks once more:
tap it, it turns red and says **Tap again**; tap again within five seconds and it presses the button. The screen must
be allowed to perform Home Assistant actions (the ESPHome device's options in Home Assistant).

## When something goes down

Make one binary sensor in Home Assistant that is on while something is down, with the names of what is down in its
attribute `problems`. With a label **Proxmox watch** on every status entity to watch:

```yaml
template:
  - binary_sensor:
      - name: Proxmox problem
        unique_id: proxmox_problem
        device_class: problem
        state: >
          {{ label_entities('Proxmox watch') | select('is_state', ['off', 'unavailable', 'stopped', 'offline', 'down'])
             | list | count > 0 }}
        attributes:
          problems: >
            {{ label_entities('Proxmox watch') | select('is_state', ['off', 'unavailable', 'stopped', 'offline', 'down'])
               | map('state_attr', 'friendly_name') | list }}
```

Choose it as the **Problem sensor** when adding the plugin. Then:

- the **Proxmox problems** item of the top bar shows what is down, in red, and nothing while all is well;
- a popup shows when something goes down (the **Popup on a problem** setting turns it off);
- the screensaver's **Proxmox problems** step (Screensaver card, Add from plugins, with the same sensor) shows what is
  down on the black glass, and is skipped while all is well.
