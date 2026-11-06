# Proxmox

Status, CPU, memória e disco de um nó do Proxmox num tile, e uma lista de VMs ou serviços que mostra quais caíram.

## Configuração

1. Adicione a integração **Proxmox VE** no Home Assistant, para o nó e as VMs virarem entidades. Para serviços, a
   integração **Uptime Kuma** ou **Ping** funciona do mesmo jeito.
2. Adicione o plugin a uma tela no Tessera. A tela é compilada uma vez.
3. Em Layout, coloque **Nó do Proxmox** ou **Lista de status**, no grupo Plugins da biblioteca.
4. No inspetor, escolha as entidades:
   - **Nó do Proxmox**: um status (binary sensor, ou sensor que diz running, online ou up) e sensores de CPU, memória e
     disco. Um sensor em % vira uma barra que fica amarela em 75% e vermelha em 90%.
   - **Lista de status**: até oito VMs, containers ou serviços. Os que estão fora aparecem primeiro.

## Bom saber

- Um binary sensor ou switch está no ar quando está `on`, exceto com device class `problem`.
- Trocar a entidade no inspetor não precisa recompilar.
