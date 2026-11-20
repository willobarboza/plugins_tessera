# Impressora 3D Creality

Como vai a impressão na sua impressora Creality: o estado, o arquivo, o progresso, o tempo restante e a hora em que
termina, as temperaturas e a camada; e um cartão com teclas para pausar, retomar e parar a impressão e para ligar e
desligar a luz da impressora.

**A câmera não faz parte deste plugin.** Para a câmera da impressora, use o tile de câmera nativo do Tessera (no grupo
Câmeras da biblioteca, com a **Printer Camera** da impressora) e coloque-o ao lado do tile da impressora. Ele atualiza
a imagem e a abre grande, o que a imagem de um plugin ainda não consegue.

Funciona com a **Creality WebSocket Integration** do Home Assistant (`ha_creality_ws`, do HACS), para as impressoras
K1, K1C, K1 Max, K1 SE, K2, Ender 3 V3 e Creality Hi. A tela nunca fala com a impressora: quem fala é o Home Assistant,
e a tela lê e controla as entidades da integração.

## Configurar

### 1. Primeiro, no Home Assistant

1. Instale a **Creality WebSocket Integration** pelo HACS, reinicie o Home Assistant e adicione a integração
   (Configurações > Dispositivos e serviços > Adicionar integração > Creality WebSocket Integration) com o endereço da
   impressora.
2. Confira a página do dispositivo da impressora. O plugin usa estas entidades: **Print Status**
   (`sensor.<impressora>_print_status`), **Nozzle Temperature**, **Bed Temperature**, **Chamber Temperature**,
   **Working Layer**, **Total Layers**, a **Light**, os botões **Pause Print**, **Resume Print** e **Stop Print**.
3. Permita que a tela execute ações: Configurações > Dispositivos e serviços > ESPHome > sua tela > **Configurar** >
   marque **Permitir que o dispositivo execute ações do Home Assistant**. Sem isso as teclas não fazem nada.

### 2. O tile

Adicione o plugin a uma tela no Tessera (a tela compila uma vez). Em Layout, no grupo Plugins da biblioteca:

1. **Impressora Creality**: escolha o sensor **Print Status** da impressora como entidade. Depois, no inspetor, os
   sensores de temperatura, a camada atual e o total de camadas, a luz e os três botões. Deixe vazio o que não quiser
   ver. Um tile pequeno mostra o percentual grande; um maior mostra o estado, o arquivo, uma barra, o tempo restante e
   a hora em que termina, as temperaturas e a camada.
2. **A câmera**: adicione o tile de câmera nativo do Tessera com a **Printer Camera** da impressora, ao lado do tile
   da impressora.

Trocar uma entidade no inspetor não precisa compilar de novo.

### 3. O cartão de controle

1. Toque no tile da impressora. O cartão mostra a impressora grande.
2. **Teclas**: pausar enquanto imprime, retomar enquanto está pausada, parar e a luz. Uma tecla só aparece quando você
   escolheu a entidade dela no inspetor do tile e a impressora está num estado em que ela faz algo.
3. **Parar pergunta antes**: o primeiro toque deixa a tecla vermelha; toque de novo em até cinco segundos para
   interromper a impressão.

## Bom saber

- **Com a impressora desligada.** Com uma tomada vinculada nas opções da integração, as entidades ficam indisponíveis
  enquanto ela está desligada e o tile diz "Desligada" ou "Offline".
- **O tempo restante** é a estimativa da própria impressora, contada regressivamente pela tela entre as atualizações;
  "Pronta às" é o relógio da tela mais esse tempo.

## Testado

Na bancada do Tessera (CYD, Guition 4", Waveshare 4.3" retrato, JC8012P4A1), sem avisos, e numa tela real com uma K1.
