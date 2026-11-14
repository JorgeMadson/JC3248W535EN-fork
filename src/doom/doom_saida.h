#pragma once
// Incluído à força (-include) no motor do Doom e no código comum, só no
// launcher: o que eles imprimem com printf vai para o serial_log (que não
// trava sem computador ligado) e para a tela de "Carregando".
// fprintf em arquivo de verdade (default.cfg) continua indo para o arquivo.
#include <stdarg.h>
#include <stdio.h>

int doom_printf(const char *fmt, ...);
int doom_puts(const char *s);
int doom_putchar(int c);
int doom_fprintf(FILE *f, const char *fmt, ...);
int doom_vfprintf(FILE *f, const char *fmt, va_list ap);

#define printf doom_printf
#define puts doom_puts
#define putchar doom_putchar
#define fprintf doom_fprintf
#define vfprintf doom_vfprintf
