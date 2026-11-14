// Testes da ordem e visibilidade dos apps da tela inicial (src/home_apps.cpp).
//
// O launcher não tem ambiente de teste no PlatformIO; este teste compila só com g++.
// No Deck, dentro do distrobox dev-gcc:
//   test/home_apps/rodar.sh
#include <stdio.h>
#include <string.h>

#include <string>

#include "home_apps.h"

static int falhas = 0, total = 0;
#define CONFERIR(cond)                                                       \
    do {                                                                     \
        total++;                                                             \
        if (!(cond)) {                                                       \
            falhas++;                                                        \
            printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                    \
    } while (0)
#define IGUAL(a, b) CONFERIR(std::string(a) == std::string(b))

// Mesmo formato do catálogo de home_ui.cpp (os testes não dependem da lista real).
static const HomeAppInfo k_cat[] = {
    {"placar", true, false},  {"freedoom", true, false}, {"clima", true, false},
    {"wifi", true, false},    {"ajustes", true, true},   {"relogio", false, false},
};
static const int k_n = sizeof(k_cat) / sizeof(k_cat[0]);

static std::string visiveis(const HomeLayout &l) {
    int v[HomeLayout::kMax];
    int n = l.visiveis(v, HomeLayout::kMax);
    std::string s;
    for (int i = 0; i < n; i++) {
        if (i) s += ',';
        s += k_cat[v[i]].id;
    }
    return s;
}

static void sem_nada_salvo_usa_o_padrao() {
    HomeLayout l(k_cat, k_n);
    IGUAL(visiveis(l), "placar,freedoom,clima,wifi,ajustes");
    IGUAL(l.texto_ordem(), "placar,freedoom,clima,wifi,ajustes,relogio");
    IGUAL(l.texto_ocultos(), "relogio");
}

static void le_e_grava_o_mesmo_texto() {
    HomeLayout l(k_cat, k_n);
    l.carregar("relogio,wifi,ajustes,placar,clima,freedoom", "freedoom");
    IGUAL(visiveis(l), "relogio,wifi,ajustes,placar,clima");
    HomeLayout outro(k_cat, k_n);
    outro.carregar(l.texto_ordem().c_str(), l.texto_ocultos().c_str());
    IGUAL(outro.texto_ordem(), l.texto_ordem());
    IGUAL(outro.texto_ocultos(), l.texto_ocultos());
}

static void ids_desconhecidos_e_repetidos_sao_ignorados() {
    HomeLayout l(k_cat, k_n);
    l.carregar(" wifi , xyz,wifi,,placar ", "abc");
    IGUAL(l.texto_ordem(), "wifi,placar,freedoom,clima,ajustes,relogio");
    IGUAL(l.texto_ocultos(), "");
}

static void app_novo_que_nao_estava_salvo_entra_no_fim() {
    HomeLayout l(k_cat, k_n);
    l.carregar("clima,placar", "");
    IGUAL(l.texto_ordem(), "clima,placar,freedoom,wifi,ajustes,relogio");
}

static void mover_troca_com_o_vizinho() {
    HomeLayout l(k_cat, k_n);
    CONFERIR(l.mover(1, -1));
    IGUAL(l.texto_ordem(), "freedoom,placar,clima,wifi,ajustes,relogio");
    CONFERIR(l.mover(1, +1));
    IGUAL(l.texto_ordem(), "freedoom,clima,placar,wifi,ajustes,relogio");
}

static void mover_alem_das_pontas_nao_faz_nada() {
    HomeLayout l(k_cat, k_n);
    CONFERIR(!l.mover(0, -1));
    CONFERIR(!l.mover(k_n - 1, +1));
    CONFERIR(!l.mover(99, -1));
    IGUAL(l.texto_ordem(), "placar,freedoom,clima,wifi,ajustes,relogio");
}

static void esconder_freedoom_e_trazer_o_relogio() {
    HomeLayout l(k_cat, k_n);
    CONFERIR(l.definir_visivel(1, false));
    CONFERIR(l.definir_visivel(5, true));
    IGUAL(visiveis(l), "placar,clima,wifi,ajustes,relogio");
    IGUAL(l.texto_ocultos(), "freedoom");
}

static void ajustes_nunca_pode_ser_escondido() {
    HomeLayout l(k_cat, k_n);
    CONFERIR(!l.definir_visivel(4, false));
    CONFERIR(l.visivel(4));
    // Nem por um settings.ini editado à mão.
    l.carregar(nullptr, "ajustes,clima");
    IGUAL(visiveis(l), "placar,freedoom,wifi,ajustes,relogio");
    // E continua aparecendo depois de mudar de lugar.
    CONFERIR(l.mover(4, -1));
    CONFERIR(!l.definir_visivel(3, false));
    CONFERIR(visiveis(l).find("ajustes") != std::string::npos);
}

static void lista_de_ocultos_vazia_mostra_tudo() {
    HomeLayout l(k_cat, k_n);
    l.carregar(nullptr, "");
    IGUAL(visiveis(l), "placar,freedoom,clima,wifi,ajustes,relogio");
}

int main() {
    struct {
        const char *nome;
        void (*f)();
    } testes[] = {
        {"sem_nada_salvo_usa_o_padrao", sem_nada_salvo_usa_o_padrao},
        {"le_e_grava_o_mesmo_texto", le_e_grava_o_mesmo_texto},
        {"ids_desconhecidos_e_repetidos_sao_ignorados", ids_desconhecidos_e_repetidos_sao_ignorados},
        {"app_novo_que_nao_estava_salvo_entra_no_fim", app_novo_que_nao_estava_salvo_entra_no_fim},
        {"mover_troca_com_o_vizinho", mover_troca_com_o_vizinho},
        {"mover_alem_das_pontas_nao_faz_nada", mover_alem_das_pontas_nao_faz_nada},
        {"esconder_freedoom_e_trazer_o_relogio", esconder_freedoom_e_trazer_o_relogio},
        {"ajustes_nunca_pode_ser_escondido", ajustes_nunca_pode_ser_escondido},
        {"lista_de_ocultos_vazia_mostra_tudo", lista_de_ocultos_vazia_mostra_tudo},
    };
    for (auto &t : testes) {
        int antes = falhas;
        t.f();
        printf("%s %s\n", falhas == antes ? "PASSOU" : "FALHOU", t.nome);
    }
    printf("%d verificações, %d falhas\n", total, falhas);
    return falhas ? 1 : 0;
}
