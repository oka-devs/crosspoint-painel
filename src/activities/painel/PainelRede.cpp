#include "PainelRede.h"

#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <ctime>
#include <vector>

#include "CrossPointSettings.h"
#include "WifiCredentialStore.h"

namespace {
constexpr char DESCANSO_BMP[] = "/sleep.bmp";
constexpr char PASTA_LEITURAS[] = "/Leituras";
constexpr size_t COPIA_BUFFER = 2048;
constexpr int MAX_LEITURAS_POR_VEZ = 5;
constexpr time_t RELOGIO_MINIMO = 1700000000;  // nov/2023: qualquer hora antes disso é o 1970 do boot frio

size_t valorNumero(const std::string& json, const char* chave, size_t pos, long& out) {
  const std::string marca = std::string("\"") + chave + "\":";
  pos = json.find(marca, pos);
  if (pos == std::string::npos) return pos;
  pos += marca.size();
  while (pos < json.size() && json[pos] == ' ') ++pos;
  out = strtol(json.c_str() + pos, nullptr, 10);
  return pos;
}
}  // namespace

namespace painelRede {

size_t valorTexto(const std::string& json, const char* chave, size_t pos, std::string& out) {
  const std::string marca = std::string("\"") + chave + "\":";
  pos = json.find(marca, pos);
  if (pos == std::string::npos) return pos;
  pos += marca.size();
  while (pos < json.size() && json[pos] == ' ') ++pos;
  if (pos >= json.size() || json[pos] != '"') return std::string::npos;
  const auto fim = json.find('"', pos + 1);
  if (fim == std::string::npos) return fim;
  out = json.substr(pos + 1, fim - pos - 1);
  return fim + 1;
}

HttpDownloader::Headers cabecalhos(const PainelConfig& config) {
  HttpDownloader::Headers headers = {{"Authorization", "Bearer " + config.token}};
  if (!config.cfId.empty() && !config.cfSecret.empty()) {
    headers.emplace_back("CF-Access-Client-Id", config.cfId);
    headers.emplace_back("CF-Access-Client-Secret", config.cfSecret);
  }
  return headers;
}

std::string sufixoBateria() { return "&bateria=" + std::to_string(powerManager.getBatteryPercentage()); }

std::string caminhoPagina(const std::string& pagina) {
  return std::string(PainelConfig::PASTA) + "/" + pagina + ".bmp";
}

bool baixarPara(const PainelConfig& config, const std::string& url, const std::string& pagina) {
  const std::string destino = caminhoPagina(pagina);
  const std::string temporario = destino + ".tmp";
  const auto resultado = HttpDownloader::downloadToFile(url, temporario, cabecalhos(config));
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

bool baixarPagina(const PainelConfig& config, const std::string& pagina) {
  return baixarPara(config, config.url + "/xteink/tela?pagina=" + pagina + "&formato=bmp" + sufixoBateria(), pagina);
}

void copiarDescanso(const PainelConfig& config) {
  if (config.descanso.empty()) return;
  const std::string origem = caminhoPagina(config.descanso);
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

int baixarLeituras(const PainelConfig& config) {
  std::string lista;
  if (!HttpDownloader::fetchUrl(config.url + "/xteink/leituras", lista, cabecalhos(config))) return -1;
  // Cada item: {"id":N,...,"arquivo":"nome.epub",...}. O JSON é compacto e pequeno; varredura de texto.
  Storage.ensureDirectoryExists(PASTA_LEITURAS);
  int baixadas = 0;
  size_t pos = 0;
  while (baixadas < MAX_LEITURAS_POR_VEZ) {
    long id = 0;
    pos = valorNumero(lista, "id", pos, id);
    if (pos == std::string::npos) break;
    std::string arquivo;
    pos = valorTexto(lista, "arquivo", pos, arquivo);
    if (pos == std::string::npos) break;
    if (arquivo.empty() || arquivo.find('/') != std::string::npos || arquivo.find("..") != std::string::npos) continue;
    const std::string destino = std::string(PASTA_LEITURAS) + "/" + arquivo;
    if (Storage.exists(destino.c_str())) continue;
    const std::string temporario = destino + ".tmp";
    const std::string url = config.url + "/xteink/leituras/" + std::to_string(id) + "/epub";
    if (HttpDownloader::downloadToFile(url, temporario, cabecalhos(config)) != HttpDownloader::OK) {
      LOG_ERR("PNL", "leitura %ld falhou", id);
      continue;
    }
    if (Storage.rename(temporario.c_str(), destino.c_str())) {
      ++baixadas;
      LOG_INF("PNL", "leitura nova: %s", arquivo.c_str());
    }
  }
  return baixadas;
}

bool conectarSalvo(const unsigned long timeoutMs) {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) return true;
  WIFI_STORE.loadFromFile();
  const std::string ssid = WIFI_STORE.getLastConnectedSsid();
  if (ssid.empty()) {
    LOG_ERR("PNL", "sem rede salva");
    return false;
  }
  const auto cred = WIFI_STORE.findCredential(ssid);
  if (!cred) return false;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  if (cred->password.empty()) {
    WiFi.begin(cred->ssid.c_str());
  } else {
    WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  }
  const unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < timeoutMs) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("PNL", "nao conectou em %s", ssid.c_str());
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }
  LOG_INF("PNL", "conectado em %s", ssid.c_str());
  return true;
}

bool relogioValido() { return time(nullptr) > RELOGIO_MINIMO; }

bool acertarRelogio(const bool forcar) {
  if (!forcar && relogioValido()) return true;
  if (WiFi.status() != WL_CONNECTED) return false;
  const char* tz = getenv("TZ");
  configTzTime(tz && tz[0] ? tz : "UTC0", "pool.ntp.org", "time.nist.gov");
  for (int i = 0; i < 40; ++i) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      struct tm local;
      const time_t agora = time(nullptr);
      localtime_r(&agora, &local);
      LOG_INF("PNL", "relogio acertado: %02d:%02d local", local.tm_hour, local.tm_min);
      return true;
    }
    delay(100);
  }
  LOG_ERR("PNL", "SNTP nao respondeu");
  return relogioValido();
}

}  // namespace painelRede
