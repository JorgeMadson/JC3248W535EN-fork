#include "home_apps.h"

#include <string.h>

HomeLayout::HomeLayout(const HomeAppInfo *catalogo, int n) : cat_(catalogo), n_(n > kMax ? kMax : n) {
    carregar(nullptr, nullptr);
}

int HomeLayout::achar(const std::string &id) const {
    for (int i = 0; i < n_; i++) {
        if (id == cat_[i].id) {
            return i;
        }
    }
    return -1;
}

/** Chama f(id) para cada item de uma lista separada por vírgulas (espaços em volta são ignorados). */
template <typename F> static void cada_id(const char *lista, F f) {
    std::string atual;
    for (const char *p = lista;; p++) {
        if (*p == ',' || *p == '\0') {
            size_t a = atual.find_first_not_of(" \t"), b = atual.find_last_not_of(" \t\r");
            if (a != std::string::npos) {
                f(atual.substr(a, b - a + 1));
            }
            atual.clear();
            if (*p == '\0') {
                break;
            }
        } else {
            atual += *p;
        }
    }
}

void HomeLayout::carregar(const char *ordem, const char *ocultos) {
    bool usado[kMax] = {false};
    int k = 0;
    if (ordem) {
        cada_id(ordem, [&](const std::string &id) {
            int i = achar(id);
            if (i >= 0 && !usado[i]) {
                usado[i] = true;
                ordem_[k++] = i;
            }
        });
    }
    /* O que não estava na lista (ou tudo, sem lista) entra no fim, na ordem do catálogo. */
    for (int i = 0; i < n_; i++) {
        if (!usado[i]) {
            ordem_[k++] = i;
        }
    }

    for (int i = 0; i < n_; i++) {
        oculto_[i] = ocultos ? false : !cat_[i].visivel_padrao;
    }
    if (ocultos) {
        cada_id(ocultos, [&](const std::string &id) {
            int i = achar(id);
            if (i >= 0) {
                oculto_[i] = true;
            }
        });
    }
    for (int i = 0; i < n_; i++) {
        if (cat_[i].fixo) {
            oculto_[i] = false;
        }
    }
}

std::string HomeLayout::texto_ordem() const {
    std::string s;
    for (int p = 0; p < n_; p++) {
        if (p) {
            s += ',';
        }
        s += cat_[ordem_[p]].id;
    }
    return s;
}

std::string HomeLayout::texto_ocultos() const {
    std::string s;
    for (int p = 0; p < n_; p++) {
        if (oculto_[ordem_[p]]) {
            if (!s.empty()) {
                s += ',';
            }
            s += cat_[ordem_[p]].id;
        }
    }
    return s;
}

bool HomeLayout::visivel(int pos) const {
    return !oculto_[ordem_[pos]];
}

bool HomeLayout::mover(int pos, int delta) {
    int outra = pos + delta;
    if (pos < 0 || pos >= n_ || outra < 0 || outra >= n_ || delta == 0) {
        return false;
    }
    int t = ordem_[pos];
    ordem_[pos] = ordem_[outra];
    ordem_[outra] = t;
    return true;
}

bool HomeLayout::definir_visivel(int pos, bool v) {
    if (pos < 0 || pos >= n_) {
        return false;
    }
    int i = ordem_[pos];
    if (!v && cat_[i].fixo) {
        return false;
    }
    oculto_[i] = !v;
    return true;
}

int HomeLayout::visiveis(int *saida, int max) const {
    int k = 0;
    for (int p = 0; p < n_ && k < max; p++) {
        if (!oculto_[ordem_[p]]) {
            saida[k++] = ordem_[p];
        }
    }
    return k;
}
