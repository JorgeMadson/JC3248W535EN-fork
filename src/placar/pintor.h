#pragma once
// Pintor do placar para o launcher LVGL: mesma interface do placar-mesa
// (src/hal/pintor.h), mas desenha por software num buffer RGB565 do lv_canvas.
//
// O placar pensa em retrato 320x480 (LARGURA_TELA x ALTURA_TELA); a tela do
// launcher é paisagem 480x320. Ponto lógico (x, y) -> físico (y, 319 - x).
// Sobre isso ainda vale orientar(): o lado de Cima é girado 180°.

#include <stdint.h>

#include "geometria.h"
#include "orientacao.h"
#include "tema.h"

namespace placar {

enum class Alinhamento : uint8_t { Esquerda, Centro, Direita };

// Largura/altura do buffer físico (paisagem).
constexpr int16_t LARGURA_FISICA = ALTURA_TELA;  // 480
constexpr int16_t ALTURA_FISICA = LARGURA_TELA;  // 320

// Coordenada física do toque -> coordenada de tela do placar (retrato).
inline Ponto fisicoParaTela(int16_t px, int16_t py) { return {int16_t(LARGURA_TELA - 1 - py), px}; }

class Pintor {
 public:
  // buffer: LARGURA_FISICA * ALTURA_FISICA pixels, no formato do lv_color_t
  // (RGB565 com os bytes trocados, LV_COLOR_16_SWAP).
  explicit Pintor(uint16_t* buffer) : buf_(buffer) {}

  void orientar(Lado l) { cima_ = l == Lado::Cima; }

  void limpar(uint16_t c);
  void retangulo(Retangulo r, uint16_t c) { preencher(r.x, r.y, r.w, r.h, c); }
  void arredondado(Retangulo r, int16_t raio, uint16_t c);
  void contornoArredondado(Retangulo r, int16_t raio, uint16_t c);
  void linhaH(int16_t x, int16_t y, int16_t w, uint16_t c) { preencher(x, y, w, 1, c); }
  void linhaV(int16_t x, int16_t y, int16_t h, uint16_t c) { preencher(x, y, 1, h, c); }
  void circulo(Ponto c, int16_t r, uint16_t cor);
  void contornoCirculo(Ponto c, int16_t r, uint16_t cor);
  void triangulo(Ponto a, Ponto b, Ponto c, uint16_t cor);

  // Texto com o centro vertical em yCentro (altura de referência = altura dos
  // dígitos, para números e letras ficarem alinhados entre si). UTF-8.
  void texto(const char* s, int16_t x, int16_t yCentro, Fonte f, uint16_t cor, Alinhamento a = Alinhamento::Centro);
  int16_t largura(const char* s, Fonte f);

  // Usado pelo decodificador de fontes (público só por isso).
  void preencher(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c);

 private:
  void ponto(int16_t x, int16_t y, uint16_t c) { preencher(x, y, 1, 1, c); }
  int16_t meiaAlturaReferencia(Fonte f);

  uint16_t* buf_;
  bool cima_ = false;
  int16_t cacheMeiaAltura_[6] = {-1, -1, -1, -1, -1, -1};
};

}  // namespace placar
