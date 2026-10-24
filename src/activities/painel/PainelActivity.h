#pragma once

#include <string>

#include "PainelConfig.h"
#include "activities/Activity.h"

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
 * A página de descanso (config `descanso`) é copiada para /sleep.bmp, então a tela de dormir mostra o
 * painel sem o firmware precisar de nada mais.
 *
 * Memória: a página chega em fluxo para o cartão (nada do BMP fica na RAM) e é desenhada do arquivo pelo
 * mesmo caminho do visualizador de BMP. O único buffer extra é o de cópia do descanso (2 KB, no heap, só
 * durante a cópia).
 */
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
  PainelState state = PainelState::SEM_CONFIG;
  int paginaAtual = 0;
  bool baixarPendente = false;
  bool wifiLigadoAqui = false;
  std::string erro;

  void conectar();
  void onWifiPronto(bool conectado);
  void baixarAgora();
  bool baixarPagina(const std::string& pagina);
  int paginasGuardadas() const;
  std::string caminho(const std::string& pagina) const;
  void copiarDescanso() const;
  void mudarPagina(int delta);
  void renderMensagem(const char* titulo, const char* detalhe) const;
  bool renderPagina() const;
};
