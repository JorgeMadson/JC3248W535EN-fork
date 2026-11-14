// Ligações em C entre o motor do Doom e o launcher (compilado sem o
// doom_saida.h, para poder chamar o vfprintf de verdade).
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "serial_log.h"
#include "w_file.h"

// O motor procura primeiro um WAD na flash (w_file.c, para o firmware
// próprio). No launcher não há partição `wad`: o jogo vem só do cartão.
static wad_file_t *nada_abrir(char *path)
{
    (void) path;
    return NULL;
}

static void nada_fechar(wad_file_t *f)
{
    (void) f;
}

static size_t nada_ler(wad_file_t *f, unsigned int offset, void *buf, size_t len)
{
    (void) f;
    (void) offset;
    (void) buf;
    (void) len;
    return 0;
}

wad_file_class_t flash_wad_file = {nada_abrir, nada_fechar, nada_ler};

// ---- saída de texto do motor ----

void doom_ui_linha(const char *linha);  // doom_ui.cpp: mostra na tela de carregando

static char linha[200];
static size_t tamanho;

static void soltar_linha(void)
{
    linha[tamanho] = '\0';
    // tira espaços do começo (o motor centraliza títulos com espaços)
    const char *t = linha;
    while (*t == ' ')
    {
        t++;
    }
    if (*t)
    {
        serial_log("[doom] %s", t);
        doom_ui_linha(t);
    }
    tamanho = 0;
}

static void juntar(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (s[i] == '\n')
        {
            soltar_linha();
        }
        else if (tamanho < sizeof(linha) - 1)
        {
            linha[tamanho++] = s[i];
        }
    }
}

int doom_vfprintf(FILE *f, const char *fmt, va_list ap)
{
    if (f != stdout && f != stderr)
    {
        return vfprintf(f, fmt, ap);
    }
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0)
    {
        juntar(buf, (size_t) n < sizeof(buf) ? (size_t) n : sizeof(buf) - 1);
    }
    return n;
}

int doom_fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = doom_vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int doom_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = doom_vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int doom_puts(const char *s)
{
    juntar(s, strlen(s));
    juntar("\n", 1);
    return 1;
}

int doom_putchar(int c)
{
    char ch = (char) c;
    juntar(&ch, 1);
    return c;
}
