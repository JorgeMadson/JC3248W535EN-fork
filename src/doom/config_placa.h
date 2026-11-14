#pragma once
// Ajustes do Freedoom dentro do launcher (o firmware próprio, Arduino, tem o
// seu em JC3248W535/freedoom/src/config_placa.h). Lido pelo código comum
// (freedoom/src/comum/): vídeo, toque e mixer de som.

// 1 = desenha os contornos dos botões de toque por cima do jogo.
#define MOSTRAR_BOTOES 1

// Som: o mixer entrega 16 bits estéreo para o audio_player do launcher, que
// aplica o volume dos Ajustes. Por isso o ganho daqui fica cheio.
#define SOM_TAXA_HZ 22050
#define SOM_GANHO 256

// Imagem da tela: deitada, do jeito que o LVGL vê (480x320). Quem gira para o
// painel é o lvgl_port_flush_frame. Bytes trocados como o LVGL usa
// (LV_COLOR_16_SWAP em lv_conf.h).
#define FB_LARGURA 480
#define FB_ALTURA 320
#define FB_GIRO 0
#define FB_TROCA_BYTES 1

#define TELA_LARGURA 480
#define TELA_ALTURA 320
