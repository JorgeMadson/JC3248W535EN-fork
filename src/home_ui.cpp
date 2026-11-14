#include "home_ui.h"
#include "home_apps.h"
#include "fontes_pt.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "air_ui.h"
#include "ambient_ui.h"
#include "breakout_ui.h"
#include "placar_ui.h"
#include "clock_ui.h"
#include "doom_ui.h"
#include "files_ui.h"
#include "music_ui.h"
#include "radio_ui.h"
#include "serial_log.h"
#include "app_settings.h"
#include "status_bar.h"
#include "ui_font.h"
#include "ui_theme.h"
#include "weather_ui.h"
#include "wifi_ui.h"
#include "platform_time.h"

static lv_obj_t *s_home_scr = nullptr;
static lv_obj_t *s_bar = nullptr;
static lv_obj_t *s_clock_tile_time = nullptr;
static lv_obj_t *s_weather_tile_temp = nullptr;
static lv_obj_t *s_air_tile_val = nullptr;
static lv_obj_t *s_apps_area = nullptr; /* páginas da grade + pontinhos; refeito por rebuild_apps() */

extern "C" const lv_img_dsc_t icone_freedoom;

typedef void (*app_open_fn)(void);

static void home_btn_cb(lv_event_t *e) {
    LV_UNUSED(e);
    home_ui_show();
}

static void app_tile_cb(lv_event_t *e) {
    app_open_fn open = (app_open_fn)lv_event_get_user_data(e);
    if (open) {
        open();
    }
}

/** Refresh the date title and the live tiles once a second (status bar updates itself). */
static void home_timer_cb(lv_timer_t *t) {
    LV_UNUSED(t);
    if (lv_scr_act() != s_home_scr) {
        return;
    }
    /* Idle on Home for the time set in Settings: ambient screen (retry at most once a minute). */
    static uint32_t s_ambient_try_ms = 0;
    const int idle_min = settings_get().ambient_min;
    if (idle_min > 0 && lv_disp_get_inactive_time(nullptr) >= (uint32_t)idle_min * 60000U &&
        (s_ambient_try_ms == 0 || now_ms() - s_ambient_try_ms > 60000)) {
        s_ambient_try_ms = now_ms();
        if (ambient_ui_open()) {
            return;
        }
    }
    if (s_weather_tile_temp) {
        lv_label_set_text(s_weather_tile_temp, weather_ui_tile_text());
    }
    if (s_air_tile_val) {
        lv_color_t air_color;
        lv_label_set_text(s_air_tile_val, air_ui_tile_text(&air_color));
        lv_obj_set_style_bg_color(lv_obj_get_parent(s_air_tile_val), air_color, 0); /* tile colour = radon level */
    }

    const time_t now = time(nullptr);
    if (now < 1700000000) {
        status_bar_set_title(s_bar, "Início");
        if (s_clock_tile_time) {
            lv_label_set_text(s_clock_tile_time, "--:--");
        }
        return;
    }
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[24];
    strftime(buf, sizeof(buf), "%a, %b %d", &ti);
    status_bar_set_title(s_bar, buf);
    if (s_clock_tile_time) {
        settings_format_hm(buf, sizeof(buf), &ti, false);
        lv_label_set_text(s_clock_tile_time, buf);
    }
}

/* ---------------------------------------------------------------- catálogo dos apps */

/* O que o botão mostra além do ícone fixo: texto que muda (atualizado por home_timer_cb). */
enum class Vivo : uint8_t { Nenhum, Clima, Relogio, Air };

struct HomeApp {
    const char *nome;
    const char *icone;         /* símbolo do LVGL, ou nullptr se usa imagem/texto vivo */
    const lv_font_t *fonte;
    const lv_img_dsc_t *img;   /* ícone desenhado (só transparência, pintado de branco) */
    const char *icone_lista;   /* ícone pequeno no editor, para apps com texto vivo */
    lv_palette_t paleta;
    int8_t escurecer;          /* 0 = lv_palette_main, n = lv_palette_darken(n) */
    app_open_fn abrir;
    Vivo vivo;
};

static void open_ambient(void) {
    (void)ambient_ui_open();
}

/*
 * Todos os apps que podem ir para a tela inicial, na ordem padrão. A ordem e quais aparecem
 * são escolhidas em Ajustes > Tela inicial e ficam no /settings.ini (home_apps.h).
 * Os dois catálogos andam juntos: mesmo índice.
 */
static const HomeAppInfo k_info[] = {
    {"placar", true, false},  {"freedoom", true, false}, {"clima", true, false},   {"arquivos", true, false},
    {"musica", true, false},  {"radio", true, false},    {"breakout", true, false}, {"wifi", true, false},
    {"ajustes", true, true},  {"painel", true, false},   {"relogio", false, false}, {"air", false, false},
};
static const HomeApp k_apps[] = {
    {"Placar", LV_SYMBOL_LIST, &pt_montserrat_30, nullptr, nullptr, LV_PALETTE_RED, 0, placar_ui_open, Vivo::Nenhum},
    {"Freedoom", nullptr, nullptr, &icone_freedoom, nullptr, LV_PALETTE_BROWN, 1, doom_ui_open, Vivo::Nenhum},
    {"Clima", nullptr, &pt_montserrat_22, nullptr, "\xC2\xB0" "C", LV_PALETTE_TEAL, 0, weather_ui_open, Vivo::Clima},
    {"Arquivos", LV_SYMBOL_DIRECTORY, &pt_montserrat_30, nullptr, nullptr, LV_PALETTE_AMBER, 0, files_ui_open,
     Vivo::Nenhum},
    {"Música", LV_SYMBOL_AUDIO, &pt_montserrat_30, nullptr, nullptr, LV_PALETTE_PINK, 0, music_ui_open, Vivo::Nenhum},
    {"Rádio", LV_SYMBOL_VOLUME_MAX, &pt_montserrat_30, nullptr, nullptr, LV_PALETTE_INDIGO, 0, radio_ui_open,
     Vivo::Nenhum},
    {"Breakout", LV_SYMBOL_PLAY, &pt_montserrat_28, nullptr, nullptr, LV_PALETTE_DEEP_PURPLE, 0, breakout_ui_open,
     Vivo::Nenhum},
    {"Wi-Fi", LV_SYMBOL_WIFI, &pt_montserrat_30, nullptr, nullptr, LV_PALETTE_BLUE, 0, wifi_ui_open, Vivo::Nenhum},
    {"Ajustes", LV_SYMBOL_SETTINGS, &pt_montserrat_30, nullptr, nullptr, LV_PALETTE_BLUE_GREY, 0, settings_ui_open,
     Vivo::Nenhum},
    {"Painel", LV_SYMBOL_IMAGE, &pt_montserrat_30, nullptr, nullptr, LV_PALETTE_LIGHT_BLUE, 0, open_ambient,
     Vivo::Nenhum},
    {"Relógio", nullptr, &pt_montserrat_20, nullptr, "12", LV_PALETTE_DEEP_ORANGE, 1, clock_ui_open, Vivo::Relogio},
    {"Air", nullptr, &pt_montserrat_24, nullptr, "Rn", LV_PALETTE_GREEN, 0, air_ui_open, Vivo::Air},
};
static const int k_n_apps = sizeof(k_apps) / sizeof(k_apps[0]);
static_assert(sizeof(k_info) / sizeof(k_info[0]) == sizeof(k_apps) / sizeof(k_apps[0]), "catálogos diferentes");

static HomeLayout s_layout(k_info, k_n_apps);

static lv_color_t app_color(const HomeApp &a) {
    return a.escurecer ? lv_palette_darken(a.paleta, a.escurecer) : lv_palette_main(a.paleta);
}

/** Desenha o ícone do app dentro de tile (botão do tamanho que for). Devolve o rótulo do texto vivo. */
static lv_obj_t *add_icon(lv_obj_t *tile, const HomeApp &a, bool pequeno) {
    if (a.img) {
        lv_obj_t *img = lv_img_create(tile);
        lv_img_set_src(img, a.img);
        lv_obj_set_style_img_recolor(img, lv_color_white(), 0);
        lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
        if (pequeno) {
            lv_img_set_zoom(img, 128); /* 48 -> 24 px */
        }
        lv_obj_center(img);
        return nullptr;
    }
    lv_obj_t *ic = lv_label_create(tile);
    lv_obj_set_style_text_color(ic, lv_color_white(), 0);
    if (pequeno) {
        lv_obj_set_style_text_font(ic, &pt_montserrat_16, 0);
        lv_label_set_text(ic, a.icone ? a.icone : a.icone_lista);
        lv_obj_center(ic);
        return nullptr;
    }
    lv_obj_set_style_text_font(ic, a.fonte, 0);
    lv_obj_center(ic);
    switch (a.vivo) {
    case Vivo::Clima:
        lv_label_set_text(ic, weather_ui_tile_text());
        s_weather_tile_temp = ic;
        break;
    case Vivo::Relogio:
        lv_label_set_text(ic, "--:--");
        s_clock_tile_time = ic;
        break;
    case Vivo::Air: {
        lv_color_t c;
        lv_label_set_text(ic, air_ui_tile_text(&c));
        lv_obj_set_style_bg_color(tile, c, 0); /* cor do botão = nível de radônio */
        s_air_tile_val = ic;
        break;
    }
    default:
        lv_label_set_text(ic, a.icone);
        break;
    }
    return ic;
}

/* Home grid: 5 columns x 2 rows of 88 px cells (72 px tile + name) under the status bar - the full 464 px width.
 * More than 10 apps: more pages side by side (swipe), with dots below. */
static const lv_coord_t k_cell_w = 88;
static const int k_grid_cols = 5;
static const int k_per_page = 10;
static const lv_coord_t k_grid_gap = 6;
static const lv_coord_t k_grid_w = k_grid_cols * k_cell_w + (k_grid_cols - 1) * k_grid_gap;

/** Rounded app tile: icon on top, app name below. */
static void add_app_tile(lv_obj_t *parent, const HomeApp &a) {
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, k_cell_w, LV_SIZE_CONTENT); /* fixed cells keep a tidy grid */
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 6, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(col, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *tile = lv_btn_create(col);
    lv_obj_set_size(tile, 72, 72);
    lv_obj_set_style_radius(tile, 20, 0);
    lv_obj_set_style_bg_color(tile, app_color(a), 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_add_event_cb(tile, app_tile_cb, LV_EVENT_CLICKED, (void *)a.abrir);
    (void)add_icon(tile, a, false);

    lv_obj_t *lbl = lv_label_create(col);
    lv_obj_set_style_text_font(lbl, ui_font_get(), 0);
    lv_obj_set_style_text_color(lbl, ui_color_text(), 0);
    lv_label_set_text(lbl, a.nome);
}

static lv_obj_t *s_dots = nullptr;

static void pages_scroll_cb(lv_event_t *e) {
    lv_obj_t *pages = lv_event_get_target(e);
    if (!s_dots) {
        return;
    }
    const int atual = (lv_obj_get_scroll_x(pages) + k_grid_w / 2) / k_grid_w;
    const int n = lv_obj_get_child_cnt(s_dots);
    for (int i = 0; i < n; i++) {
        lv_obj_set_style_bg_opa(lv_obj_get_child(s_dots, i), i == atual ? LV_OPA_COVER : LV_OPA_30, 0);
    }
}

/** (Re)monta a grade com os apps visíveis, na ordem escolhida. */
static void rebuild_apps(void) {
    if (s_apps_area) {
        lv_obj_del(s_apps_area);
    }
    s_weather_tile_temp = s_clock_tile_time = s_air_tile_val = s_dots = nullptr;

    int vis[HomeLayout::kMax];
    const int n = s_layout.visiveis(vis, HomeLayout::kMax);
    const int n_pages = (n + k_per_page - 1) / k_per_page;

    s_apps_area = lv_obj_create(s_home_scr);
    lv_obj_remove_style_all(s_apps_area);
    lv_obj_set_size(s_apps_area, k_grid_w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_apps_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_apps_area, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_apps_area, 8, 0);
    lv_obj_clear_flag(s_apps_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_apps_area, LV_ALIGN_CENTER, 0, n_pages > 1 ? 26 : 20);

    lv_obj_t *pages = lv_obj_create(s_apps_area);
    lv_obj_remove_style_all(pages);
    lv_obj_set_size(pages, k_grid_w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pages, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(pages, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(pages, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(pages, LV_OBJ_FLAG_SCROLL_ONE);
    if (n_pages <= 1) {
        lv_obj_clear_flag(pages, LV_OBJ_FLAG_SCROLLABLE);
    }

    for (int pg = 0; pg < n_pages; pg++) {
        lv_obj_t *grid = lv_obj_create(pages);
        lv_obj_remove_style_all(grid);
        lv_obj_set_size(grid, k_grid_w, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(grid, k_grid_gap, 0);
        lv_obj_set_style_pad_row(grid, 10, 0);
        lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(grid, LV_OBJ_FLAG_SNAPPABLE);
        for (int k = pg * k_per_page; k < n && k < (pg + 1) * k_per_page; k++) {
            add_app_tile(grid, k_apps[vis[k]]);
        }
    }

    if (n_pages > 1) {
        s_dots = lv_obj_create(s_apps_area);
        lv_obj_remove_style_all(s_dots);
        lv_obj_set_size(s_dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(s_dots, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(s_dots, 8, 0);
        for (int pg = 0; pg < n_pages; pg++) {
            lv_obj_t *d = lv_obj_create(s_dots);
            lv_obj_remove_style_all(d);
            lv_obj_set_size(d, 8, 8);
            lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(d, ui_color_text_muted(), 0);
            lv_obj_set_style_bg_opa(d, pg == 0 ? LV_OPA_COVER : LV_OPA_30, 0);
        }
        lv_obj_add_event_cb(pages, pages_scroll_cb, LV_EVENT_SCROLL_END, nullptr);
    }
}

void home_ui_init(void) {
    if (s_home_scr) {
        return;
    }

    s_layout.carregar(settings_home_order(), settings_home_hidden());

    s_home_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_opa(s_home_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_home_scr, ui_color_bg(), 0);
    lv_obj_clear_flag(s_home_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_pad_all(s_home_scr, 8, 0);
    s_bar = status_bar_create(s_home_scr, "Início", false);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 0);

    rebuild_apps();

    (void)lv_timer_create(home_timer_cb, 1000, nullptr);

    lv_scr_load(s_home_scr);
    serial_log("[home] home screen shown (%s; ocultos: %s)", s_layout.texto_ordem().c_str(),
               s_layout.texto_ocultos().c_str());
}

/* ---------------------------------------------------------------- Ajustes > Tela inicial */

static lv_obj_t *s_edit_scr = nullptr;
static lv_obj_t *s_edit_list = nullptr;

static void fill_edit_list(void);

static void salvar_layout(void) {
    settings_set_home(s_layout.texto_ordem().c_str(), s_layout.texto_ocultos().c_str());
    rebuild_apps(); /* a tela inicial não está à vista agora: refaz já */
}

static void edit_move_cb(lv_event_t *e) {
    const intptr_t v = (intptr_t)lv_event_get_user_data(e);
    const int pos = int(v >> 1), delta = (v & 1) ? +1 : -1;
    if (s_layout.mover(pos, delta)) {
        salvar_layout();
        /* Depois do evento: refazer a lista apaga o botão que foi tocado. */
        lv_async_call([](void *) { if (s_edit_list) fill_edit_list(); }, nullptr);
    }
}

static void edit_switch_cb(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    const int pos = int((intptr_t)lv_event_get_user_data(e));
    const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (s_layout.definir_visivel(pos, on)) {
        salvar_layout();
    } else {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
}

static lv_obj_t *edit_arrow(lv_obj_t *row, const char *sym, int pos, int delta, bool enabled) {
    lv_obj_t *b = lv_btn_create(row);
    lv_obj_set_size(b, 44, 36);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &pt_montserrat_18, 0);
    lv_label_set_text(l, sym);
    lv_obj_center(l);
    if (enabled) {
        lv_obj_add_event_cb(b, edit_move_cb, LV_EVENT_CLICKED, (void *)(intptr_t)((pos << 1) | (delta > 0 ? 1 : 0)));
    } else {
        lv_obj_add_state(b, LV_STATE_DISABLED);
    }
    return b;
}

static void fill_edit_list(void) {
    const lv_coord_t scroll = lv_obj_get_scroll_y(s_edit_list);
    lv_obj_clean(s_edit_list);
    const int n = s_layout.quantos();
    for (int pos = 0; pos < n; pos++) {
        const HomeApp &a = k_apps[s_layout.app(pos)];
        lv_obj_t *row = lv_obj_create(s_edit_list);
        ui_style_card(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *tile = lv_obj_create(row);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, 36, 36);
        lv_obj_set_style_radius(tile, 10, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(tile, app_color(a), 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        (void)add_icon(tile, a, true);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, ui_font_get(), 0);
        lv_obj_set_style_text_color(name, s_layout.visivel(pos) ? ui_color_text() : ui_color_text_muted(), 0);
        lv_label_set_text(name, a.nome);
        lv_obj_set_flex_grow(name, 1);

        (void)edit_arrow(row, LV_SYMBOL_UP, pos, -1, pos > 0);
        (void)edit_arrow(row, LV_SYMBOL_DOWN, pos, +1, pos < n - 1);

        lv_obj_t *sw = lv_switch_create(row);
        if (s_layout.visivel(pos)) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        if (s_layout.fixo(pos)) {
            lv_obj_add_state(sw, LV_STATE_DISABLED); /* Ajustes: sem ele não há como desfazer */
        } else {
            lv_obj_add_event_cb(sw, edit_switch_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)pos);
        }
    }
    lv_obj_update_layout(s_edit_list);
    lv_obj_scroll_to_y(s_edit_list, scroll, LV_ANIM_OFF);
}

void home_ui_open_editor(void) {
    if (!s_edit_scr) {
        s_edit_scr = lv_obj_create(nullptr);
        lv_obj_set_style_bg_opa(s_edit_scr, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_edit_scr, ui_color_bg(), 0);
        lv_obj_set_style_pad_all(s_edit_scr, 8, 0);
        lv_obj_set_style_pad_row(s_edit_scr, 6, 0);
        lv_obj_set_flex_flow(s_edit_scr, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(s_edit_scr, LV_OBJ_FLAG_SCROLLABLE);
        (void)status_bar_create(s_edit_scr, "Tela inicial", true);

        lv_obj_t *hint = lv_label_create(s_edit_scr);
        lv_obj_set_style_text_font(hint, &pt_montserrat_14, 0);
        lv_obj_set_style_text_color(hint, ui_color_text_muted(), 0);
        lv_label_set_text(hint, "Setas mudam a ordem; a chave mostra ou esconde. Ajustes fica sempre.");

        s_edit_list = lv_obj_create(s_edit_scr);
        lv_obj_remove_style_all(s_edit_list);
        lv_obj_set_width(s_edit_list, LV_PCT(100));
        lv_obj_set_flex_grow(s_edit_list, 1);
        lv_obj_set_flex_flow(s_edit_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(s_edit_list, 6, 0);
        lv_obj_set_scroll_dir(s_edit_list, LV_DIR_VER);
    }
    fill_edit_list();
    lv_scr_load(s_edit_scr);
}

static void release_editor(void) {
    if (s_edit_scr) {
        lv_obj_del(s_edit_scr);
        s_edit_scr = s_edit_list = nullptr;
    }
}

/**
 * Free the heavier app screens once Home is showing (they are rebuilt on next open; Weather keeps
 * its data and Files its folder). Breakout stays so a paused game survives; Clock and WiFi are
 * small, and the WiFi screen receives status updates in the background.
 */
static void release_apps_cb(void *arg) {
    LV_UNUSED(arg);
    if (lv_scr_act() != s_home_scr) {
        return; /* user already opened something else */
    }
    weather_ui_release();
    files_ui_release();
    music_ui_release();
    radio_ui_release();
    settings_ui_release();
    air_ui_release();
    release_editor();
}

void home_ui_show(void) {
    if (s_home_scr && lv_scr_act() != s_home_scr) {
        lv_scr_load(s_home_scr);
        /* Async: the Home button that got us here lives on one of the screens being deleted. */
        lv_async_call(release_apps_cb, nullptr);
    }
}

lv_obj_t *home_ui_add_home_button(lv_obj_t *parent) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 44, 36);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &pt_montserrat_20, 0);
    lv_label_set_text(lbl, LV_SYMBOL_HOME);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, home_btn_cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}
