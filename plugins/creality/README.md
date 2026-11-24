# Creality 3D printer

How the print on your Creality printer goes: its state, the file, the progress, the time left and when it is done, the
temperatures and the layer; and a card with keys to pause, resume and stop the print and to switch the printer's light.

**The camera is not in this plugin.** For the printer's camera, use Tessera's own camera tile (in the library's
Cameras group, with the printer's **Printer Camera**) and put it beside the printer tile. It refreshes the picture and
opens it large, which a plugin's picture cannot do yet.

It works with the **Creality WebSocket Integration** for Home Assistant (`ha_creality_ws`, from HACS), for the K1, K1C,
K1 Max, K1 SE, K2, Ender 3 V3 and Creality Hi printers. The screen never talks to the printer: Home Assistant does, and
the screen reads and controls the integration's entities.

## Set up

### 1. First, in Home Assistant

1. Install **Creality WebSocket Integration** from HACS (its current version needs Home Assistant 2026.7 or newer),
   restart Home Assistant, and add it (Settings > Devices & services > Add integration > Creality WebSocket
   Integration) with your printer's address.
2. Check the printer's device page. The plugin uses these of its entities: **Print Status**
   (`sensor.<printer>_print_status`), **Nozzle Temperature**, **Bed Temperature**, **Chamber Temperature**, **Working
   Layer**, **Total Layers**, the **Light**, the **Pause Print**, **Resume Print** and **Stop Print** buttons. Not
   every printer has them all: the K1 SE and the Ender 3 V3 have no light, and the K1 SE, the Ender 3 V3 and the Hi
   no chamber sensor. Leave those out in the inspector.
3. Let the screen perform actions: Settings > Devices & services > ESPHome > your screen > **Configure** > tick
   **Allow the device to perform Home Assistant actions**. Without it the keys do nothing.

### 2. The tile

Add the plugin to a screen in Tessera (the screen builds once). In Layout, from the library's Plugins group:

1. **Creality printer**: choose the printer's **Print Status** sensor as its entity. Then, in the inspector, its
   temperature sensors, the working layer and total layers, the light and the three buttons. Leave out what you do not
   want to see. A small tile shows the percent large; a larger one the state, the file, a bar, the time left and when
   it is done, the temperatures and the layer.
2. **The camera**: add Tessera's own camera tile with the printer's **Printer Camera**, beside the printer tile.

Changing an entity in the inspector needs no build.

### 3. The control card

1. Tap the printer tile. The card shows the printer large.
2. **Keys**: pause while it prints, resume while it is paused, stop, and the light. A key shows only when you chose its
   entity in the tile's inspector and the printer is in a state where it does something.
3. **Stop asks first**: the first tap turns the key red; tap it again within five seconds to stop the print.

## Good to know

- **When the printer is off.** With a power switch bound in the integration's options, the entities go unavailable
  while it is off and the tile says "Offline".
- **The time left** is the printer's own estimate, counted down by the screen between updates; "Done at" is the
  screen's clock plus that time.

## Tested

In Tessera's preview tool (`tools/preview.py`) on the CYD, Guition 4", Waveshare 4.3" portrait and JC8012P4A1, with no
warnings, and on a real screen with a K1.
