#pragma once

#include <cstdint>
#include <ctime>

struct PainelConfig;

/**
 * O despertar por timer do Painel. Com `atualizar_min=N` no /painel.txt, o aparelho dorme segurando a
 * bateria e acorda a cada N minutos sem tela nenhuma: conecta no Wi-Fi salvo, baixa a página de descanso,
 * grava o /sleep.bmp e volta a dormir; quem chama desenha o descanso novo ao dormir. Com N = 0 (padrão)
 * nada muda: o aparelho desliga de verdade, como o CrossPoint faz.
 * Com `silencio=2-7` o timer pula essas horas: um despertar que cairia dentro da janela é adiado para o fim
 * dela. Vale a hora local do CrossPoint (fuso das Configurações); sem relógio acertado, a janela é ignorada.
 */

// `agora` está dentro da janela de silêncio? Com `fim`, devolve nele o instante em que a janela termina.
bool painelEmSilencio(const PainelConfig& config, time_t agora, time_t* fim);

// Segundos até o próximo despertar (o intervalo, ou até o fim do silêncio), ou 0 sem timer.
uint32_t painelTimerSegundos();

// A atualização sem tela. Verdadeiro quando a página de descanso foi renovada.
bool painelAtualizarSilencioso();
