# Proxmox

Status, CPU, memória e disco de um nó do Proxmox; um card com as últimas 24 horas e botões para iniciar, desligar e
reiniciar; e um alerta na barra superior, em popup e no screensaver quando algo cai.

Funciona com a integração **Proxmox VE** do Home Assistant ou com a **Proxmox Extended Sensors** (HACS). Para
serviços, **Uptime Kuma** ou **Ping** também servem na lista de status.

## Configuração

### 1. Antes de começar: no Home Assistant

1. Instale uma das integrações do Proxmox (Configurações > Dispositivos e serviços > Adicionar integração).
2. Permita que a tela execute ações: Configurações > Dispositivos e serviços > ESPHome > a sua tela > **Configurar** >
   marque **Permitir que o dispositivo execute ações do Home Assistant**. Sem isso os botões do card não funcionam.

### 2. Os tiles

Adicione o plugin a uma tela no Tessera (a tela compila uma vez). Em Layout, no grupo Plugins da biblioteca:

- **Nó do Proxmox**: escolha no inspetor o **Status**, e os sensores de **CPU**, **Memória** e **Disco**. Um sensor
  em % vira uma barra que fica amarela em 75% e vermelha em 90%; use os sensores de porcentagem (ex. "Memory Usage",
  "RootFS Usage"). Outro valor (GB) aparece como número, sem barra.
- **Lista de status**: até oito entidades. Um status (VM, container, serviço) aparece com um ponto verde, amarelo ou
  vermelho e os que estão fora vêm primeiro; um sensor com número (CPU, rede) aparece com o valor ao lado do nome.

Trocar uma entidade no inspetor não precisa compilar de novo.

### 3. O card de controle

1. No inspetor do **Nó do Proxmox**, escolha os botões **Iniciar**, **Desligar** e **Reiniciar** (as entidades
   `button` da integração para esse nó ou VM).
2. Toque no tile na tela. Abre o card com o status, o gráfico das últimas 24 horas e os botões.
3. **Gráfico**: mostra a média de cada hora. Toque nele para alternar entre CPU e memória. Ele usa as estatísticas de
   longo prazo do Home Assistant, então só aparece para sensores com histórico ("Sem histórico" nos outros).
4. **Botões**: o primeiro toque deixa o botão vermelho com **Toque de novo**; o segundo, em até 5 segundos, executa.

### 4. Alerta quando algo cai

O alerta vem de um sensor que você cria uma vez no Home Assistant: ele fica `on` quando algo está fora e lista o que
caiu no atributo `problems`.

**4.1 Crie o rótulo.** Configurações > Áreas, rótulos e zonas > **Rótulos** > Criar rótulo, com o nome `Proxmox watch`.

**4.2 Marque o que vigiar.** Configurações > **Entidades**, selecione os status das VMs, containers e serviços, e
clique em **Adicionar rótulo** > Proxmox watch.

**4.3 Crie o sensor.** Abra o `configuration.yaml` (add-on File editor, Studio Code Server ou Terminal) e cole no fim:

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

Se o arquivo já tiver uma linha `template:`, cole só a partir de `- binary_sensor:`, embaixo dela.
O helper "Template" da interface não serve aqui: ele não cria o atributo `problems`.

**4.4 Ative.** Ferramentas do desenvolvedor > YAML > **Verificar configuração** (tem que estar válida). Na primeira
vez que o `template:` entra no arquivo, **reinicie o Home Assistant** (Configurações > Sistema > botão de energia);
recarregar só as entidades de template não basta. Nas próximas mudanças, "Entidades de template" já recarrega.

**4.5 Confira.** Ferramentas do desenvolvedor > Estados > `binary_sensor.proxmox_problem`: `off` com tudo no ar. Pare
uma VM de teste: vira `on` e o nome dela aparece em `problems`.

**4.6 Ligue no Tessera.** Na aba Plugins da tela, abra o Proxmox e escolha `binary_sensor.proxmox_problem` em
**Sensor de problema** (Ao compilar), depois **Salvar e compilar**. Então:

- **Barra superior**: em Layout, adicione o item **Problemas do Proxmox** (De plugins). Ele mostra o que caiu, em
  vermelho, e some quando está tudo bem.
- **Popup**: aparece sozinho quando algo cai. Desligue em **Popup quando algo cai**, nas configurações do plugin.
- **Screensaver**: na tela, Configurações > Screensaver > **Adicionar dos plugins** > **Problemas do Proxmox**, com o
  mesmo sensor. A etapa só aparece enquanto algo está fora.

## Bom saber

- Conta como no ar: `on`, `running`, `online`, `up`, `ok`, `healthy`, `quorate`. Fora: `off`, `stopped`, `offline`,
  `down`, `error`, `faulted`, `critical`. Pendente (amarelo): `pending`, `paused`, `suspended`, `degraded`, `warning`.
- Um binary sensor com device class `problem` é o contrário: `on` é problema.
- As palavras que todos os nomes da lista repetem ("1. Node: server") são omitidas.
