# Changelog

## 0.1.2 - 2026-11-20
- A Jellyfin app that cannot be controlled remotely shows no keys and no Library any more, instead of keys that did
  nothing.
- The sessions tile keeps a player whose data did not fit the screen's message, as "No data", instead of leaving it out.
- Long names and titles are cut on a whole letter; the screen redraws less every second.
- The README says how to find the right player when a TV shows up several times, which Jellyfin apps take commands,
  and how to show the cover as a screensaver.

## 0.1.1 - 2026-11-10
- The library now opens a library, a series or an album: it asks Home Assistant with the folder's type, which
  Home Assistant needs beside its id.

## 0.1.0 - 2026-11-10
- First version: a now playing tile of one Jellyfin client (cover, title, series and episode or artist, progress), a
  sessions tile of up to eight clients, a control card (play and pause, seek, stop, volume, mute) and a library card
  that browses your libraries and plays what you tap. Needs Home Assistant's Jellyfin integration (see the README).
