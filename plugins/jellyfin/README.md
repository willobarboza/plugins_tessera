# Jellyfin

What plays on your Jellyfin clients, with its cover, the series and episode or the artist, and how far along; a card
with keys to play and pause, seek, stop, change the volume and mute; and your libraries to browse and play from the
screen.

It works with Home Assistant's own **Jellyfin** integration. The screen never talks to the Jellyfin server: Home
Assistant does, and the screen reads and controls the integration's media players.

## Set up

### 1. First, in Home Assistant

1. Add the **Jellyfin** integration (Settings > Devices & services > Add integration > Jellyfin) with your server's
   address and a Jellyfin user.
2. Open a Jellyfin app on a TV, a phone or a browser. The integration makes a **media player** for every app or device
   that connects to the server (`media_player.living_room_tv`). It stays in Home Assistant when the app closes, as
   unavailable until the app connects again.
3. Let the screen perform actions: Settings > Devices & services > ESPHome > your screen > **Configure** > tick
   **Allow the device to perform Home Assistant actions**. Without it the keys do nothing.

### 2. The tiles

Add the plugin to a screen in Tessera (the screen builds once). In Layout, from the library's Plugins group:

1. **Jellyfin now playing**: choose one Jellyfin media player as its entity. It shows the cover (on screens with
   pictures), the title, the series and episode or the artist and album, and a bar with the time played and left.
   With nothing playing it says so.
2. **Jellyfin sessions**: choose up to eight Jellyfin media players, and in **Names** your own names for them in their
   order (`Living room, Bedroom`; an empty place keeps the player's name). The ones that play come first. Turn on
   **Only what plays** to leave out the idle and offline ones.

Changing an entity in the inspector needs no build.

### 3. The control card

1. Tap a now playing tile, or a row of the sessions tile. The card opens with the cover, what plays and how far along.
2. **Keys**: back and forward by the **Seek step** of the tile (30 s unless you change it), play and pause, stop, the
   volume down and up, and mute. A key shows only when the Jellyfin app says it can do it: a browser has no volume of
   its own, for example.
3. **Library** opens the library card for that player.

### 4. The library

1. The card starts with your libraries (Movies, Shows, Music). Tap one to open it, then a series, a season or an album.
2. Tap a film, an episode or a song to play it on the player the card was opened for. **Play all** plays the folder
   that is open (a season, an album).
3. **Back** at the top left goes up a level; the arrows at the bottom turn the page.

## Good to know

- **A large library shows its first items only.** An answer of Home Assistant reaches the screen within about 3 KB, so a
  folder shows its first 8 to 24 items, sorted by name, and the card says so. Open a smaller folder (a series, a
  season, a collection) to reach the rest.
- **No covers in the library.** Its pictures are on your Jellyfin server, on the home network, where Tessera fetches
  nothing. The now playing tile and the control card do show the cover: Home Assistant hands it over.
- **No next and previous track.** The integration's media player has no such action; use seek, or the library.
- A Jellyfin app that cannot be controlled remotely (some TV apps) shows what it plays but has no keys.
- The library is read through the player's own media player, so it needs that player connected to the server.
