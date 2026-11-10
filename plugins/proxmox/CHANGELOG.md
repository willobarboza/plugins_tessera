# Changelog

## 0.3.2 - 2026-11-10
- A small node tile fits every chosen value: it draws them tighter, then without bars, instead of leaving one out.
- A value under 10% keeps its decimal: 1.4%, not 1%.

## 0.3.1 - 2026-11-06
- The tiles take any size the screen's grid allows, up to 9x9.
- The control card is as wide as the glass, so its chart is larger.

## 0.3.0 - 2026-11-06
- A tap on a node tile opens its card: the status large, CPU or memory of the last 24 hours as a chart (tap it to
  switch), and keys to start, shut down or restart, each asking once more before it acts.
- New in the node tile's inspector: the start, shut down and restart buttons.
- Something down shows in a top bar item, in a popup (a setting turns it off), and on a step of the screensaver that
  shows only then. Choose the problem sensor when adding the plugin (see the README).

## 0.2.0 - 2026-11-06
- The status list shows the value of a sensor with a number (CPU, network traffic) beside its name, and the
  state of a VM or service as a word.
- The words every name starts with ("1. Node: server") are left out.
- Understands the states of Proxmox Extended Sensors: a ZFS pool, a backup, a cluster's quorum.

## 0.1.0 - 2026-11-06
- First version: a Proxmox node tile (status, CPU, memory and disk) and a status list of up to eight guests or
  services, the ones that are down first.
