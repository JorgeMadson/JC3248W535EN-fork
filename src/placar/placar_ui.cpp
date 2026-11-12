#include "placar_ui.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <lvgl.h>
#include <nvs.h>

#include <memory>

#include "contexto.h"
#include "gerenciador_telas.h"
#include "gestos.h"
#include "home_ui.h"
#include "pintor.h"
#include "serial_log.h"
#include "tela_menu.h"

/*
 * Placar de mesa (código em JC3248W535/placar-mesa) como app do launcher.
 * Tudo roda na tarefa do LVGL: um lv_timer faz o mesmo laço do main.cpp do
 * placar (toque -> gestos -> telas -> desenho). O desenho é por software num
 * buffer de tela inteira (PSRAM) mostrado por um lv_canvas em tela cheia.
 */

namespace placar {
namespace {

class RelogioEsp : public Relogio {
 public:
  uint32_t agoraMs() const override { return uint32_t(esp_timer_get_time() / 1000); }
};

// NVS, espaço "placar" (o mesmo nome usado pelo firmware próprio).
class ArmazenamentoNvs : public Armazenamento {
 public:
  bool iniciar() { return ok_ = nvs_open("placar", NVS_READWRITE, &h_) == ESP_OK; }

  int32_t lerInt(const char* chave, int32_t padrao) override {
    int32_t v;
    return ok_ && nvs_get_i32(h_, chave, &v) == ESP_OK ? v : padrao;
  }

  void gravarInt(const char* chave, int32_t valor) override {
    if (!ok_ || lerInt(chave, valor + 1) == valor) return;  // evita gravar à toa
    nvs_set_i32(h_, chave, valor);
    nvs_commit(h_);
  }

 private:
  nvs_handle_t h_ = 0;
  bool ok_ = false;
};

RelogioEsp s_relogio;
ArmazenamentoNvs s_armazenamento;
GerenciadorTelas s_telas;
ReconhecedorGestos s_gestos;
Contexto s_ctx{s_telas, s_relogio, s_armazenamento};

lv_obj_t* s_scr = nullptr;
lv_obj_t* s_canvas = nullptr;
lv_timer_t* s_timer = nullptr;
uint16_t* s_buf = nullptr;
Pintor* s_pintor = nullptr;

bool s_tocando = false;
Ponto s_toque;

void sair() {
  lv_timer_pause(s_timer);
  s_tocando = false;
  home_ui_show();
}

void toque_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    s_toque = fisicoParaTela(int16_t(p.x), int16_t(p.y));
    s_tocando = true;
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    s_tocando = false;
  }
}

void laco_cb(lv_timer_t*) {
  uint32_t agora = s_relogio.agoraMs();
  Evento eventos[ReconhecedorGestos::MAX_EVENTOS];
  int n = s_gestos.processar(s_tocando, s_toque, agora, eventos);
  for (int i = 0; i < n; i++) {
    s_telas.aoEvento(eventos[i]);
    s_telas.aplicarPendentes();
    if (lv_scr_act() != s_scr) return;  // "Sair" já voltou para o launcher
  }
  s_telas.atualizar(agora);
  if (s_telas.precisaDesenhar()) {
    s_telas.desenhar(*s_pintor);
    lv_obj_invalidate(s_canvas);
  }
}

bool construir() {
  if (s_scr) return true;
  const size_t bytes = size_t(LARGURA_FISICA) * ALTURA_FISICA * sizeof(uint16_t);
  s_buf = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
  if (!s_buf) {
    serial_log("[placar] sem memória para o canvas (%u bytes)", (unsigned)bytes);
    return false;
  }
  s_pintor = new Pintor(s_buf);
  if (!s_armazenamento.iniciar()) serial_log("[placar] NVS indisponível, configurações não serão salvas");
  s_ctx.sair = sair;

  s_scr = lv_obj_create(nullptr);
  lv_obj_remove_style_all(s_scr);
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

  s_canvas = lv_canvas_create(s_scr);
  lv_canvas_set_buffer(s_canvas, s_buf, LARGURA_FISICA, ALTURA_FISICA, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(s_canvas, 0, 0);
  lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(s_canvas, toque_cb, LV_EVENT_ALL, nullptr);

  s_telas.empilhar(std::unique_ptr<Tela>(new TelaMenu(s_ctx)));
  s_telas.aplicarPendentes();

  s_timer = lv_timer_create(laco_cb, 10, nullptr);
  lv_timer_pause(s_timer);
  serial_log("[placar] pronto (canvas %dx%d em PSRAM)", LARGURA_FISICA, ALTURA_FISICA);
  return true;
}

}  // namespace
}  // namespace placar

void placar_ui_open(void) {
  using namespace placar;
  if (!construir()) return;
  s_tocando = false;
  lv_timer_resume(s_timer);
  lv_scr_load(s_scr);
}
