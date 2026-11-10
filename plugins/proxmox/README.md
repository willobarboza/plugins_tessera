# Proxmox

A Proxmox node's status, CPU, memory and disk; a card with its last 24 hours and keys to start, shut down and restart
it; and an alert in the top bar, in a popup and on the screensaver when something goes down.

It works with Home Assistant's **Proxmox VE** integration or with **Proxmox Extended Sensors** (HACS). For services,
**Uptime Kuma** or **Ping** fit the status list too.

## Set up

### 1. First, in Home Assistant

1. Add one of the Proxmox integrations (Settings > Devices & services > Add integration).
2. Let the screen perform actions: Settings > Devices & services > ESPHome > your screen > **Configure** > tick
   **Allow the device to perform Home Assistant actions**. Without it the card's keys do nothing.

### 2. The tiles

Add the plugin to a screen in Tessera (the screen builds once). In Layout, from the library's Plugins group:

- **Proxmox node**: choose a **Status** and sensors for **CPU**, **Memory** and **Disk** in the inspector. A sensor
  in % shows as a bar that turns amber at 75% and red at 90%: take the percentage sensors ("Memory Usage", "RootFS
  Usage"). Another value (GB) shows as a number without a bar.
- **Status list**: up to eight entities, and in **Names** your own names for them in their order (`Arco, Docker, NAS`;
  an empty place keeps the entity's name). A status (a VM, a container, a service) shows a green, amber or red dot, the
  ones that are down first; a sensor with a number (CPU, network) shows its value beside its name.

Changing an entity in the inspector needs no build.

### 3. The control card

1. In the **Proxmox node** inspector, choose the **Start**, **Shut down** and **Restart** buttons (the integration's
   `button` entities of that node or guest).
2. Tap the tile on the screen. The card opens with the status, the chart of the last 24 hours and the keys.
3. **Chart**: the mean of every hour. Tap it to switch between CPU and memory. It reads Home Assistant's long-term
   statistics, so it shows only for sensors that keep them ("No history" for the others).
4. **Keys**: the first tap turns a key red with **Tap again**; a second tap within 5 seconds acts.

### 4. An alert when something goes down

The alert comes from one sensor you make once in Home Assistant: it is on while something is down, and names what is
down in its attribute `problems`.

**4.1 Make the label.** Settings > Areas, labels & zones > **Labels** > Create label, named `Proxmox watch`.

**4.2 Mark what to watch.** Settings > **Entities**, select the status entities of the VMs, containers and services,
and **Add label** > Proxmox watch.

**4.3 Make the sensor.** Open `configuration.yaml` (the File editor, Studio Code Server or Terminal add-on) and add at
the end:

```yaml
template:
  - binary_sensor:
      - name: Proxmox problem
        unique_id: proxmox_problem
        device_class: problem
        state: >
          {{ label_entities('Proxmox watch') | select('is_state', ['off','unavailable','stopped','offline','down']) | list | count > 0 }}
        attributes:
          problems: >
            {{ label_entities('Proxmox watch') | select('is_state', ['off','unavailable','stopped','offline','down']) | map('state_attr','friendly_name') | list }}
```

If the file has a `template:` line already, add only the part from `- binary_sensor:` under it. The Template helper of
the interface does not do here: it makes no `problems` attribute.

**4.4 Turn it on.** Developer tools > YAML > **Check configuration** (it must be valid). The first time `template:`
comes into the file, **restart Home Assistant** (Settings > System > the power key); reloading the template entities
alone is not enough. After that, "Template entities" reloads a change.

**4.5 Check it.** Developer tools > States > `binary_sensor.proxmox_problem`: `off` while all is up. Stop a test VM:
it turns `on`, with its name in `problems`.

**4.6 Connect it in Tessera.** On the screen's Plugins tab, open Proxmox, choose `binary_sensor.proxmox_problem` as
the **Problem sensor** (When building), and **Save and build**. Then:

- **Top bar**: in Layout, add the **Proxmox problems** item (From plugins). It shows what is down, in red, and nothing
  while all is well.
- **Popup**: shows by itself when something goes down. Turn it off with **Popup on a problem** in the plugin's settings.
- **Screensaver**: on the screen, Settings > Screensaver > **Add from plugins** > **Proxmox problems**, with the same
  sensor. The step shows only while something is down.

## Good to know

- Up: `on`, `running`, `online`, `up`, `ok`, `healthy`, `quorate`. Down: `off`, `stopped`, `offline`, `down`, `error`,
  `faulted`, `critical`. Pending (amber): `pending`, `paused`, `suspended`, `degraded`, `warning`.
- A binary sensor of device class `problem` is the other way round: on is a problem.
- The words every name of the list starts with ("1. Node: server") are left out.
