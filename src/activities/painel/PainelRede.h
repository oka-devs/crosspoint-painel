#pragma once

#include <string>

#include "PainelConfig.h"
#include "network/HttpDownloader.h"

/**
 * O que o Painel faz pela rede, fora de qualquer Activity, para servir tanto a tela interativa quanto a
 * atualização silenciosa do despertar por timer.
 */
namespace painelRede {

// Cabeçalhos de toda chamada ao servidor: o token do aparelho e, se houver, o token de serviço do Access.
HttpDownloader::Headers cabecalhos(const PainelConfig& config);

// O valor texto da chave JSON `"chave":"valor"` a partir de `pos`, em `out`. Devolve a posição logo depois do
// valor, ou npos quando a chave não aparece (varredura de texto: os JSONs do servidor são pequenos e planos).
size_t valorTexto(const std::string& json, const char* chave, size_t pos, std::string& out);

// /.painel/<pagina>.bmp
std::string caminhoPagina(const std::string& pagina);

// Baixa `url` para o arquivo da página, por um .tmp, para a versão anterior sobreviver a uma queda.
bool baixarPara(const PainelConfig& config, const std::string& url, const std::string& pagina);

// GET /xteink/tela da página.
bool baixarPagina(const PainelConfig& config, const std::string& pagina);

// Copia a página de descanso para /sleep.bmp e liga o modo de descanso personalizado.
void copiarDescanso(const PainelConfig& config);

// GET /xteink/leituras e baixa para /Leituras/ os EPUBs que o cartão não tem. Devolve quantos baixou
// (ou -1 se a lista não veio).
int baixarLeituras(const PainelConfig& config);

// Conecta no último Wi-Fi salvo sem interface, esperando até `timeoutMs`. Usa o WifiCredentialStore.
bool conectarSalvo(unsigned long timeoutMs = 20000);

}  // namespace painelRede
