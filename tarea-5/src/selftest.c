/*
 * Archivo: selftest.c
 * Tarea 5: Simulación de algoritmos de administración de memoria
 * Sistemas Operativos, Equipo E04
 *
 * Descripción: Casos clásicos de validación tomados de Silberschatz y de la
 * literatura de reemplazo de páginas.  Además de comparar el número de fallos,
 * se verifican los invariantes internos de cada corrida y se hace una prueba
 * de estrés con referencias aleatorias para todos los algoritmos y varios
 * tamaños de memoria.
 */

#include "selftest.h"
#include "paging.h"

#include <stdio.h>
#include <string.h>

/* Cadena clásica de Silberschatz (10ª edición), 20 referencias. */
static const int CLASSIC[] = {
    7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1
};

/* Cadena de Belady: FIFO pasa de 9 fallos con 3 marcos a 10 con 4. */
static const int BELADY[] = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};

/*
 * @brief  Corre un algoritmo y verifica sus invariantes.
 * @param  s      destino de la simulación.
 * @param  algo   algoritmo a ejecutar.
 * @param  refs   cadena de referencias.
 * @param  n      número de referencias.
 * @param  frames número de marcos.
 * @param  bits   bits del contador de Aging.
 * @return 1 si los invariantes se cumplen, 0 si no.
 */
static int run_checked(Sim *s, Algo algo, const int *refs, int n,
                       int frames, int bits)
{
    sim_init(s, algo, frames, bits);
    sim_run(s, refs, n, NULL, NULL);

    char err[256];
    if (!stats_ok(s, n, err, sizeof err)) {
        printf("        invariantes: %s\n", err);
        return 0;
    }
    return 1;
}

/*
 * @brief  Comprueba el número de fallos de una corrida.
 * @param  label     nombre del caso.
 * @param  algo      algoritmo.
 * @param  refs      cadena.
 * @param  n         referencias.
 * @param  frames    marcos.
 * @param  bits      bits de Aging.
 * @param  expected  fallos esperados.
 * @param  pass      contador de pruebas aprobadas.
 * @param  fail      contador de pruebas fallidas.
 * @return No retorna valor.
 */
static void expect_faults(const char *label, Algo algo, const int *refs, int n,
                          int frames, int bits, int expected,
                          int *pass, int *fail)
{
    Sim s;
    if (!run_checked(&s, algo, refs, n, frames, bits)) {
        printf("[FALLO] %s: invariantes incumplidos\n", label);
        (*fail)++;
        return;
    }
    if (s.st.faults != expected) {
        printf("[FALLO] %s: esperados %d fallos, obtenidos %d\n",
               label, expected, s.st.faults);
        (*fail)++;
        return;
    }
    printf("[ OK  ] %s: %d fallos\n", label, expected);
    (*pass)++;
}

/*
 * @brief  Comprueba fallos y métricas del reloj (Second Chance).
 * @param  label           nombre del caso.
 * @param  refs            cadena.
 * @param  n               referencias.
 * @param  frames          marcos.
 * @param  expected_faults fallos esperados.
 * @param  expected_clear  bits limpiados esperados.
 * @param  expected_scans  marcos examinados esperados.
 * @param  pass            contador de pruebas aprobadas.
 * @param  fail            contador de pruebas fallidas.
 * @return No retorna valor.
 */
static void expect_second_chance(const char *label, const int *refs, int n,
                                 int frames, int expected_faults,
                                 int expected_clear, int expected_scans,
                                 int *pass, int *fail)
{
    Sim s;
    if (!run_checked(&s, ALG_SECOND_CHANCE, refs, n, frames, 8)) {
        printf("[FALLO] %s: invariantes incumplidos\n", label);
        (*fail)++;
        return;
    }
    if (s.st.faults != expected_faults ||
        s.st.ref_cleared != expected_clear ||
        s.st.scans != expected_scans) {
        printf("[FALLO] %s: esperado fallos=%d limpios=%d examinados=%d; "
               "obtenido fallos=%d limpios=%d examinados=%d\n",
               label, expected_faults, expected_clear, expected_scans,
               s.st.faults, s.st.ref_cleared, s.st.scans);
        (*fail)++;
        return;
    }
    printf("[ OK  ] %s: fallos=%d limpios=%d examinados=%d\n",
           label, expected_faults, expected_clear, expected_scans);
    (*pass)++;
}

/*
 * @brief  Caso mínimo de Second Chance: el puntero da la vuelta completa,
 *         limpia los tres bits y expulsa la página 0.
 * @param  pass  contador de pruebas aprobadas.
 * @param  fail  contador de pruebas fallidas.
 * @return No retorna valor.
 */
static void test_second_chance_hand(int *pass, int *fail)
{
    static const int refs[] = {0, 1, 2, 0, 3};
    Sim s;
    if (!run_checked(&s, ALG_SECOND_CHANCE, refs, 5, 3, 8)) {
        printf("[FALLO] Second Chance: invariantes incumplidos\n");
        (*fail)++;
        return;
    }
    if (s.st.faults != 4 || s.st.ref_cleared != 3 || s.st.scans != 4 ||
        s.fr[0].page != 3 || s.fr[1].page != 1 || s.fr[2].page != 2) {
        printf("[FALLO] Second Chance: se esperaba fallos=4 limpios=3 "
               "examinados=4 y víctima=0; obtenido fallos=%d limpios=%d "
               "examinados=%d marcos=[%d,%d,%d]\n",
               s.st.faults, s.st.ref_cleared, s.st.scans,
               s.fr[0].page, s.fr[1].page, s.fr[2].page);
        (*fail)++;
        return;
    }
    printf("[ OK  ] Second Chance: vuelta completa, 3 bits limpiados, "
           "víctima 0\n");
    (*pass)++;
}

/*
 * @brief  Comprueba que un contador de Aging decae hasta 0 tras 8 referencias
 *         sin tocar la página, mientras la página activa llega a 0xFF.
 * @param  pass  contador de pruebas aprobadas.
 * @param  fail  contador de pruebas fallidas.
 * @return No retorna valor.
 */
static void test_aging_decay(int *pass, int *fail)
{
    static const int refs[] = {5, 6, 5, 6, 6, 6, 6, 6, 6, 6, 6};
    Sim s;
    if (!run_checked(&s, ALG_AGING, refs, 11, 2, 8)) {
        printf("[FALLO] Aging: invariantes incumplidos\n");
        (*fail)++;
        return;
    }

    int p5 = -1, p6 = -1;
    for (int i = 0; i < s.frames; i++) {
        if (s.fr[i].page == 5)
            p5 = i;
        if (s.fr[i].page == 6)
            p6 = i;
    }

    if (p5 < 0 || p6 < 0 || s.fr[p5].age != 0 || s.fr[p6].age != 0xFF) {
        printf("[FALLO] Aging: se esperaba edad(5)=00 y edad(6)=FF; "
               "obtenido edad(5)=%02X edad(6)=%02X\n",
               p5 >= 0 ? s.fr[p5].age : 0,
               p6 >= 0 ? s.fr[p6].age : 0);
        (*fail)++;
        return;
    }
    printf("[ OK  ] Aging: decaimiento 80 -> 00 en 8 referencias; "
           "página activa en FF\n");
    (*pass)++;
}

/*
 * @brief  Prueba de estrés: referencias pseudoaleatorias para todos los
 *         algoritmos y 1..8 marcos.  Solo verifica invariantes.
 * @param  pass  contador de pruebas aprobadas.
 * @param  fail  contador de pruebas fallidas.
 * @return No retorna valor.
 */
static void test_stress(int *pass, int *fail)
{
    enum { N = 600, PAGES = 16 };
    static int refs[N];
    unsigned seed = 12345u;

    for (int i = 0; i < N; i++) {
        seed = seed * 1103515245u + 12345u;
        refs[i] = (int)((seed >> 16) % PAGES);
    }

    for (int frames = 1; frames <= 8; frames++) {
        for (int a = 0; a < ALG_COUNT; a++) {
            Sim s;
            if (!run_checked(&s, (Algo)a, refs, N, frames, 8)) {
                printf("[FALLO] Estrés: %s con %d marcos\n",
                       algo_name((Algo)a), frames);
                (*fail)++;
                return;
            }
        }
    }

    printf("[ OK  ] Estrés: 5 algoritmos x 8 tamaños de memoria x %d "
           "referencias\n", N);
    (*pass)++;
}

int self_test(void)
{
    int pass = 0, fail = 0;

    printf("================================================================================\n");
    printf(" Self-test: casos clásicos de reemplazo de páginas\n");
    printf("================================================================================\n");

    expect_faults("FIFO, clásica, 3 marcos", ALG_FIFO, CLASSIC, 20, 3, 8,
                  15, &pass, &fail);
    expect_faults("LRU, clásica, 3 marcos", ALG_LRU, CLASSIC, 20, 3, 8,
                  12, &pass, &fail);
    expect_faults("OPT, clásica, 3 marcos", ALG_OPT, CLASSIC, 20, 3, 8,
                  9, &pass, &fail);
    expect_second_chance("Second Chance, clásica, 3 marcos", CLASSIC, 20, 3,
                         14, 16, 27, &pass, &fail);
    expect_faults("Aging (8 bits), clásica, 3 marcos", ALG_AGING, CLASSIC, 20,
                  3, 8, 12, &pass, &fail);
    expect_faults("Aging (4 bits), clásica, 3 marcos", ALG_AGING, CLASSIC, 20,
                  3, 4, 12, &pass, &fail);

    expect_faults("FIFO, clásica, 4 marcos", ALG_FIFO, CLASSIC, 20, 4, 8,
                  10, &pass, &fail);
    expect_faults("LRU, clásica, 4 marcos", ALG_LRU, CLASSIC, 20, 4, 8,
                  8, &pass, &fail);
    expect_faults("OPT, clásica, 4 marcos", ALG_OPT, CLASSIC, 20, 4, 8,
                  8, &pass, &fail);
    expect_second_chance("Second Chance, clásica, 4 marcos", CLASSIC, 20, 4,
                         9, 10, 15, &pass, &fail);
    expect_faults("Aging (8 bits), clásica, 4 marcos", ALG_AGING, CLASSIC, 20,
                  4, 8, 8, &pass, &fail);

    expect_faults("FIFO, Belady, 3 marcos", ALG_FIFO, BELADY, 12, 3, 8,
                  9, &pass, &fail);
    expect_faults("FIFO, Belady, 4 marcos (anomalía)", ALG_FIFO, BELADY, 12,
                  4, 8, 10, &pass, &fail);
    expect_faults("LRU, Belady, 3 marcos", ALG_LRU, BELADY, 12, 3, 8,
                  10, &pass, &fail);
    expect_faults("LRU, Belady, 4 marcos", ALG_LRU, BELADY, 12, 4, 8,
                  8, &pass, &fail);
    expect_faults("OPT, Belady, 3 marcos", ALG_OPT, BELADY, 12, 3, 8,
                  7, &pass, &fail);
    expect_faults("OPT, Belady, 4 marcos", ALG_OPT, BELADY, 12, 4, 8,
                  6, &pass, &fail);
    expect_faults("Second Chance, Belady, 3 marcos", ALG_SECOND_CHANCE,
                  BELADY, 12, 3, 8, 9, &pass, &fail);
    expect_faults("Second Chance, Belady, 4 marcos (anomalía)",
                  ALG_SECOND_CHANCE, BELADY, 12, 4, 8, 10, &pass, &fail);
    expect_faults("Aging (8 bits), Belady, 3 marcos", ALG_AGING, BELADY, 12,
                  3, 8, 10, &pass, &fail);
    expect_faults("Aging (8 bits), Belady, 4 marcos", ALG_AGING, BELADY, 12,
                  4, 8, 8, &pass, &fail);

    test_second_chance_hand(&pass, &fail);
    test_aging_decay(&pass, &fail);
    test_stress(&pass, &fail);

    printf("--------------------------------------------------------------------------------\n");
    printf("Self-test: %d comprobaciones, %d fallos.\n", pass + fail, fail);
    return fail == 0 ? 0 : 1;
}
