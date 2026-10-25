#pragma once

#include <string>
#include <vector>

#include "PainelConfig.h"
#include "activities/Activity.h"
#include "network/HttpDownloader.h"

enum class PainelState {
  SEM_CONFIG,  // não há /painel.txt válido no cartão
  WIFI,        // a WifiSelectionActivity está por cima, conectando
  BAIXANDO,    // baixando as páginas do servidor
  MOSTRANDO,   // uma página na tela
  ERRO         // nada baixado e nada guardado para mostrar
};

/**
 * Painel: as telas que o AkiraOS desenha (GET /xteink/tela, BMP de 1 bit em 480×800) mostradas no
 * aparelho. Ao entrar, conecta no Wi-Fi salvo, baixa cada página para /.painel/<pagina>.bmp e mostra a
 * primeira. As setas trocam de página (já no cartão, sem rede); Confirmar baixa tudo de novo; Voltar sai.
 * As teclas laterais (cima/baixo) mandam a ação daquela página ao servidor (GET /xteink/acao), que responde
 * com a página já redesenhada. As dicas dos botões são desenhadas aqui, por cima da imagem, como nas outras
 * telas do CrossPoint: embaixo, Voltar, Atualizar e as setas com o nome da página vizinha; na borda direita,
 * o que cada tecla lateral faz nesta página (os rótulos vêm do catálogo, GET /xteink/paginas). O servidor
 * deixa essas duas faixas em branco.
 * A página de descanso (config `descanso`) é copiada para /sleep.bmp, então a tela de dormir mostra o
 * painel sem o firmware precisar de nada mais.
 *
 * Memória: a página chega em fluxo para o cartão (nada do BMP fica na RAM) e é desenhada do arquivo pelo
 * mesmo caminho do visualizador de BMP. O único buffer extra é o de cópia do descanso (2 KB, no heap, só
 * durante a cópia).
 */
// O que o aparelho sabe de cada página além do id: o nome (nas dicas das setas) e o que as teclas laterais
// fazem nela (vazio quando a página não tem ação para aquela tecla).
struct PainelRotulos {
  std::string nome, cima, baixo;
};

class PainelActivity final : public Activity {
 public:
  explicit PainelActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Painel", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == PainelState::BAIXANDO || state == PainelState::WIFI; }

 private:
  PainelConfig config;
  std::vector<PainelRotulos> rotulos;  // alinhado com config.paginas
  bool paginasFixas = false;           // o cartão trouxe `paginas=`: o catálogo só dá os rótulos
  PainelState state = PainelState::SEM_CONFIG;
  int paginaAtual = 0;
  bool baixarPendente = false;
  bool wifiLigadoAqui = false;
  std::string erro;
  int leiturasNovas = 0;

  void conectar();
  void onWifiPronto(bool conectado);
  void baixarAgora();
  bool lerCatalogo();
  void rotulosPadrao();
  void desenharDicas() const;
  void acao(const char* tecla);
  int paginasGuardadas() const;
  std::string caminho(const std::string& pagina) const;
  void mudarPagina(int delta);
  void renderMensagem(const char* titulo, const char* detalhe) const;
  bool renderPagina() const;
};
