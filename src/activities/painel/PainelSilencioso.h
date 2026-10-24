#pragma once

#include <cstdint>

/**
 * O despertar por timer do Painel. Com `atualizar_min=N` no /painel.txt, o aparelho dorme segurando a
 * bateria e acorda a cada N minutos sem tela nenhuma: conecta no Wi-Fi salvo, baixa a página de descanso,
 * grava o /sleep.bmp e volta a dormir; quem chama desenha o descanso novo ao dormir. Com N = 0 (padrão)
 * nada muda: o aparelho desliga de verdade, como o CrossPoint faz.
 */

// Segundos do timer configurado, ou 0.
uint32_t painelTimerSegundos();

// A atualização sem tela. Verdadeiro quando a página de descanso foi renovada.
bool painelAtualizarSilencioso();
