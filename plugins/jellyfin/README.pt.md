# Jellyfin

O que toca nos seus clientes Jellyfin, com a capa, a série e o episódio ou o artista, e quanto já passou; um card com
teclas para tocar e pausar, avançar e voltar, parar, mudar o volume e silenciar; e suas bibliotecas para navegar e
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
   **Allow the device to perform Home Assistant actions**. Sem isso as teclas não fazem nada.

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
2. **Teclas**: voltar e avançar pelo **Salto do avanço** do tile (30 s se você não mudar), tocar e pausar, parar,
   volume para baixo e para cima, e silenciar. Uma tecla só aparece quando o app do Jellyfin diz que consegue fazer
   aquilo: um navegador não tem volume próprio, por exemplo.
3. **Biblioteca** abre o card da biblioteca para aquele player.

### 4. A biblioteca

1. O card começa pelas suas bibliotecas (Filmes, Séries, Música). Toque numa para abrir, depois numa série, numa
   temporada ou num álbum.
2. Toque num filme, num episódio ou numa música para tocar no player para o qual o card foi aberto. **Tocar tudo**
   toca a pasta aberta (uma temporada, um álbum).
3. **Voltar**, no canto de cima à esquerda, sobe um nível; as setas embaixo trocam a página.

## Bom saber

- **Uma biblioteca grande mostra só os primeiros itens.** A resposta do Home Assistant chega à tela com uns 3 KB, então
  uma pasta mostra os primeiros 8 a 24 itens, em ordem de nome, e o card avisa. Abra uma pasta menor (uma série, uma
  temporada, uma coleção) para chegar ao resto.
- **Sem capas na biblioteca.** As imagens estão no seu servidor Jellyfin, na rede de casa, de onde o Tessera não busca
  nada. O tile tocando agora e o card de controle mostram a capa: o Home Assistant a entrega.
- **Sem próxima e anterior.** O media player da integração não tem essa ação; use o avanço ou a biblioteca.
- Um app do Jellyfin que não aceita controle remoto (alguns apps de TV) mostra o que toca, mas não tem teclas.
- A biblioteca é lida pelo media player do próprio player, então ele precisa estar conectado ao servidor.
