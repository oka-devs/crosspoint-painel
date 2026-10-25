#include "PainelSilencioso.h"

#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>

#include "PainelConfig.h"
#include "PainelRede.h"

bool painelEmSilencio(const PainelConfig& config, const time_t agora, time_t* fim) {
  if (!config.temSilencio()) return false;
  struct tm local;
  localtime_r(&agora, &local);
  const int h = local.tm_hour;
  const bool cruzaMeiaNoite = config.silencioDe > config.silencioAte;
  const bool dentro = cruzaMeiaNoite ? (h >= config.silencioDe || h < config.silencioAte)
                                     : (h >= config.silencioDe && h < config.silencioAte);
  if (!dentro) return false;
  if (fim) {
    struct tm t = local;
    t.tm_hour = config.silencioAte;
    t.tm_min = 0;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    if (cruzaMeiaNoite && h >= config.silencioDe) t.tm_mday += 1;  // a janela termina amanhã
    *fim = mktime(&t);
  }
  return true;
}

uint32_t painelTimerSegundos() {
  if (!Storage.exists(PainelConfig::ARQUIVO)) return 0;
  PainelConfig config;
  if (!config.carregar() || config.atualizarMin <= 0) return 0;
  const uint32_t intervalo = static_cast<uint32_t>(config.atualizarMin) * 60U;
  if (!config.temSilencio() || !painelRede::relogioValido()) return intervalo;
  const time_t agora = time(nullptr);
  time_t fim = 0;
  if (painelEmSilencio(config, agora + static_cast<time_t>(intervalo), &fim) && fim > agora) {
    LOG_INF("PNL", "despertar cairia no silencio; dormindo %ld s ate o fim", static_cast<long>(fim - agora));
    return static_cast<uint32_t>(std::max<time_t>(60, fim - agora));
  }
  return intervalo;
}

bool painelAtualizarSilencioso() {
  PainelConfig config;
  if (!Storage.exists(PainelConfig::ARQUIVO) || !config.carregar() || config.descanso.empty()) return false;
  if (painelRede::relogioValido() && painelEmSilencio(config, time(nullptr), nullptr)) {
    LOG_INF("PNL", "silencio: sem atualizar agora");  // o relogio derivou e acordou cedo: volta a dormir
    return false;
  }
  LOG_INF("PNL", "atualizacao silenciosa: heap %u", ESP.getFreeHeap());
  if (!painelRede::conectarSalvo()) return false;
  painelRede::acertarRelogio(config.temSilencio());  // com janela, acerta sempre: o deep sleep deriva
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
