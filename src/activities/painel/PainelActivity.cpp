#include "PainelActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include <cmath>

#include "CrossPointSettings.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "PainelRede.h"
#include "network/HttpDownloader.h"

namespace {
bool wifiConectado() { return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0); }
}  // namespace

// ---------- ciclo de vida ----------

void PainelActivity::onEnter() {
  Activity::onEnter();
  paginaAtual = 0;
  if (!Storage.exists(PainelConfig::ARQUIVO)) {
    state = PainelState::SEM_CONFIG;
    requestUpdate();
    return;
  }
  if (!config.carregar()) {
    state = PainelState::ERRO;
    erro = tr(STR_PAINEL_CONFIG_INVALIDA);
    requestUpdate();
    return;
  }
  Storage.ensureDirectoryExists(PainelConfig::PASTA);
  conectar();
}

void PainelActivity::onExit() {
  Activity::onExit();
  if (wifiLigadoAqui && WiFi.getMode() != WIFI_MODE_NULL) {
    // Mesmo caminho do navegador OPDS: derrubar o rádio e reiniciar em silêncio, em vez de deixar o heap
    // fragmentado pelo Wi-Fi para o leitor.
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void PainelActivity::loop() {
  Activity::loop();

  if (baixarPendente) {
    baixarPendente = false;
    state = PainelState::BAIXANDO;
    requestUpdateAndWait();  // a mensagem "atualizando" precisa estar na tela antes do download bloquear
    baixarAgora();
    requestUpdate();
    return;
  }

  if (state == PainelState::WIFI || state == PainelState::BAIXANDO) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome(HomeMenuItem::PAINEL);
    return;
  }
  if (state != PainelState::MOSTRANDO) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    conectar();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    mudarPagina(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    mudarPagina(+1);
    return;
  }
  // As teclas laterais são as ações: o servidor desenhou no rodapé o que cada uma faz nesta página.
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    acao("cima");
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    acao("baixo");
  }
}

// ---------- rede ----------

void PainelActivity::conectar() {
  if (wifiConectado()) {
    baixarPendente = true;
    return;
  }
  state = PainelState::WIFI;
  wifiLigadoAqui = true;
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiPronto(!result.isCancelled); });
}

void PainelActivity::onWifiPronto(const bool conectado) {
  if (conectado) {
    baixarPendente = true;
    return;
  }
  // Sem rede: mostra o que já há no cartão; sem nada, erro.
  state = paginasGuardadas() > 0 ? PainelState::MOSTRANDO : PainelState::ERRO;
  erro = tr(STR_WIFI_CONN_FAILED);
  requestUpdate();
}

std::string PainelActivity::caminho(const std::string& pagina) const { return painelRede::caminhoPagina(pagina); }

int PainelActivity::paginasGuardadas() const {
  int n = 0;
  for (const auto& p : config.paginas) {
    if (Storage.exists(caminho(p).c_str())) ++n;
  }
  return n;
}

// GET /xteink/paginas e colhe os "id" na ordem; o JSON é pequeno (< 300 bytes) e plano, então uma
// varredura de texto basta e dispensa um parser.
bool PainelActivity::lerCatalogo() {
  std::string corpo;
  if (!HttpDownloader::fetchUrl(config.url + "/xteink/paginas", corpo, painelRede::cabecalhos(config))) return false;
  std::vector<std::string> ids;
  ids.reserve(6);
  size_t pos = 0;
  while ((pos = corpo.find("\"id\":\"", pos)) != std::string::npos) {
    pos += 6;
    const auto fim = corpo.find('"', pos);
    if (fim == std::string::npos) break;
    ids.push_back(corpo.substr(pos, fim - pos));
    pos = fim + 1;
  }
  if (ids.empty()) return false;
  config.paginas = std::move(ids);
  LOG_DBG("PNL", "catalogo: %u paginas", static_cast<unsigned>(config.paginas.size()));
  return true;
}

void PainelActivity::baixarAgora() {
  LOG_DBG("PNL", "heap antes: %u livre, %u maior bloco", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  int ok = 0;
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_ERR("PNL", "heap baixo para TLS (%u livre, %u maior bloco)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  } else {
    if (config.paginasDoServidor() && !lerCatalogo()) {
      LOG_ERR("PNL", "sem catalogo do servidor; tentando o que ha no cartao");
    }
    for (const auto& pagina : config.paginas) {
      if (painelRede::baixarPagina(config, pagina)) ++ok;
    }
  }
  if (ok > 0) {
    painelRede::copiarDescanso(config);
    leiturasNovas = painelRede::baixarLeituras(config);
  }
  if (paginasGuardadas() > 0) {
    state = PainelState::MOSTRANDO;
    if (paginaAtual >= static_cast<int>(config.paginas.size())) paginaAtual = 0;
  } else {
    state = PainelState::ERRO;
    erro = tr(STR_PAINEL_ERRO);
  }
  LOG_DBG("PNL", "%d de %u paginas; heap depois: %u livre", ok, static_cast<unsigned>(config.paginas.size()),
          ESP.getFreeHeap());
}

void PainelActivity::acao(const char* tecla) {
  if (!wifiConectado()) {
    conectar();  // a rede caiu: volta a conectar e a baixar tudo; a ação fica para a próxima tecla
    return;
  }
  const std::string& pagina = config.paginas[paginaAtual];
  GUI.drawPopup(renderer, tr(STR_PAINEL_ENVIANDO));
  const bool ok = painelRede::baixarPara(
      config, config.url + "/xteink/acao?pagina=" + pagina + "&tecla=" + tecla + "&formato=bmp", pagina);
  if (!ok) {
    GUI.drawPopup(renderer, tr(STR_PAINEL_ACAO_FALHOU));
    delay(1200);
  } else if (pagina == config.descanso) {
    painelRede::copiarDescanso(config);
  }
  requestUpdate();
}

// ---------- navegação ----------

void PainelActivity::mudarPagina(const int delta) {
  const int total = static_cast<int>(config.paginas.size());
  if (total <= 1) return;
  // Pula as páginas que não estão no cartão (download parcial), dando a volta.
  for (int passo = 1; passo < total + 1; ++passo) {
    const int candidata = ((paginaAtual + delta * passo) % total + total) % total;
    if (Storage.exists(caminho(config.paginas[candidata]).c_str())) {
      if (candidata == paginaAtual) return;
      paginaAtual = candidata;
      requestUpdate();
      return;
    }
  }
}

// ---------- desenho ----------

void PainelActivity::renderMensagem(const char* titulo, const char* detalhe) const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, titulo, true, EpdFontFamily::BOLD);
  if (detalhe && detalhe[0]) renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 12, detalhe);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

bool PainelActivity::renderPagina() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  HalFile file;
  if (!Storage.openFileForRead("PNL", caminho(config.paginas[paginaAtual]), file)) return false;
  Bitmap bitmap(file, false, false);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    return false;
  }
  // Mesma colocação do visualizador de BMP: a imagem vem no tamanho da tela em retrato; em paisagem ela
  // é reduzida para caber.
  int x, y;
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    const float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);
    if (ratio > screenRatio) {
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
    }
  } else {
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }
  renderer.clearScreen();
  const bool ok = renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0);
  file.close();
  return ok;
}

void PainelActivity::render(RenderLock&&) {
  switch (state) {
    case PainelState::SEM_CONFIG:
      renderMensagem(tr(STR_PAINEL), tr(STR_PAINEL_SEM_CONFIG));
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      return;
    case PainelState::WIFI:
      return;  // a seleção de Wi-Fi desenha a tela dela
    case PainelState::BAIXANDO:
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_PAINEL_BAIXANDO), true,
                                EpdFontFamily::BOLD);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      return;
    case PainelState::ERRO:
      renderMensagem(tr(STR_PAINEL_ERRO), erro.c_str());
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      return;
    case PainelState::MOSTRANDO:
      if (!renderPagina()) {
        renderMensagem(tr(STR_PAINEL_ERRO), tr(STR_FILE_OPEN_FAILED));
      }
      // HALF: um passe limpo por página, sem o pisca do refresh completo (como a tela de descanso).
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      if (leiturasNovas > 0) {
        char aviso[48];
        snprintf(aviso, sizeof(aviso), "%d %s", leiturasNovas, tr(STR_PAINEL_LEITURAS_NOVAS));
        GUI.drawPopup(renderer, aviso);
        leiturasNovas = 0;
      }
      return;
  }
}
