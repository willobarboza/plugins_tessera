# Jellyfin

O que toca nos seus clientes Jellyfin, com a capa, a série e o episódio ou o artista, e quanto já passou; um card com
botões para tocar e pausar, avançar e voltar, parar, mudar o volume e silenciar; e suas bibliotecas para navegar e
tocar a partir da tela.

Funciona com a integração **Jellyfin** do próprio Home Assistant. A tela nunca fala com o servidor Jellyfin: quem fala
é o Home Assistant, e a tela lê e controla os media players da integração.

## Configuração

### 1. Primeiro, no Home Assistant

1. Adicione a integração **Jellyfin** (Configurações > Dispositivos e serviços > Adicionar integração > Jellyfin) com
   o endereço do seu servidor e um usuário do Jellyfin.
2. Abra um app do Jellyfin numa TV, num celular ou num navegador. A integração cria um **media player** para cada app
   ou aparelho que se conecta ao servidor (`media_player.living_room_tv`). Ele continua no Home Assistant quando o app
   fecha, como indisponível, até o app se conectar de novo.
3. Deixe a tela executar ações: Configurações > Dispositivos e serviços > ESPHome > sua tela > **Configurar** > marque
   **Permitir que o dispositivo execute ações do Home Assistant** (*Allow the device to perform Home Assistant
   actions*). Sem isso os botões não fazem nada.

### 2. Os tiles

Adicione o plugin a uma tela no Tessera (a tela compila uma vez). Em Layout, no grupo Plugins da biblioteca:

1. **Jellyfin tocando agora**: escolha um media player do Jellyfin como entidade. Mostra a capa (nas telas com
   imagens), o título, a série e o episódio ou o artista e o álbum, e uma barra com o tempo tocado e o que falta. Sem
   nada tocando, ele avisa.
2. **Sessões do Jellyfin**: escolha até oito media players do Jellyfin e, em **Nomes**, seus próprios nomes para eles
   na ordem (`Sala, Quarto`; um lugar vazio mantém o nome do player). Os que estão tocando vêm primeiro. Ligue **Só o
   que toca** para esconder os parados e offline.

Trocar uma entidade no inspetor não precisa de compilação.

### 3. O card de controle

1. Toque num tile tocando agora, ou numa linha do tile de sessões. O card abre com a capa, o que toca e quanto já
   passou.
2. **Botões**: voltar e avançar pelo **Pulo ao voltar e avançar** do tile (30 s se você não mudar), tocar e pausar, parar,
   volume para baixo e para cima, e silenciar. Um botão só aparece quando o app do Jellyfin diz que consegue fazer
   aquilo: nem todo app informa um volume, por exemplo.
3. **Biblioteca** abre o card da biblioteca para aquele player.

### 4. A biblioteca

1. O card começa pelas suas bibliotecas (Filmes, Séries, Música). Toque numa para abrir, depois numa série, numa
   temporada ou num álbum.
2. Toque num filme, num episódio ou numa música para tocar no player para o qual o card foi aberto. **Tocar tudo**
   toca a pasta aberta (uma temporada, um álbum).
3. **Voltar**, no canto de cima à esquerda, sobe um nível; as setas embaixo trocam a página.

## Bom saber

- **Uma biblioteca grande mostra só os primeiros itens.** A resposta do Home Assistant chega à tela com uns 3 KB, então
  uma pasta mostra só os primeiros itens (uns dez, na ordem do servidor), e o card avisa que pode haver mais. Abra uma pasta menor (uma série, uma
  temporada, uma coleção) para chegar ao resto.
- **Sem capas na biblioteca.** As imagens estão no seu servidor Jellyfin, na rede de casa, de onde o Tessera não busca
  nada. O tile tocando agora e o card de controle mostram a capa: o Home Assistant a entrega.
- **Sem próxima e anterior.** O media player da integração não tem essa ação; use o avanço ou a biblioteca.
- **Vários players com o mesmo nome.** O Jellyfin cria um media player para cada sessão, então uma TV pode aparecer
  várias vezes (três "Samsung Smart TV", `media_player.samsung_smart_tv_2`...). Escolha aquele cujo estado muda
  enquanto toca, e escolha de novo se o app criar outro.
- **Nem todo app do Jellyfin aceita comandos.** Um app que não aceita controle remoto mostra o que toca, mas não tem
  botões nem biblioteca. O receptor do Chromecast não informa nenhum comando, e numa casa um app da Samsung (Tizen) e
  um de Android não aceitaram nada do Home Assistant, enquanto um navegador aceitou. Teste rápido: o painel do
  Jellyfin só mostra botões de controle numa sessão quando o servidor consegue controlá-la; sem eles, nem o Home
  Assistant nem a tela conseguem.
- **A capa como descanso de tela.** O passo **Música a tocar** do próprio descanso de tela do Tessera (Configurações
  da tela > Descanso de tela) mostra a capa de um player do Jellyfin enquanto ele toca, sem plugin extra.
- A biblioteca é lida pelo media player do próprio player, então ele precisa estar conectado ao servidor.

## Testado

Na bancada do Tessera (CYD, Waveshare 4.3" em pé, JC8012P4A1) e numa tela de verdade, com os apps do Jellyfin para
Samsung (Tizen), Android, Android TV, Chromecast e web. Não testado: a tela Guition de 4", cuja compilação falha no
núcleo do Tessera com o ESPHome 2026.9.
