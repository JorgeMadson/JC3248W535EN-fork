#pragma once
/*
 * Ordem e visibilidade dos apps da tela inicial, sem LVGL (testado no PC: test/home_apps).
 *
 * O catálogo (home_ui.cpp) diz quais apps existem, a ordem padrão e quais começam escondidos.
 * No /settings.ini fica o que a pessoa escolheu:
 *   home_ordem=placar,freedoom,clima,...   (ids na ordem da tela)
 *   home_ocultos=relogio,air               (ids escondidos)
 * Um app que não pode ser escondido (Ajustes, senão não haveria como desfazer) aparece sempre.
 */
#include <string>

struct HomeAppInfo {
    const char *id;     /* nome curto, o que vai para o settings.ini */
    bool visivel_padrao;
    bool fixo;          /* nunca pode ser escondido */
};

class HomeLayout {
  public:
    static constexpr int kMax = 16;

    HomeLayout(const HomeAppInfo *catalogo, int n);

    /** Lê o que foi salvo. nullptr = nada salvo (usa o padrão). Ids desconhecidos são ignorados. */
    void carregar(const char *ordem, const char *ocultos);
    std::string texto_ordem() const;
    std::string texto_ocultos() const;

    int quantos() const { return n_; }
    /** Índice no catálogo do app que está na posição pos. */
    int app(int pos) const { return ordem_[pos]; }
    bool visivel(int pos) const;
    bool fixo(int pos) const { return cat_[ordem_[pos]].fixo; }

    /** Troca o app de lugar com o vizinho (delta -1 = sobe, +1 = desce). false se não deu. */
    bool mover(int pos, int delta);
    /** Mostra ou esconde. false se não deu (esconder um app fixo). */
    bool definir_visivel(int pos, bool v);
    /** Índices no catálogo dos apps visíveis, na ordem da tela. Devolve quantos. */
    int visiveis(int *saida, int max) const;

  private:
    const HomeAppInfo *cat_;
    int n_;
    int ordem_[kMax];
    bool oculto_[kMax]; /* por índice no catálogo */

    int achar(const std::string &id) const;
};
