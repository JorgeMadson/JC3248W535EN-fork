#include "pintor.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include <algorithm>

extern "C" {
extern const uint8_t u8g2_font_logisoso92_tn[];
extern const uint8_t u8g2_font_logisoso62_tn[];
extern const uint8_t u8g2_font_logisoso38_tn[];
extern const uint8_t u8g2_font_logisoso28_tf[];
extern const uint8_t u8g2_font_logisoso20_tf[];
extern const uint8_t u8g2_font_logisoso16_tf[];
}

namespace placar {

namespace {

const uint8_t* fonteU8g2(Fonte f) {
  switch (f) {
    case Fonte::Gigante: return u8g2_font_logisoso92_tn;
    case Fonte::Grande: return u8g2_font_logisoso62_tn;
    case Fonte::Tempo: return u8g2_font_logisoso38_tn;
    case Fonte::Titulo: return u8g2_font_logisoso28_tf;
    case Fonte::Texto: return u8g2_font_logisoso20_tf;
    case Fonte::Legenda: return u8g2_font_logisoso16_tf;
  }
  return u8g2_font_logisoso20_tf;
}

inline uint16_t trocarBytes(uint16_t c) { return uint16_t((c >> 8) | (c << 8)); }

// ---------- fontes U8g2 (formato documentado em u8g2_font.c) ----------

constexpr int TAM_CABECALHO = 23;

struct Cabecalho {
  uint8_t bits0, bits1, bitsW, bitsH, bitsX, bitsY, bitsD;
  uint16_t inicioA, inicioa;
};

Cabecalho lerCabecalho(const uint8_t* f) {
  return {f[2], f[3], f[4], f[5], f[6], f[7], f[8], uint16_t((f[17] << 8) | f[18]), uint16_t((f[19] << 8) | f[20])};
}

// Dados do glifo (depois dos 2 bytes de código/tamanho), ou nullptr.
const uint8_t* acharGlifo(const uint8_t* f, uint32_t cp) {
  if (cp > 255) return nullptr;  // os textos do placar são todos Latin-1
  Cabecalho c = lerCabecalho(f);
  const uint8_t* p = f + TAM_CABECALHO;
  if (cp >= 'a') p += c.inicioa;
  else if (cp >= 'A') p += c.inicioA;
  for (;;) {
    if (p[1] == 0) return nullptr;
    if (p[0] == cp) return p + 2;
    p += p[1];
  }
}

class LeitorBits {
 public:
  explicit LeitorBits(const uint8_t* p) : p_(p) {}
  uint32_t semSinal(uint8_t n) {
    uint32_t v = uint32_t(*p_) >> pos_;
    uint8_t fim = pos_ + n;
    if (fim >= 8) {
      p_++;
      v |= uint32_t(*p_) << (8 - pos_);
      fim -= 8;
    }
    pos_ = fim;
    return v & ((1u << n) - 1);
  }
  int32_t comSinal(uint8_t n) { return int32_t(semSinal(n)) - (1 << (n - 1)); }

 private:
  const uint8_t* p_;
  uint8_t pos_ = 0;
};

struct Glifo {
  int16_t w, h, dx, dy, avanco;  // dx/dy: deslocamento do canto (dy para cima da linha de base)
  LeitorBits bits;
};

bool lerGlifo(const uint8_t* f, uint32_t cp, Glifo& g) {
  const uint8_t* d = acharGlifo(f, cp);
  if (!d) return false;
  Cabecalho c = lerCabecalho(f);
  g.bits = LeitorBits(d);
  g.w = int16_t(g.bits.semSinal(c.bitsW));
  g.h = int16_t(g.bits.semSinal(c.bitsH));
  g.dx = int16_t(g.bits.comSinal(c.bitsX));
  g.dy = int16_t(g.bits.comSinal(c.bitsY));
  g.avanco = int16_t(g.bits.comSinal(c.bitsD));
  return true;
}

// Desenha o glifo com a linha de base em (x, base): RLE de corridas de 0 e 1.
void desenharGlifo(Pintor& p, const uint8_t* f, Glifo& g, int16_t x, int16_t base, uint16_t cor) {
  if (g.w <= 0) return;
  Cabecalho c = lerCabecalho(f);
  const int16_t ox = x + g.dx, oy = base - (g.h + g.dy);
  int16_t lx = 0, ly = 0;
  auto corrida = [&](uint32_t n, bool tinta) {
    for (;;) {
      int16_t resto = g.w - lx;
      int16_t atual = n < uint32_t(resto) ? int16_t(n) : resto;
      if (tinta && atual > 0) p.preencher(ox + lx, oy + ly, atual, 1, cor);
      if (n < uint32_t(resto)) break;
      n -= resto;
      lx = 0;
      ly++;
    }
    lx += int16_t(n);
  };
  for (;;) {
    uint32_t a = g.bits.semSinal(c.bits0), b = g.bits.semSinal(c.bits1);
    do {
      corrida(a, false);
      corrida(b, true);
    } while (g.bits.semSinal(1) != 0);
    if (ly >= g.h) break;
  }
}

uint32_t proximoCodigo(const char*& s) {
  const uint8_t* u = reinterpret_cast<const uint8_t*>(s);
  uint32_t cp;
  int n;
  if (u[0] < 0x80) { cp = u[0]; n = 1; }
  else if ((u[0] & 0xE0) == 0xC0) { cp = u[0] & 0x1F; n = 2; }
  else if ((u[0] & 0xF0) == 0xE0) { cp = u[0] & 0x0F; n = 3; }
  else { cp = u[0] & 0x07; n = 4; }
  for (int i = 1; i < n; i++) {
    if ((u[i] & 0xC0) != 0x80) { s += i; return '?'; }
    cp = (cp << 6) | (u[i] & 0x3F);
  }
  s += n;
  return cp;
}

struct Caixa {
  int16_t x1, y1, w, h;
};

// Caixa da tinta do texto com a linha de base em y=0 e cursor em x=0.
Caixa medir(const uint8_t* f, const char* s) {
  int16_t cx = 0, minX = INT16_MAX, maxX = INT16_MIN, minY = INT16_MAX, maxY = INT16_MIN;
  while (*s) {
    Glifo g{0, 0, 0, 0, 0, LeitorBits(nullptr)};
    if (!lerGlifo(f, proximoCodigo(s), g)) continue;
    if (g.w > 0 && g.h > 0) {
      minX = std::min<int16_t>(minX, cx + g.dx);
      maxX = std::max<int16_t>(maxX, cx + g.dx + g.w - 1);
      minY = std::min<int16_t>(minY, -(g.h + g.dy));
      maxY = std::max<int16_t>(maxY, -g.dy - 1);
    }
    cx += g.avanco;
  }
  if (minX > maxX) return {0, 0, cx, 0};
  return {minX, minY, int16_t(maxX - minX + 1), int16_t(maxY - minY + 1)};
}

}  // namespace

// ---------- primitivas ----------

void Pintor::preencher(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  if (cima_) {
    x = int16_t(LARGURA_TELA - x - w);
    y = int16_t(ALTURA_TELA - y - h);
  }
  int16_t x2 = x + w, y2 = y + h;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x2 > LARGURA_TELA) x2 = LARGURA_TELA;
  if (y2 > ALTURA_TELA) y2 = ALTURA_TELA;
  if (x >= x2 || y >= y2) return;
  // lógico [x,x2)x[y,y2) -> físico px em [y,y2), py em [320-x2, 320-x)
  const uint16_t v = trocarBytes(c);
  for (int16_t py = LARGURA_TELA - x2; py < LARGURA_TELA - x; py++) {
    uint16_t* linha = buf_ + size_t(py) * LARGURA_FISICA;
    for (int16_t px = y; px < y2; px++) linha[px] = v;
  }
}

void Pintor::limpar(uint16_t c) {
  const uint16_t v = trocarBytes(c);
  const size_t n = size_t(LARGURA_FISICA) * ALTURA_FISICA;
  for (size_t i = 0; i < n; i++) buf_[i] = v;
}

void Pintor::arredondado(Retangulo r, int16_t raio, uint16_t c) {
  raio = std::min<int16_t>(raio, std::min(r.w, r.h) / 2);
  if (raio <= 0) return retangulo(r, c);
  for (int16_t i = 0; i < r.h; i++) {
    int16_t inset = 0;
    float d = 0;
    if (i < raio) d = raio - i - 0.5f;
    else if (i >= r.h - raio) d = i - (r.h - raio) + 0.5f;
    if (d > 0) inset = int16_t(lroundf(raio - sqrtf(float(raio) * raio - d * d)));
    preencher(r.x + inset, r.y + i, r.w - 2 * inset, 1, c);
  }
}

void Pintor::contornoArredondado(Retangulo r, int16_t raio, uint16_t c) {
  raio = std::min<int16_t>(raio, std::min(r.w, r.h) / 2);
  linhaH(r.x + raio, r.y, r.w - 2 * raio, c);
  linhaH(r.x + raio, r.y + r.h - 1, r.w - 2 * raio, c);
  linhaV(r.x, r.y + raio, r.h - 2 * raio, c);
  linhaV(r.x + r.w - 1, r.y + raio, r.h - 2 * raio, c);
  // Cantos: ponto médio (Bresenham) nos quatro quadrantes.
  const int16_t cx0 = r.x + raio, cx1 = r.x + r.w - 1 - raio, cy0 = r.y + raio, cy1 = r.y + r.h - 1 - raio;
  int16_t f = 1 - raio, ddx = 1, ddy = -2 * raio, x = 0, y = raio;
  while (x <= y) {
    ponto(cx1 + x, cy1 + y, c); ponto(cx1 + y, cy1 + x, c);
    ponto(cx0 - x, cy1 + y, c); ponto(cx0 - y, cy1 + x, c);
    ponto(cx1 + x, cy0 - y, c); ponto(cx1 + y, cy0 - x, c);
    ponto(cx0 - x, cy0 - y, c); ponto(cx0 - y, cy0 - x, c);
    if (f >= 0) { y--; ddy += 2; f += ddy; }
    x++; ddx += 2; f += ddx;
  }
}

void Pintor::circulo(Ponto c, int16_t r, uint16_t cor) {
  for (int16_t dy = -r; dy <= r; dy++) {
    int16_t dx = int16_t(lroundf(sqrtf(float(r) * r - float(dy) * dy)));
    preencher(c.x - dx, c.y + dy, 2 * dx + 1, 1, cor);
  }
}

void Pintor::contornoCirculo(Ponto c, int16_t r, uint16_t cor) {
  int16_t f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
  while (x <= y) {
    ponto(c.x + x, c.y + y, cor); ponto(c.x - x, c.y + y, cor);
    ponto(c.x + x, c.y - y, cor); ponto(c.x - x, c.y - y, cor);
    ponto(c.x + y, c.y + x, cor); ponto(c.x - y, c.y + x, cor);
    ponto(c.x + y, c.y - x, cor); ponto(c.x - y, c.y - x, cor);
    if (f >= 0) { y--; ddy += 2; f += ddy; }
    x++; ddx += 2; f += ddx;
  }
}

void Pintor::triangulo(Ponto a, Ponto b, Ponto c, uint16_t cor) {
  // Ordena por y e preenche linha a linha.
  if (a.y > b.y) std::swap(a, b);
  if (b.y > c.y) std::swap(b, c);
  if (a.y > b.y) std::swap(a, b);
  if (a.y == c.y) {
    int16_t lo = std::min({a.x, b.x, c.x}), hi = std::max({a.x, b.x, c.x});
    return linhaH(lo, a.y, hi - lo + 1, cor);
  }
  for (int16_t y = a.y; y <= c.y; y++) {
    float xa = a.x + float(c.x - a.x) * (y - a.y) / float(c.y - a.y);
    float xb = (y < b.y || b.y == c.y)
                   ? (b.y == a.y ? b.x : a.x + float(b.x - a.x) * (y - a.y) / float(b.y - a.y))
                   : b.x + float(c.x - b.x) * (y - b.y) / float(c.y - b.y);
    if (xa > xb) std::swap(xa, xb);
    int16_t x1 = int16_t(lroundf(xa)), x2 = int16_t(lroundf(xb));
    linhaH(x1, y, x2 - x1 + 1, cor);
  }
}

// ---------- texto ----------

int16_t Pintor::meiaAlturaReferencia(Fonte f) {
  int16_t& cache = cacheMeiaAltura_[static_cast<int>(f)];
  if (cache < 0) {
    Caixa c = medir(fonteU8g2(f), "0");
    cache = int16_t(-c.y1 - c.h / 2);
  }
  return cache;
}

int16_t Pintor::largura(const char* s, Fonte f) { return medir(fonteU8g2(f), s).w; }

void Pintor::texto(const char* s, int16_t x, int16_t yCentro, Fonte f, uint16_t cor, Alinhamento a) {
  const uint8_t* fonte = fonteU8g2(f);
  Caixa c = medir(fonte, s);
  int16_t cx = x - c.x1;  // Esquerda
  if (a == Alinhamento::Centro) cx = x - c.x1 - c.w / 2;
  if (a == Alinhamento::Direita) cx = x - c.x1 - c.w;
  const int16_t base = yCentro + meiaAlturaReferencia(f);
  while (*s) {
    Glifo g{0, 0, 0, 0, 0, LeitorBits(nullptr)};
    if (!lerGlifo(fonte, proximoCodigo(s), g)) continue;
    desenharGlifo(*this, fonte, g, cx, base, cor);
    cx += g.avanco;
  }
}

}  // namespace placar
