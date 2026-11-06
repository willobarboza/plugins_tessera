# Changelog

## 0.2.0 - 2026-11-06
- The status list shows the value of a sensor with a number (CPU, network traffic) beside its name, and the
  state of a VM or service as a word.
- The words every name starts with ("1. Node: server") are left out.
- Understands the states of Proxmox Extended Sensors: a ZFS pool, a backup, a cluster's quorum.

## 0.1.0 - 2026-11-06
- First version: a Proxmox node tile (status, CPU, memory and disk) and a status list of up to eight guests or
  services, the ones that are down first.
