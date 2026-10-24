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
#include "network/HttpDownloader.h"

namespace {
constexpr char DESCANSO_BMP[] = "/sleep.bmp";
constexpr size_t COPIA_BUFFER = 2048;

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

std::string PainelActivity::caminho(const std::string& pagina) const {
  return std::string(PainelConfig::PASTA) + "/" + pagina + ".bmp";
}

int PainelActivity::paginasGuardadas() const {
  int n = 0;
  for (const auto& p : config.paginas) {
    if (Storage.exists(caminho(p).c_str())) ++n;
  }
  return n;
}

bool PainelActivity::baixarPagina(const std::string& pagina) {
  return baixarPara(config.url + "/xteink/tela?pagina=" + pagina + "&formato=bmp", pagina);
}

bool PainelActivity::baixarPara(const std::string& url, const std::string& pagina) {
  const std::string destino = caminho(pagina);
  const std::string temporario = destino + ".tmp";
  HttpDownloader::Headers headers = {{"Authorization", "Bearer " + config.token}};
  if (!config.cfId.empty() && !config.cfSecret.empty()) {
    headers.emplace_back("CF-Access-Client-Id", config.cfId);
    headers.emplace_back("CF-Access-Client-Secret", config.cfSecret);
  }
  // Baixa num .tmp para a página anterior sobreviver a uma queda de rede no meio.
  const auto resultado = HttpDownloader::downloadToFile(url, temporario, headers);
  if (resultado != HttpDownloader::OK) {
    LOG_ERR("PNL", "falhou %s (%d)", pagina.c_str(), static_cast<int>(resultado));
    return false;
  }
  if (Storage.exists(destino.c_str())) Storage.remove(destino.c_str());
  if (!Storage.rename(temporario.c_str(), destino.c_str())) {
    LOG_ERR("PNL", "nao renomeou %s", temporario.c_str());
    return false;
  }
  return true;
}

void PainelActivity::baixarAgora() {
  LOG_DBG("PNL", "heap antes: %u livre, %u maior bloco", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  int ok = 0;
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_ERR("PNL", "heap baixo para TLS (%u livre, %u maior bloco)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  } else {
    for (const auto& pagina : config.paginas) {
      if (baixarPagina(pagina)) ++ok;
    }
  }
  if (ok > 0) copiarDescanso();
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

void PainelActivity::copiarDescanso() const {
  if (config.descanso.empty()) return;
  const std::string origem = caminho(config.descanso);
  if (!Storage.exists(origem.c_str())) return;
  auto buffer = makeUniqueNoThrow<uint8_t[]>(COPIA_BUFFER);
  if (!buffer) {
    LOG_ERR("PNL", "OOM: buffer de copia do descanso");
    return;
  }
  HalFile entrada, saida;
  if (!Storage.openFileForRead("PNL", origem, entrada)) return;
  if (!Storage.openFileForWrite("PNL", DESCANSO_BMP, saida)) {
    entrada.close();
    return;
  }
  bool sucesso = true;
  int lidos;
  while ((lidos = entrada.read(buffer.get(), COPIA_BUFFER)) > 0) {
    if (saida.write(buffer.get(), static_cast<size_t>(lidos)) != static_cast<size_t>(lidos)) {
      sucesso = false;
      break;
    }
  }
  if (lidos < 0) sucesso = false;
  saida.close();
  entrada.close();
  if (!sucesso) {
    LOG_ERR("PNL", "copia do descanso falhou");
    return;
  }
  // O /sleep.bmp só aparece nos modos que o leem; sem mexer em quem já escolheu "capa + custom".
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM &&
      SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM) {
    SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    SETTINGS.saveToFile();
  }
}

void PainelActivity::acao(const char* tecla) {
  if (!wifiConectado()) {
    conectar();  // a rede caiu: volta a conectar e a baixar tudo; a ação fica para a próxima tecla
    return;
  }
  const std::string& pagina = config.paginas[paginaAtual];
  GUI.drawPopup(renderer, tr(STR_PAINEL_ENVIANDO));
  const bool ok = baixarPara(config.url + "/xteink/acao?pagina=" + pagina + "&tecla=" + tecla + "&formato=bmp", pagina);
  if (!ok) {
    GUI.drawPopup(renderer, tr(STR_PAINEL_ACAO_FALHOU));
    delay(1200);
  } else if (pagina == config.descanso) {
    copiarDescanso();
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
      return;
  }
}
