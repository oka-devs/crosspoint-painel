#include "PainelSilencioso.h"

#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>

#include "PainelConfig.h"
#include "PainelRede.h"

uint32_t painelTimerSegundos() {
  if (!Storage.exists(PainelConfig::ARQUIVO)) return 0;
  PainelConfig config;
  if (!config.carregar() || config.atualizarMin <= 0) return 0;
  return static_cast<uint32_t>(config.atualizarMin) * 60U;
}

bool painelAtualizarSilencioso() {
  PainelConfig config;
  if (!Storage.exists(PainelConfig::ARQUIVO) || !config.carregar() || config.descanso.empty()) return false;
  LOG_INF("PNL", "atualizacao silenciosa: heap %u", ESP.getFreeHeap());
  if (!painelRede::conectarSalvo()) return false;
  Storage.ensureDirectoryExists(PainelConfig::PASTA);
  const bool ok = painelRede::baixarPagina(config, config.descanso);
  if (ok) painelRede::copiarDescanso(config);
  // As leituras também entram aqui: é o momento em que o aparelho está na rede sem ninguém olhando.
  painelRede::baixarLeituras(config);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  LOG_INF("PNL", "atualizacao silenciosa: %s", ok ? "ok" : "falhou");
  return ok;
}
