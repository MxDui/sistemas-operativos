/*
 * Archivo: simulador.c
 * Tarea 3 — Simulación de algoritmos de planificación de CPU
 * Sistemas Operativos
 *
 * Descripción: Interfaz de línea de comandos del simulador.  Interpreta las
 * opciones, carga los procesos, corre los seis algoritmos sobre copias
 * independientes, verifica los invariantes, imprime métricas y Gantt, y
 * contiene los self-tests con casos clásicos de libro.
 */

#include "process.h"
#include "schedulers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Firma común de los algoritmos sin quantum (FCFS, SJF, SRTN y Priority) */
typedef void (*SimFn)(Process *, int, Gantt *, Metrics *);

/*
 * @brief  Comparador de qsort por id, para imprimir la tabla por proceso en
 *         orden de PID.
 * @param  a  puntero a Process (lo entrega qsort).
 * @param  b  puntero a Process (lo entrega qsort).
 * @return Negativo si a->id < b->id, 0 si son iguales, positivo si es mayor.
 */
static int cmp_id(const void *a, const void *b)
{
    const Process *pa = a, *pb = b;
    return pa->id - pb->id;
}

/*
 * @brief  Imprime el encabezado de una sección, enmarcado con líneas.
 * @param  name  título de la sección.
 * @return No retorna valor.
 */
static void print_header(const char *name)
{
    printf("\n");
    printf("================================================================================\n");
    printf(" %s\n", name);
    printf("================================================================================\n");
}

/*
 * @brief  Imprime el bloque de métricas globales de una corrida.
 * @param  m  métricas a imprimir (no debe ser NULL).
 * @return No retorna valor.
 */
static void print_metrics(const Metrics *m)
{
    printf("--------------------------------------------------------------------------------\n");
    printf("  Promedio espera      : %8.4f\n", m->avg_wait);
    printf("  Promedio retorno     : %8.4f\n", m->avg_tat);
    printf("  Promedio respuesta   : %8.4f\n", m->avg_resp);
    printf("  Utilización CPU      : %7.2f %%\n", m->cpu_util);
    printf("  Throughput           : %8.4f proc/u.t.\n", m->throughput);
    printf("  Cambios de contexto  : %8d\n", m->context_switches);
    printf("  Makespan             : %8d\n", m->makespan);
    printf("  Tiempo ocioso        : %8d\n", m->idle_time);
    printf("  Ráfaga total         : %8ld\n", m->total_burst);
    printf("  Último fin           : %8d\n", m->last_finish);
}

/*
 * @brief  Imprime una fila por proceso con sus tiempos, ordenadas por PID.
 * @param  p  procesos ya simulados.
 * @param  n  número de procesos.
 * @return No retorna valor.
 * @note   qsort reordena el arreglo p; se llama con la corrida ya terminada,
 *         así que el orden no afecta los resultados.
 */
static void print_table(Process *p, int n)
{
    qsort(p, (size_t)n, sizeof(Process), cmp_id);
    printf("  PID  Llegada  Ráfaga  Prio  Inicio   Fin  Espera  Retorno  Respuesta\n");
    for (int i = 0; i < n; i++) {
        printf("  %3d  %7d  %6d  %4d  %6d  %4d  %6d  %7d  %9d\n",
               p[i].id, p[i].arrival, p[i].burst, p[i].priority,
               p[i].start_time, p[i].finish_time, p[i].waiting_time,
               p[i].turnaround, p[i].response_time);
    }
}

/*
 * @brief  Compara dos doubles con tolerancia, para los self-tests.
 * @param  a  valor obtenido.
 * @param  b  valor esperado.
 * @return 1 si |a - b| < 1e-6, 0 en caso contrario.
 * @note   Los promedios se calculan por una ruta distinta a la de las
 *         fracciones esperadas del test; la tolerancia evita falsos negativos
 *         por redondeo binario.
 */
static int almost(double a, double b)
{
    double d = a - b;
    if (d < 0)
        d = -d;
    return d < 1e-6;
}

/*
 * @brief  Comprueba los tres promedios de una corrida contra los esperados y
 *         reporta el resultado en una línea.
 * @param  name  nombre del caso, para la salida.
 * @param  m     métricas obtenidas.
 * @param  wait  espera media esperada.
 * @param  tat   retorno medio esperado.
 * @param  resp  respuesta media esperada.
 * @return 1 si los tres promedios coinciden dentro de la tolerancia, 0 si no.
 */
static int expect_avgs(const char *name, const Metrics *m,
                       double wait, double tat, double resp)
{
    int ok = almost(m->avg_wait, wait) &&
             almost(m->avg_tat, tat) &&
             almost(m->avg_resp, resp);
    printf("  %-28s  wait=%7.4f (esp %7.4f)  tat=%7.4f  resp=%7.4f  %s\n",
           name, m->avg_wait, wait, m->avg_tat, m->avg_resp,
           ok ? "OK" : "FALLO");
    return ok;
}

/*
 * @brief  Corre los casos clásicos (Silberschatz / Tanenbaum) para validar la
 *         implementación antes de correr los 100 procesos de prueba.
 * @return 0 si todos los casos pasan, 1 si falla alguno.
 * @note   Cada caso se ejecuta sobre una copia del arreglo original (work) y
 *         suma a fails si los invariantes o los promedios no cuadran.
 */
static int run_self_tests(void)
{
    Process work[8];
    Gantt g;
    Metrics m;
    int fails = 0;

    printf("Self-test de algoritmos de planificación\n");
    printf("----------------------------------------\n");

    /* FCFS / SJF / RR: tres procesos que llegan en t=0 */
    Process abc[] = {
        { .id = 1, .arrival = 0, .burst = 24, .priority = 1 },
        { .id = 2, .arrival = 0, .burst =  3, .priority = 1 },
        { .id = 3, .arrival = 0, .burst =  3, .priority = 1 },
    };

    process_copy(work, abc, 3);
    simulate_fcfs(work, 3, &g, &m);
    if (!processes_ok(work, 3) || !expect_avgs("FCFS", &m, 17.0, 27.0, 17.0))
        fails++;

    process_copy(work, abc, 3);
    simulate_sjf(work, 3, &g, &m);
    if (!processes_ok(work, 3) || !expect_avgs("SJF", &m, 3.0, 13.0, 3.0))
        fails++;

    process_copy(work, abc, 3);
    simulate_rr(work, 3, 4, &g, &m);
    if (!processes_ok(work, 3) || !expect_avgs("RR q=4", &m, 17.0 / 3.0, 47.0 / 3.0, 11.0 / 3.0))
        fails++;

    /* SRTN clásico: P1=8@0, P2=4@1, P3=9@2, P4=5@3 → avg wait 6.5 */
    Process srtn[] = {
        { .id = 1, .arrival = 0, .burst = 8, .priority = 1 },
        { .id = 2, .arrival = 1, .burst = 4, .priority = 1 },
        { .id = 3, .arrival = 2, .burst = 9, .priority = 1 },
        { .id = 4, .arrival = 3, .burst = 5, .priority = 1 },
    };
    process_copy(work, srtn, 4);
    simulate_srtn(work, 4, &g, &m);
    if (!processes_ok(work, 4) || !expect_avgs("SRTN", &m, 6.5, 13.0, 4.25))
        fails++;

    /* Priority no expulsivo, menor número = mayor prioridad */
    Process prio[] = {
        { .id = 1, .arrival = 0, .burst = 10, .priority = 3 },
        { .id = 2, .arrival = 0, .burst =  1, .priority = 1 },
        { .id = 3, .arrival = 0, .burst =  2, .priority = 4 },
        { .id = 4, .arrival = 0, .burst =  1, .priority = 2 },
    };
    process_copy(work, prio, 4);
    simulate_priority(work, 4, &g, &m);
    if (!processes_ok(work, 4) || !expect_avgs("Priority", &m, 3.75, 7.25, 3.75))
        fails++;

    /*
     * Conservación con hueco ocioso y primera llegada en t=3:
     *   P1 3..8, idle 8..10, P2 10..12, P3 12..14
     *   burst 9 + idle 2 = makespan 11 (14 - 3); utilización 9/11
     */
    Process mix[] = {
        { .id = 1, .arrival = 3,  .burst = 5, .priority = 2 },
        { .id = 2, .arrival = 10, .burst = 2, .priority = 1 },
        { .id = 3, .arrival = 10, .burst = 2, .priority = 3 },
    };
    process_copy(work, mix, 3);
    simulate_rr(work, 3, 2, &g, &m);
    if (!processes_ok(work, 3) || m.idle_time != 2 || m.makespan != 11 ||
        m.total_burst + m.idle_time != m.makespan ||
        !almost(m.cpu_util, 100.0 * 9.0 / 11.0))
        /* Operador coma: imprime el fallo y suma 1 en la misma sentencia */
        fails += (printf("  Conservación / idle RR q=2 (llegada en t=3)   FALLO\n"), 1);
    else
        printf("  Conservación / idle RR q=2 (llegada en t=3)   OK\n");

    /*
     * Llegadas simultáneas escritas en desorden en el archivo: la cola RR
     * debe admitirlas por (arrival, id), no por posición.
     *   t=0: P3, P1 llegan -> cola [P1, P3]; P1 0..2 expira -> cola [P3, P1]
     *   P3 2..3 termina; P1 3..5 termina.  start: P1=0, P3=2.
     */
    Process sim[] = {
        { .id = 3, .arrival = 0, .burst = 1, .priority = 1 },
        { .id = 1, .arrival = 0, .burst = 4, .priority = 1 },
    };
    process_copy(work, sim, 2);
    simulate_rr(work, 2, 2, &g, &m);
    if (!processes_ok(work, 2) ||
        work[0].id != 3 || work[0].start_time != 2 || work[0].finish_time != 3 ||
        work[1].id != 1 || work[1].start_time != 0 || work[1].finish_time != 5)
        fails += (printf("  RR desempate llegadas simultáneas por id   FALLO\n"), 1);
    else
        printf("  RR desempate llegadas simultáneas por id   OK\n");

    /*
     * MLQ q=2: una cola por prioridad, RR dentro de cada cola.  P1 (prio 2)
     * no toca la CPU hasta que la cola de prioridad 1 se vacía; P2 y P3 se
     * reparten por RR.  Espera media 3, TAT 19/3, respuesta 7/3.
     */
    Process mlq[] = {
        { .id = 1, .arrival = 0, .burst = 4, .priority = 2 },
        { .id = 2, .arrival = 0, .burst = 4, .priority = 1 },
        { .id = 3, .arrival = 1, .burst = 2, .priority = 1 },
    };
    process_copy(work, mlq, 3);
    simulate_multilevel(work, 3, 2, &g, &m);
    if (!processes_ok(work, 3) || !expect_avgs("MLQ q=2", &m, 3.0, 19.0 / 3.0, 7.0 / 3.0))
        fails++;

    /*
     * MLQ q=4: prioridad absoluta entre colas.  P2 (prio 1) llega en t=2 y
     * expulsa a P1 (prio 3); P1 vuelve al frente de su cola y reanuda en
     * t=4 (fin 10).  Si no hubiera expulsión, P1 terminaría en t=8.
     */
    Process mlqp[] = {
        { .id = 1, .arrival = 0, .burst = 8, .priority = 3 },
        { .id = 2, .arrival = 2, .burst = 2, .priority = 1 },
    };
    process_copy(work, mlqp, 2);
    simulate_multilevel(work, 2, 4, &g, &m);
    if (!processes_ok(work, 2) ||
        !expect_avgs("MLQ expulsión entre colas", &m, 1.0, 6.0, 0.0) ||
        m.context_switches != 2)
        fails++;

    /*
     * MLQ q=4: el quantum de P1 expira en t=4 justo cuando llega P2 a la
     * misma cola; la llegada debe entrar antes que el expirado.
     *   P1 0..4, P2 4..6, P1 6..10.  Espera media 1, TAT 6, respuesta 0.
     */
    Process mlqe[] = {
        { .id = 1, .arrival = 0, .burst = 8, .priority = 1 },
        { .id = 2, .arrival = 4, .burst = 2, .priority = 1 },
    };
    process_copy(work, mlqe, 2);
    simulate_multilevel(work, 2, 4, &g, &m);
    if (!processes_ok(work, 2) ||
        !expect_avgs("MLQ expiración = llegada", &m, 1.0, 6.0, 0.0))
        fails++;

    printf("----------------------------------------\n");
    if (fails == 0)
        /* 10 = cantidad de casos que se ejecutan arriba */
        printf("Self-test: %d comprobaciones, 0 fallos.\n", 10);
    else
        printf("Self-test: %d fallo(s).\n", fails);
    return fails == 0 ? 0 : 1;
}

/*
 * @brief  Imprime la ayuda de uso en stderr.
 * @param  argv0  nombre del ejecutable, tal como se invocó.
 * @return No retorna valor.
 */
static void usage(const char *argv0)
{
    fprintf(stderr,
            "Uso: %s [opciones] [archivo]\n"
            "  -a, --algo NOMBRE   fcfs|sjf|srtn|priority|mlq|rr|all   (default: all)\n"
            "  -q, --quantum N     RR: repetible (default 1 2 4 8); MLQ usa el primero (default 4)\n"
            "  -f, --full          tabla por proceso\n"
            "  -g, --gantt         diagrama de Gantt compacto\n"
            "  -t, --self-test     casos clásicos de validación\n"
            "  -h, --help          esta ayuda\n"
            "Archivo '-' = stdin. Sin archivo: Procesos.txt si existe, si no stdin.\n",
            argv0);
}

/*
 * Fila del resumen comparativo: nombre corto del algoritmo y sus métricas.
 * El nombre apunta a un literal o a un buffer static que sobreviva a la
 * corrida (rows[] solo guarda el puntero).
 */
typedef struct {
    const char *name;
    Metrics m;
} Row;

/*
 * @brief  Imprime la tabla comparativa final, una fila por algoritmo corrido.
 * @param  rows  filas a imprimir.
 * @param  n     número de filas.
 * @return No retorna valor.
 */
static void print_comparison(const Row *rows, int n)
{
    print_header("Comparación de algoritmos");
    printf("  %-16s %10s %10s %10s %8s %8s %8s\n",
           "Algoritmo", "Espera", "Retorno", "Respuesta", "C.Sw", "Makespan", "CPU%");
    printf("  ---------------- ---------- ---------- ---------- -------- -------- --------\n");
    for (int i = 0; i < n; i++) {
        printf("  %-16s %10.4f %10.4f %10.4f %8d %8d %7.2f\n",
               rows[i].name, rows[i].m.avg_wait, rows[i].m.avg_tat,
               rows[i].m.avg_resp, rows[i].m.context_switches,
               rows[i].m.makespan, rows[i].m.cpu_util);
    }
}

/*
 * @brief  Punto de entrada: interpreta las opciones, carga los procesos y
 *         corre los algoritmos pedidos, validando los invariantes de cada
 *         corrida antes de imprimir sus resultados.
 * @param  argc  número de argumentos.
 * @param  argv  argumentos; el primer argumento no opcional es el archivo de
 *               procesos ("-" o NULL = stdin).
 * @return 0 si todo salió bien; 1 si hubo error de uso, de lectura o de
 *         invariantes.  Con -t devuelve el resultado de los self-tests
 *         (0 = todos los casos pasan).
 */
int main(int argc, char **argv)
{
    const char *algo = "all";
    const char *path = NULL;
    int quantums[8];   /* hasta 8 quantums de RR; el excedente se ignora */
    int nq = 0;
    int full = 0, show_gantt = 0, q_given = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--self-test") == 0)
            return run_self_tests();
        if ((strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--algo") == 0) && i + 1 < argc) {
            algo = argv[++i];
            continue;
        }
        if ((strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quantum") == 0) && i + 1 < argc) {
            q_given = 1;
            if (nq < 8)
                quantums[nq++] = atoi(argv[++i]);
            else
                i++;   /* más de 8 quantums: se consume el valor y se descarta */
            continue;
        }
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--full") == 0) {
            full = 1;
            continue;
        }
        if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--gantt") == 0) {
            show_gantt = 1;
            continue;
        }
        /* Un "-" solo no es una opción: es el nombre del archivo (stdin) */
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "Opción desconocida: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
        path = argv[i];
    }

    /* Sin -q, RR se corre con los cuatro quantums de referencia */
    if (nq == 0) {
        quantums[0] = 1;
        quantums[1] = 2;
        quantums[2] = 4;
        quantums[3] = 8;
        nq = 4;
    }
    for (int i = 0; i < nq; i++) {
        if (quantums[i] <= 0) {
            fprintf(stderr, "Quantum inválido: %d\n", quantums[i]);
            return 1;
        }
    }

    Process orig[MAX_PROCESOS], work[MAX_PROCESOS];
    int n;

    /* Sin archivo explícito se usa Procesos.txt si está en el directorio */
    if (!path) {
        FILE *probe = fopen("Procesos.txt", "r");
        if (probe) {
            fclose(probe);
            path = "Procesos.txt";
        }
    }

    n = load_processes(path, orig, MAX_PROCESOS);
    if (n < 0)
        return 1;
    if (n == 0) {
        fprintf(stderr, "No se leyeron procesos.\n");
        return 1;
    }

    printf("Se leyeron %d procesos", n);
    if (path)
        printf(" de %s", path);
    printf(".\n");

    /* Capacidad: 4 algoritmos base + MLQ + hasta 8 quantums de RR */
    Row rows[16];
    int nr = 0;
    Gantt gantt;

    /* Tabla de algoritmos base: clave de -a, título y función a invocar */
    struct {
        const char *key;
        const char *title;
        SimFn fn;
    } algs[] = {
        { "fcfs",     "FCFS (First-Come, First-Served)", simulate_fcfs },
        { "sjf",      "SJF (Shortest Job First, no expulsivo)", simulate_sjf },
        { "srtn",     "SRTN (Shortest Remaining Time Next)", simulate_srtn },
        { "priority", "Priority Scheduling (no expulsivo)", simulate_priority },
    };

    int want_all = strcmp(algo, "all") == 0;
    int want_rr  = want_all || strcmp(algo, "rr") == 0;

    for (size_t i = 0; i < sizeof(algs) / sizeof(algs[0]); i++) {
        if (!want_all && strcmp(algo, algs[i].key) != 0)
            continue;
        /* Cada corrida parte de los datos originales, no de la anterior */
        process_copy(work, orig, n);
        algs[i].fn(work, n, show_gantt ? &gantt : NULL, &rows[nr].m);
        if (!processes_ok(work, n)) {
            fprintf(stderr, "Invariantes rotas en %s\n", algs[i].title);
            return 1;
        }
        rows[nr].name = algs[i].key;
        print_header(algs[i].title);
        if (full)
            print_table(work, n);
        print_metrics(&rows[nr].m);
        if (show_gantt)
            gantt_print(&gantt, 36);
        nr++;
    }

    if (want_all || strcmp(algo, "mlq") == 0) {
        /* static: rows[] guarda el puntero al nombre, debe sobrevivir al bloque */
        static char mlq_name[16];
        char title[64];
        /* MLQ usa el primer -q si se dio alguno; si no, el default 4 */
        int q = q_given ? quantums[0] : 4;

        snprintf(title, sizeof(title), "Múltiples colas por prioridad (quantum = %d)", q);
        snprintf(mlq_name, sizeof(mlq_name), "MLQ q=%d", q);
        process_copy(work, orig, n);
        simulate_multilevel(work, n, q, show_gantt ? &gantt : NULL, &rows[nr].m);
        if (!processes_ok(work, n)) {
            fprintf(stderr, "Invariantes rotas en MLQ q=%d\n", q);
            return 1;
        }
        rows[nr].name = mlq_name;
        print_header(title);
        if (full)
            print_table(work, n);
        print_metrics(&rows[nr].m);
        if (show_gantt)
            gantt_print(&gantt, 36);
        nr++;
    }

    if (want_rr) {
        /* static: los nombres deben sobrevivir al bloque (rows[] los apunta) */
        static char rr_names[8][16];
        for (int q = 0; q < nq; q++) {
            char title[64];
            Metrics *m;

            snprintf(title, sizeof(title), "Round-Robin  (quantum = %d)", quantums[q]);
            process_copy(work, orig, n);
            m = &rows[nr].m;
            simulate_rr(work, n, quantums[q], show_gantt ? &gantt : NULL, m);
            if (!processes_ok(work, n)) {
                fprintf(stderr, "Invariantes rotas en RR q=%d\n", quantums[q]);
                return 1;
            }
            snprintf(rr_names[q], sizeof(rr_names[q]), "RR q=%d", quantums[q]);
            rows[nr].name = rr_names[q];
            print_header(title);
            if (full)
                print_table(work, n);
            print_metrics(m);
            if (show_gantt)
                gantt_print(&gantt, 36);
            nr++;
        }
    }

    /* Ningún algoritmo reconocido: se avisa y se recuerda el uso */
    if (nr == 0) {
        fprintf(stderr, "Algoritmo desconocido: %s\n", algo);
        usage(argv[0]);
        return 1;
    }

    if (nr > 1)
        print_comparison(rows, nr);

    return 0;
}
