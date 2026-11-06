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

## Card de controle

No inspetor do tile do nó, escolha os botões **Iniciar**, **Desligar** e **Reiniciar** (entidades `button` da
integração). Tocar no tile abre o card: status grande, CPU ou memória das últimas 24 h em gráfico (toque no gráfico
para alternar; usa as estatísticas de longo prazo do HA) e os botões. Cada botão pede confirmação: o primeiro toque
deixa vermelho com **Toque de novo**, o segundo em até 5 s executa. A tela precisa ter permissão para executar ações
do Home Assistant (opções do dispositivo ESPHome no HA).

## Quando algo cai

Crie no HA um binary sensor que fica `on` quando algo está fora, com os nomes no atributo `problems`. Coloque o
rótulo **Proxmox watch** em cada entidade de status que quer vigiar e use o template do README em inglês. Escolha esse
sensor como **Sensor de problema** ao adicionar o plugin. Aí:

- o item **Problemas do Proxmox** da barra superior mostra o que caiu, em vermelho;
- um popup aparece quando algo cai (a configuração **Popup quando algo cai** desliga);
- a etapa **Problemas do Proxmox** do screensaver mostra o que caiu na tela preta, e é pulada quando está tudo bem.
