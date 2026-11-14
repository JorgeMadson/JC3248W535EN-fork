#pragma once

/**
 * Abre o Freedoom (contexto LVGL / sob bsp_display_lock). Precisa do freedoom1.wad na raiz do cartão.
 * Depois que o jogo começa não há volta: "Sair do jogo" reinicia a placa.
 */
void doom_ui_open(void);
