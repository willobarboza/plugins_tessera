# Creality 3D printer

How the print on your Creality printer goes: its state, the file, the progress, the time left and when it is done, the
temperatures and the layer; its camera, with the progress under it; and a card with keys to pause, resume and stop the
print and to switch the printer's light.

It works with the **Creality WebSocket Integration** for Home Assistant (`ha_creality_ws`, from HACS), for the K1, K1C,
K1 Max, K1 SE, K2, Ender 3 V3 and Creality Hi printers. The screen never talks to the printer: Home Assistant does, and
the screen reads and controls the integration's entities.

## Set up

### 1. First, in Home Assistant

1. Install **Creality WebSocket Integration** from HACS, restart Home Assistant, and add it (Settings > Devices &
   services > Add integration > Creality WebSocket Integration) with your printer's address.
2. Check the printer's device page. The plugin uses these of its entities: **Print Status**
   (`sensor.<printer>_print_status`), **Nozzle Temperature**, **Bed Temperature**, **Chamber Temperature**, **Working
   Layer**, **Total Layers**, the **Light**, the **Pause Print**, **Resume Print** and **Stop Print** buttons, and the
   **Printer Camera**.
3. Let the screen perform actions: Settings > Devices & services > ESPHome > your screen > **Configure** > tick
   **Allow the device to perform Home Assistant actions**. Without it the keys do nothing.

### 2. The tiles

Add the plugin to a screen in Tessera (the screen builds once). In Layout, from the library's Plugins group:

1. **Creality printer**: choose the printer's **Print Status** sensor as its entity. Then, in the inspector, its
   temperature sensors, the working layer and total layers, the light and the three buttons. Leave out what you do not
   want to see. A small tile shows the percent large; a larger one the state, the file, a bar, the time left and when
   it is done, the temperatures and the layer.
2. **Creality camera**: choose the printer's **Printer Camera** as its entity, and its **Print Status** sensor in the
   inspector for the line of progress under the picture. Turn on **Live** to stream the camera on a screen that can
   (the ESP32-P4 boards); other screens show a picture that refreshes. On a screen without pictures (the CYD) the
   tile shows the printer's state instead.

Changing an entity in the inspector needs no build.

### 3. The control card

1. Tap either tile. The card shows the printer large, with the camera beside it when you opened it from the camera
   tile.
2. **Keys**: pause while it prints, resume while it is paused, stop, and the light. A key shows only when you chose its
   entity in the tile's inspector and the printer is in a state where it does something.
3. **Stop asks first**: the first tap turns the key red; tap it again within five seconds to stop the print.

## Good to know

- **The camera of the K2 family.** The integration streams those cameras over WebRTC. In its default mode (through
  go2rtc) Home Assistant makes still pictures of it and the tile shows them; in the **webrtc_direct** mode there is no
  still picture, so the tile says "No picture". The K1 family and the Ender 3 V3 stream MJPEG, which always works.
- **When the printer is off.** With a power switch bound in the integration's options, the entities go unavailable
  while it is off and the tile says "Off" or "Offline".
- **The time left** is the printer's own estimate, counted down by the screen between updates; "Done at" is the
  screen's clock plus that time.

## Tested

On Tessera's bench (CYD, Waveshare 4.3" portrait, JC8012P4A1). Not yet on a real screen with a printer.
