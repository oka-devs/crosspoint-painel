#include "PainelConfig.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdlib>

namespace {
constexpr size_t TAMANHO_MAXIMO = 2048;  // o arquivo é pequeno: endereço, token e meia dúzia de chaves

std::string aparar(const std::string& texto) {
  const auto inicio = texto.find_first_not_of(" \t\r\n");
  if (inicio == std::string::npos) return {};
  const auto fim = texto.find_last_not_of(" \t\r\n");
  return texto.substr(inicio, fim - inicio + 1);
}

bool lerArquivo(std::string& conteudo) {
  HalFile file;
  if (!Storage.openFileForRead("PNL", PainelConfig::ARQUIVO, file)) return false;
  const size_t tamanho = file.fileSize();
  if (tamanho == 0 || tamanho > TAMANHO_MAXIMO) {
    LOG_ERR("PNL", "%s com tamanho fora do esperado: %u", PainelConfig::ARQUIVO, static_cast<unsigned>(tamanho));
    file.close();
    return false;
  }
  conteudo.resize(tamanho);
  const int lidos = file.read(&conteudo[0], tamanho);
  file.close();
  if (lidos < 0) return false;
  conteudo.resize(static_cast<size_t>(lidos));
  return true;
}
}  // namespace

bool PainelConfig::carregar() {
  std::string conteudo;
  if (!lerArquivo(conteudo)) return false;

  paginas.clear();
  size_t pos = 0;
  while (pos <= conteudo.size()) {
    const auto fim = conteudo.find('\n', pos);
    const std::string linha = aparar(conteudo.substr(pos, fim == std::string::npos ? std::string::npos : fim - pos));
    pos = fim == std::string::npos ? conteudo.size() + 1 : fim + 1;
    if (linha.empty() || linha[0] == '#') continue;
    const auto igual = linha.find('=');
    if (igual == std::string::npos) continue;
    const std::string chave = aparar(linha.substr(0, igual));
    const std::string valor = aparar(linha.substr(igual + 1));
    if (chave == "url") {
      url = valor;
      while (!url.empty() && url.back() == '/') url.pop_back();
    } else if (chave == "token") {
      token = valor;
    } else if (chave == "cf_id") {
      cfId = valor;
    } else if (chave == "cf_secret") {
      cfSecret = valor;
    } else if (chave == "descanso") {
      descanso = valor;
    } else if (chave == "atualizar_min") {
      atualizarMin = atoi(valor.c_str());
      if (atualizarMin < 0) atualizarMin = 0;
      if (atualizarMin > 0 && atualizarMin < 5) atualizarMin = 5;   // menos que isso só gasta bateria
    } else if (chave == "silencio") {
      const auto traco = valor.find('-');
      if (traco != std::string::npos) {
        const int de = atoi(valor.substr(0, traco).c_str());
        const int ate = atoi(valor.substr(traco + 1).c_str());
        if (de >= 0 && de <= 23 && ate >= 0 && ate <= 23 && de != ate) {
          silencioDe = de;
          silencioAte = ate;
        } else {
          LOG_ERR("PNL", "silencio=%s ignorado (use horas de 0 a 23, ex. 2-7)", valor.c_str());
        }
      }
    } else if (chave == "abrir_ao_ligar") {
      abrirAoLigar = valor == "1" || valor == "sim" || valor == "true";
    } else if (chave == "paginas") {
      size_t p = 0;
      while (p <= valor.size()) {
        const auto virgula = valor.find(',', p);
        const std::string nome = aparar(valor.substr(p, virgula == std::string::npos ? std::string::npos : virgula - p));
        if (!nome.empty()) paginas.push_back(nome);
        if (virgula == std::string::npos) break;
        p = virgula + 1;
      }
    }
  }
  if (!valida()) LOG_ERR("PNL", "%s sem url ou token", ARQUIVO);
  return valida();
}

bool PainelConfig::abrirAoLigarConfigurado() {
  if (!Storage.exists(ARQUIVO)) return false;
  PainelConfig config;
  return config.carregar() && config.abrirAoLigar;
}
