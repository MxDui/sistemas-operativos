/*
 * Archivo: main.c
 * Tarea 5: Simulación de algoritmos de administración de memoria
 * Sistemas Operativos, Equipo E04
 *
 * Descripción: Interfaz de línea de comandos del simulador.  Lee la cadena de
 * referencias, ejecuta los algoritmos seleccionados (Second Chance y Aging) y
 * las líneas base (FIFO, LRU, OPT), y presenta los resultados en texto o CSV.
 */

#include "paging.h"
#include "selftest.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_FRAMES   4
#define DEFAULT_AGE_BITS 8
#define DEFAULT_TRACE    "traces/clasica.txt"

/* Orden de ejecución e impresión cuando se pide "all". */
static const Algo ORDER[ALG_COUNT] = {
    ALG_SECOND_CHANCE, ALG_AGING, ALG_FIFO, ALG_LRU, ALG_OPT
};

/*
 * @brief  Imprime la ayuda del programa.
 * @param  out  flujo de salida (stdout o stderr).
 * @return No retorna valor.
 */
static void usage(FILE *out)
{
    fprintf(out,
            "Uso: ./simulador [opciones] [archivo]\n"
            "\n"
            "  -a, --algo NOMBRE    second|aging|fifo|lru|opt|all (default: all)\n"
            "  -m, --marcos N       número de marcos de memoria (default: %d)\n"
            "  -b, --bits N         bits del contador de Aging (default: %d)\n"
            "  -t, --traza          traza paso a paso en texto\n"
            "  -c, --csv            resumen en CSV\n"
            "  -T, --csv-traza      traza paso a paso en CSV (requiere -a)\n"
            "  -s, --self-test      casos clásicos de validación\n"
            "  -h, --help           esta ayuda\n"
            "\n"
            "Archivo '-' = stdin.  Sin archivo: %s si existe; si no, stdin.\n",
            DEFAULT_FRAMES, DEFAULT_AGE_BITS, DEFAULT_TRACE);
}

/*
 * @brief  Imprime un error y termina con código 1.
 * @param  fmt  formato estilo printf.
 * @return No retorna (llama a exit).
 */
static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "Error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

/*
 * @brief  Convierte un argumento entero y valida su rango.
 * @param  s    cadena recibida de la línea de comandos.
 * @param  min  valor mínimo aceptado.
 * @param  max  valor máximo aceptado.
 * @param  what nombre del parámetro para el mensaje de error.
 * @return Valor convertido.
 */
static int parse_int(const char *s, int min, int max, const char *what)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < min || v > max)
        die("%s inválido: '%s' (rango %d..%d)", what, s, min, max);
    return (int)v;
}

/*
 * @brief  Indica si un archivo se puede abrir para lectura.
 * @param  path  ruta del archivo.
 * @return 1 si existe y se puede abrir, 0 si no.
 */
static int file_exists(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return 0;
    fclose(fp);
    return 1;
}

/*
 * @brief  Lee una cadena de referencias de un archivo o de stdin.
 *
 * Formato: enteros separados por espacios, tabuladores o comas.  Todo lo que
 * sigue a '#' en una línea es comentario.  Se valida que cada página esté en
 * [0, MAX_PAGES) y que no se exceda MAX_REFS.
 *
 * @param  path  ruta del archivo, "-" para stdin, o NULL para stdin.
 * @param  refs  arreglo destino.
 * @param  n     puntero al contador de referencias leídas.
 * @return No retorna valor.
 */
static void read_refs(const char *path, int *refs, int *n)
{
    FILE *fp;
    if (path == NULL || strcmp(path, "-") == 0) {
        fp = stdin;
        path = "<stdin>";
    } else {
        fp = fopen(path, "r");
        if (!fp)
            die("no se pudo abrir '%s': %s", path, strerror(errno));
    }

    int lineno = 1;
    int ch;
    int comment = 0;
    int in_number = 0;
    unsigned page = 0;
    *n = 0;

    /* Leer por carácter conserva números y comentarios de cualquier longitud. */
    while ((ch = fgetc(fp)) != EOF) {
        if (comment) {
            if (ch == '\n') {
                comment = 0;
                lineno++;
            }
            continue;
        }
        if (ch >= '0' && ch <= '9') {
            unsigned digit = (unsigned)(ch - '0');
            if (page > (MAX_PAGES - 1u - digit) / 10u)
                die("página fuera de rango [0,%d) en %s:%d",
                    MAX_PAGES, path, lineno);
            page = page * 10u + digit;
            in_number = 1;
            continue;
        }
        if (ch != '#' && ch != ',' && !isspace((unsigned char)ch))
            die("carácter inválido en %s:%d; se esperan páginas enteras "
                "no negativas separadas por espacios o comas", path, lineno);
        if (in_number) {
            if (*n >= MAX_REFS)
                die("demasiadas referencias (máximo %d)", MAX_REFS);
            refs[(*n)++] = (int)page;
            page = 0;
            in_number = 0;
        }
        if (ch == '#')
            comment = 1;
        if (ch == '\n')
            lineno++;
    }

    if (ferror(fp))
        die("error leyendo '%s'", path);
    if (in_number) {
        if (*n >= MAX_REFS)
            die("demasiadas referencias (máximo %d)", MAX_REFS);
        refs[(*n)++] = (int)page;
    }
    if (fp != stdin)
        fclose(fp);
}

/*
 * @brief  Construye el estado de los marcos para la traza en texto.
 * @param  s    simulación activa.
 * @param  buf  buffer destino.
 * @param  sz   tamaño del buffer.
 * @return No retorna valor.
 */
static void state_human(const Sim *s, char *buf, size_t sz)
{
    size_t off = 0;
    int hexw = (s->age_bits + 3) / 4;

#define APPEND(...)                                                            \
    do {                                                                       \
        if (off < sz) {                                                        \
            int w = snprintf(buf + off, sz - off, __VA_ARGS__);                \
            if (w > 0)                                                         \
                off += (size_t)w;                                              \
        }                                                                      \
    } while (0)

    APPEND("[");
    for (int i = 0; i < s->frames; i++) {
        if (i > 0)
            APPEND(" ");
        if (s->fr[i].page < 0) {
            APPEND("-");
        } else if (s->algo == ALG_SECOND_CHANCE) {
            APPEND("%d:%u", s->fr[i].page, s->fr[i].ref);
        } else if (s->algo == ALG_AGING) {
            APPEND("%d:%0*X", s->fr[i].page, hexw, s->fr[i].age);
        } else {
            APPEND("%d", s->fr[i].page);
        }
    }
    APPEND("]");

#undef APPEND
}

/*
 * @brief  Construye el estado de los marcos para la traza en CSV.
 *         Usa '|' como separador interno para no romper el CSV.
 * @param  s    simulación activa.
 * @param  buf  buffer destino.
 * @param  sz   tamaño del buffer.
 * @return No retorna valor.
 */
static void state_csv(const Sim *s, char *buf, size_t sz)
{
    size_t off = 0;
    int hexw = (s->age_bits + 3) / 4;

#define APPEND(...)                                                            \
    do {                                                                       \
        if (off < sz) {                                                        \
            int w = snprintf(buf + off, sz - off, __VA_ARGS__);                \
            if (w > 0)                                                         \
                off += (size_t)w;                                              \
        }                                                                      \
    } while (0)

    for (int i = 0; i < s->frames; i++) {
        if (i > 0)
            APPEND("|");
        if (s->fr[i].page < 0) {
            APPEND("-");
        } else if (s->algo == ALG_SECOND_CHANCE) {
            APPEND("%d:%u", s->fr[i].page, s->fr[i].ref);
        } else if (s->algo == ALG_AGING) {
            APPEND("%d:%0*X", s->fr[i].page, hexw, s->fr[i].age);
        } else {
            APPEND("%d", s->fr[i].page);
        }
    }

#undef APPEND
}

/*
 * @brief  Imprime el encabezado de la traza en texto.
 * @param  s  simulación a punto de correr.
 * @return No retorna valor.
 */
static void trace_header_human(const Sim *s)
{
    printf("\n");
    printf("Traza paso a paso: %s, %d marcos, %d bits\n",
           algo_name(s->algo), s->frames, s->age_bits);
    printf("%4s %4s  %-7s  %7s  %-40s  %4s  %s\n",
           "Paso", "Ref", "Result.", "Víctima", "Estado", "Mano", "Notas");
    printf("--------------------------------------------------------------------------------\n");
}

/*
 * @brief  Callback de traza en texto.
 * @param  s    simulación activa.
 * @param  ev   evento de la referencia.
 * @param  user contexto (no usado).
 * @return No retorna valor.
 */
static void trace_human(const Sim *s, const TraceEvent *ev, void *user)
{
    (void)user;
    char state[4096];
    char victim[32];
    char hand[16];
    char notes[128] = "";
    int hexw = (s->age_bits + 3) / 4;

    state_human(s, state, sizeof state);
    if (ev->victim >= 0)
        snprintf(victim, sizeof victim, "%d", ev->victim);
    else
        snprintf(victim, sizeof victim, "-");

    if (s->algo == ALG_SECOND_CHANCE || s->algo == ALG_FIFO)
        snprintf(hand, sizeof hand, "%d", ev->hand);
    else
        snprintf(hand, sizeof hand, "-");

    if (ev->free_frame >= 0)
        snprintf(notes, sizeof notes, "marco libre %d", ev->free_frame);
    else if (!ev->hit && s->algo == ALG_SECOND_CHANCE)
        snprintf(notes, sizeof notes, "limpios=%d examinados=%d",
                 ev->cleared, ev->scans);
    else if (!ev->hit && s->algo == ALG_AGING)
        snprintf(notes, sizeof notes, "edad=%0*X", hexw, ev->victim_age);

    printf("%4d %4d  %-7s  %7s  %-40s  %4s  %s\n",
           ev->step, ev->page, ev->hit ? "acierto" : "fallo",
           victim, state, hand, notes);
}

/*
 * @brief  Imprime el encabezado de la traza en CSV.
 * @return No retorna valor.
 */
static void trace_header_csv(void)
{
    printf("paso,pagina,resultado,victima,marco_libre,mano,"
           "marcos_examinados,bits_limpiados,edad_victima,estado\n");
}

/*
 * @brief  Callback de traza en CSV.
 * @param  s    simulación activa.
 * @param  ev   evento de la referencia.
 * @param  user contexto (no usado).
 * @return No retorna valor.
 */
static void trace_csv(const Sim *s, const TraceEvent *ev, void *user)
{
    (void)user;
    char state[4096];
    state_csv(s, state, sizeof state);
    int hand = (s->algo == ALG_SECOND_CHANCE || s->algo == ALG_FIFO)
                   ? ev->hand
                   : -1;
    printf("%d,%d,%s,%d,%d,%d,%d,%d,%u,%s\n",
           ev->step, ev->page, ev->hit ? "acierto" : "fallo",
           ev->victim, ev->free_frame, hand, ev->scans, ev->cleared,
           ev->victim_age, state);
}

/*
 * @brief  Imprime el bloque detallado de una corrida.
 * @param  s      simulación terminada.
 * @param  nrefs  número de referencias.
 * @return No retorna valor.
 */
static void print_block(const Sim *s, int nrefs)
{
    printf("\n");
    printf("================================================================================\n");
    printf(" %s, %d marcos, %d referencias\n",
           algo_name(s->algo), s->frames, nrefs);
    printf("================================================================================\n");
    printf("  Fallos de página      : %8d\n", s->st.faults);
    printf("  Aciertos              : %8d\n", s->st.hits);
    printf("  Tasa de fallos        : %7.2f %%\n",
           stats_fault_rate(&s->st, nrefs) * 100.0);
    printf("  Expulsiones           : %8d\n", s->st.evictions);
    printf("  Cargas en marco libre : %8d\n", s->st.free_loads);
    printf("  Residencia media      : %8.2f referencias\n",
           stats_avg_residency(&s->st));

    if (s->algo == ALG_SECOND_CHANCE) {
        printf("  Bits limpiados        : %8d\n", s->st.ref_cleared);
        printf("  Marcos examinados     : %8d\n", s->st.scans);
        printf("  Avances de mano       : %8d\n", s->st.hand_steps);
    } else if (s->algo == ALG_AGING) {
        printf("  Empates en el mínimo  : %8d\n", s->st.age_ties);
        printf("  Edad media de víctima : %8.2f\n",
               stats_avg_victim_age(&s->st));
    }
}

/*
 * @brief  Imprime la tabla comparativa de una corrida "all".
 * @param  runs   arreglo de resultados por algoritmo.
 * @param  nrefs  número de referencias.
 * @return No retorna valor.
 */
static void print_compact_table(const Sim *runs, int nrefs)
{
    printf("\n");
    printf("================================================================================\n");
    printf(" RESUMEN: %d marcos, %d referencias\n",
           runs[0].frames, nrefs);
    printf("================================================================================\n");
    printf("%-16s %7s %9s %12s %11s %13s\n",
           "Algoritmo", "Fallos", "Aciertos", "Tasa fallos",
           "Expuls.", "Resid. media");
    printf("--------------------------------------------------------------------------------\n");
    for (int k = 0; k < ALG_COUNT; k++) {
        const Sim *s = &runs[k];
        printf("%-16s %7d %9d %11.2f%% %11d %13.2f\n",
               algo_name(s->algo), s->st.faults, s->st.hits,
               stats_fault_rate(&s->st, nrefs) * 100.0,
               s->st.evictions, stats_avg_residency(&s->st));
    }
    printf("--------------------------------------------------------------------------------\n");

    const Sim *sc = &runs[0];
    const Sim *ag = &runs[1];
    printf("  Second Chance: bits limpiados=%d, marcos examinados=%d, "
           "avances de mano=%d\n",
           sc->st.ref_cleared, sc->st.scans, sc->st.hand_steps);
    printf("  Aging: empates=%d, edad media de la víctima=%.2f\n",
           ag->st.age_ties, stats_avg_victim_age(&ag->st));
}

/*
 * @brief  Imprime el encabezado del resumen CSV.
 * @return No retorna valor.
 */
static void csv_header(void)
{
    printf("algoritmo,marcos,referencias,fallos,aciertos,tasa_fallos,"
           "expulsiones,cargas_libres,residencia_media,bits_limpiados,"
           "marcos_examinados,avances_mano,empates,edad_media_victima\n");
}

/*
 * @brief  Imprime una fila del resumen CSV.
 * @param  s      simulación terminada.
 * @param  nrefs  número de referencias.
 * @return No retorna valor.
 */
static void csv_row(const Sim *s, int nrefs)
{
    printf("%s,%d,%d,%d,%d,%.6f,%d,%d,%.6f,%d,%d,%d,%d,%.6f\n",
           algo_key(s->algo), s->frames, nrefs, s->st.faults, s->st.hits,
           stats_fault_rate(&s->st, nrefs), s->st.evictions,
           s->st.free_loads, stats_avg_residency(&s->st),
           s->st.ref_cleared, s->st.scans, s->st.hand_steps,
           s->st.age_ties, stats_avg_victim_age(&s->st));
}

/*
 * @brief  Ejecuta un algoritmo y verifica sus invariantes.
 * @param  out    destino del estado de la simulación.
 * @param  algo   algoritmo a ejecutar.
 * @param  refs   cadena de referencias.
 * @param  nrefs  número de referencias.
 * @param  frames número de marcos.
 * @param  bits   bits del contador de Aging.
 * @param  trace  1 para emitir traza, 0 para no hacerlo.
 * @param  csv    1 si la traza es CSV, 0 si es texto.
 * @return No retorna valor.
 */
static void run_one(Sim *out, Algo algo, const int *refs, int nrefs,
                    int frames, int bits, int trace, int csv)
{
    sim_init(out, algo, frames, bits);
    if (trace) {
        if (csv)
            trace_header_csv();
        else
            trace_header_human(out);
    }
    sim_run(out, refs, nrefs, trace ? (csv ? trace_csv : trace_human) : NULL,
            NULL);

    char err[256];
    if (!stats_ok(out, nrefs, err, sizeof err))
        die("invariantes de %s: %s", algo_name(algo), err);
}

int main(int argc, char **argv)
{
    int frames = DEFAULT_FRAMES;
    int bits = DEFAULT_AGE_BITS;
    int algo_sel = -1; /* -1 = all */
    int trace = 0;
    int csv = 0;
    int csv_trace = 0;
    int self_test_flag = 0;

    static struct option long_opts[] = {
        {"algo",       required_argument, 0, 'a'},
        {"marcos",     required_argument, 0, 'm'},
        {"frames",     required_argument, 0, 'm'},
        {"bits",       required_argument, 0, 'b'},
        {"traza",      no_argument,       0, 't'},
        {"trace",      no_argument,       0, 't'},
        {"csv",        no_argument,       0, 'c'},
        {"csv-traza",  no_argument,       0, 'T'},
        {"csv-trace",  no_argument,       0, 'T'},
        {"self-test",  no_argument,       0, 's'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "a:m:b:tcTsh", long_opts, NULL)) != -1) {
        switch (c) {
        case 'a':
            if (strcmp(optarg, "all") == 0) {
                algo_sel = -1;
            } else {
                Algo a;
                if (!algo_parse(optarg, &a))
                    die("algoritmo desconocido: '%s' (second|aging|fifo|lru|opt|all)",
                        optarg);
                algo_sel = (int)a;
            }
            break;
        case 'm':
            frames = parse_int(optarg, 1, MAX_FRAMES, "número de marcos");
            break;
        case 'b':
            bits = parse_int(optarg, 1, MAX_AGE_BITS, "bits de Aging");
            break;
        case 't':
            trace = 1;
            break;
        case 'c':
            csv = 1;
            break;
        case 'T':
            csv_trace = 1;
            break;
        case 's':
            self_test_flag = 1;
            break;
        case 'h':
            usage(stdout);
            return 0;
        default:
            usage(stderr);
            return 1;
        }
    }

    if (self_test_flag)
        return self_test();

    if (trace && csv)
        die("--traza y --csv son excluyentes");
    if (csv_trace && (trace || csv))
        die("--csv-traza no se combina con --traza ni --csv");
    if (csv_trace && algo_sel < 0)
        die("--csv-traza requiere un algoritmo concreto (-a second|aging|fifo|lru|opt)");

    if (optind < argc - 1)
        die("argumentos de más; solo se acepta un archivo");

    const char *path = (optind < argc) ? argv[optind] : NULL;
    if (path == NULL && file_exists(DEFAULT_TRACE))
        path = DEFAULT_TRACE;

    static int refs[MAX_REFS];
    int nrefs = 0;
    read_refs(path, refs, &nrefs);
    if (nrefs == 0)
        die("la cadena de referencias está vacía");

    if (csv_trace) {
        Sim s;
        run_one(&s, (Algo)algo_sel, refs, nrefs, frames, bits, 1, 1);
        return 0;
    }

    if (algo_sel >= 0) {
        Sim s;
        run_one(&s, (Algo)algo_sel, refs, nrefs, frames, bits, trace, 0);
        if (csv) {
            csv_header();
            csv_row(&s, nrefs);
        } else {
            print_block(&s, nrefs);
        }
        return 0;
    }

    Sim runs[ALG_COUNT];
    for (int k = 0; k < ALG_COUNT; k++)
        run_one(&runs[k], ORDER[k], refs, nrefs, frames, bits, trace, 0);

    if (csv) {
        csv_header();
        for (int k = 0; k < ALG_COUNT; k++)
            csv_row(&runs[k], nrefs);
    } else {
        print_compact_table(runs, nrefs);
    }

    return 0;
}
