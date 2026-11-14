#include "doom_ui.h"

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <string.h>
#include <sys/stat.h>

#include <vector>

#include "audio_player.h"
#include "config_placa.h"
#include "esp_bsp.h"
#include "home_ui.h"
#include "lv_port.h"
#include "sd_card.h"
#include "serial_log.h"
#include "ui_font.h"

extern "C" {
#include "placa.h"
void doomgeneric_Create(int argc, char **argv);
void doomgeneric_Tick(void);
}

/*
 * Freedoom (motor e código comum em JC3248W535/freedoom) como app do launcher.
 *
 * O jogo roda numa tarefa própria no núcleo 1. A tarefa de vídeo do código comum
 * (núcleo 0) monta cada quadro deitado (480x320, formato do LVGL) e manda direto
 * para o painel com lvgl_port_flush_frame, como o player de vídeo: o LVGL só
 * desenha a tela de "Carregando" e as mensagens.
 *
 * O motor do Doom não sabe parar e recomeçar, então não há volta para o
 * launcher: "Sair do jogo" no menu do Doom reinicia a placa.
 *
 * O jogo vem do cartão (freedoom1.wad na raiz). Saves e default.cfg ficam em
 * /freedoom/ no cartão.
 */

namespace {

constexpr const char *k_pasta = "/sd/freedoom/";
constexpr size_t k_reserva_psram = 1024 * 1024;  // fica para o launcher (Wi-Fi, LVGL...)
constexpr int k_zona_min_mb = 3;
constexpr int k_zona_max_mb = 6;

lv_obj_t *s_scr = nullptr;
lv_obj_t *s_rotulo = nullptr;  // "Carregando..." / última linha do motor
lv_timer_t *s_timer = nullptr;
uint16_t *s_fb = nullptr;
bool s_iniciado = false;
volatile bool s_erro = false;
volatile bool s_jogando = false;  // primeiro quadro já foi para a tela

char s_wad[64];
char s_arg_mb[4];

// Zona do Doom: o primeiro pedaço é o -mb (malloc do próprio motor); os outros
// vêm de DG_ZonaExtra, até somar s_zona_alvo (a PSRAM costuma estar picada).
constexpr size_t k_mb = 1024 * 1024;
constexpr size_t k_pedaco_min = 256 * 1024;
size_t s_zona_alvo = 0;
size_t s_zona_dada = 0;

// Última linha impressa pelo motor (tarefa do Doom escreve, timer do LVGL lê).
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
char s_linha[120] = "";
bool s_linha_nova = false;

// Contornos e rótulos dos botões de toque, desenhados uma vez com o LVGL e
// guardados como lista de pixels (0 = menu, 1 = jogo).
struct Pixel {
    uint32_t pos;
    uint16_t cor;
};
std::vector<Pixel> s_botoes[2];

lv_indev_t *indev_toque() {
    return lv_indev_get_next(nullptr);
}

// ---------------------------------------------------------------- telas LVGL

void voltar_cb(lv_event_t *) {
    home_ui_show();
}

void reiniciar_cb(lv_event_t *) {
    esp_restart();
}

/** Mensagem com um botão. Chamar com o LVGL travado. */
void mostrar_mensagem(const char *titulo, const char *texto, const char *botao, lv_event_cb_t acao) {
    serial_log("[doom] aviso: %s | %s", titulo, texto);
    lv_obj_clean(s_scr);
    s_rotulo = nullptr;

    lv_obj_t *t = lv_label_create(s_scr);
    lv_obj_set_style_text_font(t, ui_font_get(), 0);
    lv_obj_set_style_text_color(t, lv_palette_main(LV_PALETTE_RED), 0);
    lv_label_set_text(t, titulo);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *m = lv_label_create(s_scr);
    lv_obj_set_width(m, 440);
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(m, ui_font_get(), 0);
    lv_obj_set_style_text_color(m, lv_color_white(), 0);
    lv_label_set_text(m, texto);
    lv_obj_align(m, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t *b = lv_btn_create(s_scr);
    lv_obj_set_size(b, 160, 48);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_event_cb(b, acao, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *bl = lv_label_create(b);
    lv_obj_set_style_text_font(bl, ui_font_get(), 0);
    lv_label_set_text(bl, botao);
    lv_obj_center(bl);
}

void timer_cb(lv_timer_t *) {
    if (!s_rotulo) return;
    char copia[sizeof(s_linha)];
    bool nova;
    portENTER_CRITICAL(&s_mux);
    nova = s_linha_nova;
    s_linha_nova = false;
    memcpy(copia, s_linha, sizeof(copia));
    portEXIT_CRITICAL(&s_mux);
    if (nova) lv_label_set_text(s_rotulo, copia);
}

// ---------------------------------------------------------------- botões de toque

void retangulo(lv_obj_t *c, int x, int y, int w, int h, int raio, const char *rotulo) {
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_opa = LV_OPA_TRANSP;
    r.border_color = lv_color_make(0x50, 0x50, 0x50);
    r.border_width = 2;
    r.border_opa = LV_OPA_COVER;
    r.radius = raio;
    lv_canvas_draw_rect(c, x, y, w, h, &r);

    lv_draw_label_dsc_t l;
    lv_draw_label_dsc_init(&l);
    l.color = lv_color_make(0x70, 0x70, 0x70);
    l.font = ui_font_get();
    l.align = LV_TEXT_ALIGN_CENTER;
    const int altura = lv_font_get_line_height(l.font);
    lv_canvas_draw_text(c, x, y + (h - altura) / 2, w, &l, rotulo);
}

/** Desenha as duas versões dos botões e guarda os pixels acesos. Chamar com o LVGL travado. */
bool montar_botoes() {
#if MOSTRAR_BOTOES
    const size_t n = size_t(FB_LARGURA) * FB_ALTURA;
    lv_color_t *tmp = static_cast<lv_color_t *>(heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_SPIRAM));
    if (!tmp) return false;
    lv_obj_t *c = lv_canvas_create(s_scr);
    lv_canvas_set_buffer(c, tmp, FB_LARGURA, FB_ALTURA, LV_IMG_CF_TRUE_COLOR);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);

    for (int modo = 0; modo < 2; modo++) {
        lv_canvas_fill_bg(c, lv_color_black(), LV_OPA_COVER);
        // Mesmas áreas de comum/entrada.c
        retangulo(c, 2, 2, 76, 60, 8, "MENU");
        if (modo == 1) {
            retangulo(c, 402, 2, 76, 60, 8, "MAPA");
            retangulo(c, 244, 68, 114, 78, 8, "ARMA");
            retangulo(c, 362, 68, 116, 78, 8, "USAR");
            retangulo(c, 244, 150, 234, 168, 8, "ATIRAR");
            retangulo(c, 70, 150, 100, 100, LV_RADIUS_CIRCLE, "ANDAR");
        } else {
            retangulo(c, 244, 150, 234, 168, 8, "OK");
            retangulo(c, 70, 150, 100, 100, LV_RADIUS_CIRCLE, "MOVER");
        }
        std::vector<Pixel> &lista = s_botoes[modo];
        lista.clear();
        for (size_t i = 0; i < n; i++) {
            if (tmp[i].full != 0) lista.push_back({uint32_t(i), tmp[i].full});
        }
        lista.shrink_to_fit();
    }
    lv_obj_del(c);
    heap_caps_free(tmp);
#endif
    return true;
}

// ---------------------------------------------------------------- jogo

void tarefa_doom(void *) {
    static char *args[] = {(char *)"doom", (char *)"-iwad", s_wad, (char *)"-mb", s_arg_mb, nullptr};
    doomgeneric_Create(5, args);
    for (;;) {
        doomgeneric_Tick();
        // Deixa o loop() do launcher e a tarefa ociosa do núcleo 1 rodarem
        // mesmo quando o jogo não consegue acompanhar os 35 quadros/s.
        vTaskDelay(1);
    }
}

bool achar_wad() {
    static const char *nomes[] = {
        "freedoom1.wad", "freedoom/freedoom1.wad", "freedoom2.wad", "freedoom/freedoom2.wad",
        "doom.wad",      "doom1.wad",              "doom2.wad",
    };
    struct stat st;
    for (const char *nome : nomes) {
        snprintf(s_wad, sizeof(s_wad), "/sd/%s", nome);
        if (stat(s_wad, &st) == 0 && st.st_size > 0) return true;
    }
    s_wad[0] = '\0';
    return false;
}

}  // namespace

// ---------------------------------------------------------------- chamado pelo código comum

extern "C" {

/**
 * O motor anuncia cada etapa com uma linha ("M_Init: ..."): vira "Carregando... (etapa)".
 * A etapa aparece quando começa, então a demorada (texturas, ~10 s) fica na tela enquanto dura.
 * Linhas que não são etapa não mudam a tela.
 */
void doom_ui_linha(const char *linha) {
    static const struct {
        const char *prefixo;
        const char *etapa;
    } k_etapas[] = {
        {"Z_Init", "memória"},          {"M_LoadDefaults", "configurações"}, {"W_Init", "lendo o jogo do cartão"},
        {"I_Init", "som"},              {"M_Init", "texturas"},              {"P_Init", "fases"},
        {"S_Init", "efeitos sonoros"},  {"HU_Init", "quase pronto"},
    };
    for (const auto &e : k_etapas) {
        if (strncmp(linha, e.prefixo, strlen(e.prefixo)) != 0) continue;
        portENTER_CRITICAL(&s_mux);
        snprintf(s_linha, sizeof(s_linha), "Carregando... (%s)", e.etapa);
        s_linha_nova = true;
        portEXIT_CRITICAL(&s_mux);
        return;
    }
}

/** Chamado pelo motor (z_zone.c) logo depois do primeiro pedaço: entrega os outros. */
uint8_t *DG_ZonaExtra(int *tamanho) {
    if (s_zona_dada >= s_zona_alvo) return nullptr;
    size_t quer = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (quer > s_zona_alvo - s_zona_dada) quer = s_zona_alvo - s_zona_dada;
    if (quer < k_pedaco_min) return nullptr;
    auto *p = static_cast<uint8_t *>(heap_caps_malloc(quer, MALLOC_CAP_SPIRAM));
    if (!p) return nullptr;
    s_zona_dada += quer;
    serial_log("[doom] pedaço extra da zona: %u KB", unsigned(quer / 1024));
    *tamanho = int(quer);
    return p;
}

const char *DG_PastaDados(void) {
    return k_pasta;
}

uint16_t *placa_fb(void) {
    return s_fb;
}

void placa_enviar(void) {
    if (s_erro) return;
    bsp_display_lock(0);
    if (lv_scr_act() == s_scr) {
        if (!s_jogando) {
            // Primeiro quadro: tira o "Carregando" para o LVGL não desenhar mais nada aqui.
            s_jogando = true;
            lv_obj_clean(s_scr);
            s_rotulo = nullptr;
        }
        lvgl_port_flush_frame(lv_disp_get_default(), reinterpret_cast<const lv_color_t *>(s_fb));
    }
    bsp_display_unlock();
}

void placa_desenhar_botoes(int modo) {
    if (modo < 0 || modo > 1) return;
    for (const Pixel &p : s_botoes[modo]) s_fb[p.pos] = p.cor;
}

void placa_console_fim(void) {}

void placa_tela_erro(const char *titulo, const char *msg) {
    s_erro = true;
    bsp_display_lock(0);
    lv_indev_t *t = indev_toque();
    if (t) lv_indev_enable(t, true);
    char texto[400];
    snprintf(texto, sizeof(texto), "%s\n\nA placa precisa reiniciar para voltar ao launcher.", msg);
    mostrar_mensagem(titulo, texto, "Reiniciar", reiniciar_cb);
    lv_obj_invalidate(s_scr);
    bsp_display_unlock();
}

int placa_ler_toques(int16_t x[2], int16_t y[2]) {
    esp_lcd_touch_handle_t tp = bsp_touch_handle();
    if (!tp) return 0;
    uint16_t tx[2], ty[2];
    uint8_t n = 0;
    // O LVGL também usa o toque: a trava evita duas leituras I2C ao mesmo tempo.
    if (!bsp_display_lock(20)) return 0;
    esp_lcd_touch_read_data(tp);
    bool ok = esp_lcd_touch_get_coordinates(tp, tx, ty, nullptr, &n, 2);
    bsp_display_unlock();
    if (!ok) return 0;
    int achados = 0;
    for (int i = 0; i < n && i < 2; i++) {
        // já vem girado para a tela deitada (bsp_touch_process_points_cb)
        if (tx[i] >= TELA_LARGURA || ty[i] >= TELA_ALTURA) continue;
        x[achados] = int16_t(tx[i]);
        y[achados] = int16_t(ty[i]);
        achados++;
    }
    return achados;
}

int placa_som_iniciar(int taxa_hz) {
    return audio_pcm_begin(uint32_t(taxa_hz)) ? 1 : 0;
}

void placa_som_escrever(const int16_t *estereo, int quadros) {
    audio_pcm_write(estereo, size_t(quadros));
}

uint32_t placa_millis(void) {
    return uint32_t(esp_timer_get_time() / 1000);
}

void placa_reiniciar(void) {
    esp_restart();
}

}  // extern "C"

// ---------------------------------------------------------------- abrir

void doom_ui_open(void) {
    if (s_scr && s_iniciado) {  // só acontece numa tela de erro: o jogo não volta
        lv_scr_load(s_scr);
        return;
    }
    if (!s_scr) {
        s_scr = lv_obj_create(nullptr);
        lv_obj_remove_style_all(s_scr);
        lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
        lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    }

    serial_log("[doom] abrindo: cartão %s, PSRAM livre %u KB (maior bloco %u KB)", sd_card_mounted() ? "ok" : "ausente",
               unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
               unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
    if (!sd_card_mounted() || !achar_wad()) {
        mostrar_mensagem("Freedoom: jogo não encontrado",
                         "Copie o arquivo freedoom1.wad para a raiz do cartão microSD.\n\n"
                         "Ele vem no zip do Freedoom (freedoom.github.io), na pasta freedoom-0.13.0. "
                         "Os jogos salvos ficam na pasta /freedoom do cartão.",
                         "Voltar", voltar_cb);
        lv_scr_load(s_scr);
        return;
    }

    // Memória: a imagem da tela e o que sobrar (menos uma reserva) para o Doom.
    const size_t bytes_fb = size_t(FB_LARGURA) * FB_ALTURA * sizeof(uint16_t);
    if (!s_fb) s_fb = static_cast<uint16_t *>(heap_caps_malloc(bytes_fb, MALLOC_CAP_SPIRAM));
    lv_obj_clean(s_scr);
    if (!s_fb || !montar_botoes()) {
        mostrar_mensagem("Freedoom", "Sem memória para a tela do jogo.", "Voltar", voltar_cb);
        lv_scr_load(s_scr);
        return;
    }
    // O total livre conta, não o maior bloco: a zona do motor junta vários pedaços.
    const size_t livre = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t maior = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    size_t alvo = livre > k_reserva_psram ? livre - k_reserva_psram : 0;
    if (alvo > k_zona_max_mb * k_mb) alvo = k_zona_max_mb * k_mb;
    if (alvo < k_zona_min_mb * k_mb) {
        char texto[160];
        snprintf(texto, sizeof(texto),
                 "O Doom precisa de %d MB livres e só há %u KB.\nReinicie a placa e abra o Freedoom primeiro.",
                 k_zona_min_mb, unsigned(alvo / 1024));
        mostrar_mensagem("Freedoom: pouca memória", texto, "Voltar", voltar_cb);
        lv_scr_load(s_scr);
        return;
    }
    int mb = int(maior / k_mb);  // primeiro pedaço: o que couber no maior bloco
    if (mb < 1) mb = 1;
    if (size_t(mb) * k_mb > alvo) mb = int(alvo / k_mb);
    s_zona_alvo = alvo;
    s_zona_dada = size_t(mb) * k_mb;
    snprintf(s_arg_mb, sizeof(s_arg_mb), "%d", mb);

    // Carregar do cartão leva uns 15 s (as texturas são lidas aos poucos). O título fica fixo
    // e a última linha do motor aparece embaixo, para não parecer travado.
    s_rotulo = lv_label_create(s_scr);
    lv_obj_set_width(s_rotulo, 460);
    lv_obj_set_style_text_font(s_rotulo, ui_font_get(), 0);
    lv_obj_set_style_text_color(s_rotulo, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_rotulo, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_rotulo, "Carregando... (abrindo o jogo)");
    lv_obj_align(s_rotulo, LV_ALIGN_CENTER, 0, -16);

    lv_obj_t *aviso = lv_label_create(s_scr);
    lv_obj_set_style_text_font(aviso, ui_font_get(), 0);
    lv_obj_set_style_text_color(aviso, lv_color_make(0x80, 0x80, 0x80), 0);
    lv_label_set_text(aviso, "leva uns 15 segundos");
    lv_obj_align(aviso, LV_ALIGN_CENTER, 0, 24);
    if (!s_timer) s_timer = lv_timer_create(timer_cb, 100, nullptr);

    // Daqui em diante o toque é lido pelo jogo, não pelo LVGL.
    lv_indev_t *t = indev_toque();
    if (t) lv_indev_enable(t, false);
    lv_scr_load(s_scr);

    serial_log("[doom] %s, zona de %u KB: %d MB no primeiro pedaço (PSRAM livre %u KB, maior bloco %u KB)", s_wad,
               unsigned(alvo / 1024), mb, unsigned(livre / 1024), unsigned(maior / 1024));
    s_iniciado = true;
    // Pilha na RAM interna se der (mais rápida); senão na PSRAM (o jogo não grava na flash).
    if (xTaskCreatePinnedToCore(tarefa_doom, "doom", 32 * 1024, nullptr, 1, nullptr, 1) != pdPASS &&
        xTaskCreatePinnedToCoreWithCaps(tarefa_doom, "doom", 32 * 1024, nullptr, 1, nullptr, 1, MALLOC_CAP_SPIRAM) !=
            pdPASS) {
        s_iniciado = false;
        if (t) lv_indev_enable(t, true);
        mostrar_mensagem("Freedoom", "Não deu para iniciar o jogo (memória).", "Voltar", voltar_cb);
    }
}
