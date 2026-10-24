#pragma once

#include <string>
#include <vector>

/**
 * Configuração do Painel, lida de /painel.txt na raiz do cartão SD (chave=valor, uma por linha,
 * linhas com # são comentário):
 *
 *   url=https://anyroute.oka.dev.br
 *   token=<ANYROUTE_TOKEN_XTEINK>
 *   cf_id=<CF-Access-Client-Id>          (opcional: só quando o endereço está atrás do Cloudflare Access)
 *   cf_secret=<CF-Access-Client-Secret>
 *   paginas=hoje,parque                  (ordem das páginas; padrão hoje,parque)
 *   descanso=hoje                        (página copiada para /sleep.bmp; vazio desliga)
 *   abrir_ao_ligar=1                     (ligar o aparelho já abre o Painel em vez da tela inicial)
 *
 * O arquivo fica no cartão, fora do firmware, para trocar servidor ou token sem reflashear.
 */
struct PainelConfig {
  std::string url;
  std::string token;
  std::string cfId;
  std::string cfSecret;
  std::vector<std::string> paginas;
  std::string descanso = "hoje";
  bool abrirAoLigar = false;

  static constexpr const char* ARQUIVO = "/painel.txt";
  static constexpr const char* PASTA = "/.painel";

  // Lê o arquivo. Falso quando não existe ou não tem url e token.
  bool carregar();
  bool valida() const { return !url.empty() && !token.empty() && !paginas.empty(); }

  // Só a pergunta do boot, sem guardar o resto.
  static bool abrirAoLigarConfigurado();
};
