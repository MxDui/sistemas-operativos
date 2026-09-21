/*
 * Archivo: process.c
 * Tarea 3 — Simulación de algoritmos de planificación de CPU
 * Sistemas Operativos
 *
 * Descripción: Implementa las utilidades compartidas por los simuladores:
 * carga de procesos desde archivo o stdin, reinicio y copia de PCBs, cálculo
 * de las métricas globales, cola circular de índices y registro e impresión
 * del diagrama de Gantt.
 */

#include "process.h"

#include <stdio.h>
#include <string.h>

/*
 * @brief  Prepara n procesos para una corrida: remaining = burst y resultados
 *         en blanco; los datos de entrada (id, arrival, burst, priority) se
 *         conservan.
 * @param  p  arreglo de PCBs ya cargados.
 * @param  n  número de procesos a reiniciar.
 * @return No retorna valor.
 */
void process_reset(Process *p, int n)
{
    /* Solo se borra el estado derivado de la corrida anterior: los campos de
     * entrada no se tocan.  start_time y response_time en -1 significan
     * "todavía no ha usado la CPU". */
    for (int i = 0; i < n; i++) {
        p[i].remaining     = p[i].burst;
        p[i].start_time    = -1;
        p[i].finish_time   = 0;
        p[i].waiting_time  = 0;
        p[i].turnaround    = 0;
        p[i].response_time = -1;
    }
}

/*
 * @brief  Copia n PCBs de src a dst, para que cada algoritmo corra sobre su
 *         propia copia de los datos originales.
 * @param  dst  destino, con capacidad para n procesos.
 * @param  src  origen (no se modifica).
 * @param  n    número de procesos a copiar.
 * @return No retorna valor.
 */
void process_copy(Process *dst, const Process *src, int n)
{
    memcpy(dst, src, (size_t)n * sizeof(Process));
}

/*
 * @brief  Carga procesos desde un archivo de texto, cuatro enteros por línea
 *         (id arrival burst priority).
 * @param  path  ruta del archivo; NULL o "-" significan leer de stdin.
 * @param  p     arreglo destino.
 * @param  max   capacidad máxima a leer (no se supera).
 * @return Número de procesos leídos (>= 0), o -1 si no se pudo abrir el
 *         archivo o si algún proceso trae arrival < 0 o burst <= 0.
 * @note   El bucle se detiene al llegar a max o cuando una línea no aporta
 *         exactamente cuatro enteros (fin de datos o línea mal formada).
 */
int load_processes(const char *path, Process *p, int max)
{
    FILE *fp = stdin;
    int n = 0;

    /* NULL y "-" son sinónimos de stdin: el simulador también lee de tuberías */
    if (path && strcmp(path, "-") != 0) {
        fp = fopen(path, "r");
        if (!fp) {
            fprintf(stderr, "No se pudo abrir '%s'\n", path);
            return -1;
        }
    }

    while (n < max &&
           fscanf(fp, "%d %d %d %d",
                  &p[n].id, &p[n].arrival, &p[n].burst, &p[n].priority) == 4) {
        /* Validación mínima para que las fórmulas de las métricas tengan
         * sentido: una llegada negativa o una ráfaga no positiva no se admiten */
        if (p[n].burst <= 0 || p[n].arrival < 0) {
            fprintf(stderr, "Proceso %d: arrival debe ser >= 0 y burst > 0\n", p[n].id);
            if (fp != stdin)
                fclose(fp);
            return -1;
        }
        n++;
    }

    /* stdin no se cierra: pertenece al proceso, no a esta función */
    if (fp != stdin)
        fclose(fp);
    return n;
}

/*
 * @brief  Comprueba los invariantes de una corrida terminada: start/finish
 *         válidos, response = start - arrival, turnaround = finish - arrival,
 *         waiting = turnaround - burst, finish >= arrival + burst y
 *         remaining = 0.
 * @param  p  arreglo de procesos ya simulados.
 * @param  n  número de procesos.
 * @return 1 si todos los procesos cumplen los invariantes, 0 si alguno falla.
 */
int processes_ok(const Process *p, int n)
{
    for (int i = 0; i < n; i++) {
        if (p[i].start_time < 0 || p[i].finish_time < 0)
            return 0;
        if (p[i].response_time != p[i].start_time - p[i].arrival)
            return 0;
        if (p[i].turnaround != p[i].finish_time - p[i].arrival)
            return 0;
        if (p[i].waiting_time != p[i].turnaround - p[i].burst)
            return 0;
        if (p[i].waiting_time < 0 || p[i].response_time < 0)
            return 0;
        if (p[i].finish_time < p[i].arrival + p[i].burst)
            return 0;
        if (p[i].remaining != 0)
            return 0;
    }
    return 1;
}

/*
 * @brief  Calcula los agregados de una corrida: promedios de espera, retorno y
 *         respuesta, utilización, throughput, makespan, tiempo ocioso y
 *         cambios de contexto.
 * @param  p         procesos ya simulados, con sus tiempos.
 * @param  n         número de procesos.
 * @param  idle_time ticks ociosos acumulados desde t = 0 (incluye el hueco
 *                   previo a la primera llegada, que aquí se descuenta).
 * @param  switches  cambios de contexto contados por el planificador.
 * @param  m         salida: se pone a cero y luego se rellena.
 * @return No retorna valor.
 * @note   Requiere n >= 1 (first_arr se inicializa con p[0].arrival).
 */
void compute_metrics(const Process *p, int n, int idle_time, int switches, Metrics *m)
{
    long sum_w = 0, sum_t = 0, sum_r = 0, burst = 0;
    int first_arr = p[0].arrival;
    int last_fin = 0;
    int completed = 0;

    /* Arrancar de cero deja métricas neutras si la corrida no aportó nada */
    memset(m, 0, sizeof(*m));

    for (int i = 0; i < n; i++) {
        /* Las entradas con burst < 0 no se contabilizan en ningún agregado */
        if (p[i].burst < 0)
            continue;
        sum_w += p[i].waiting_time;
        sum_t += p[i].turnaround;
        sum_r += p[i].response_time;
        burst += p[i].burst;
        if (p[i].arrival < first_arr)
            first_arr = p[i].arrival;
        if (p[i].finish_time > last_fin)
            last_fin = p[i].finish_time;
        completed++;
    }

    m->completed = completed;
    m->first_arrival = first_arr;
    m->last_finish = last_fin;
    m->total_burst = burst;
    m->makespan = last_fin - first_arr;
    /* los simuladores arrancan en t=0: el hueco [0, primera llegada) no es
     * tiempo ocioso "del sistema", se descuenta */
    m->idle_time = idle_time - first_arr;
    if (m->idle_time < 0)
        m->idle_time = 0;
    m->context_switches = switches;
    /* Sin procesos contabilizados no se divide: los promedios quedan en 0 */
    if (completed > 0) {
        m->avg_wait = (double)sum_w / completed;
        m->avg_tat  = (double)sum_t / completed;
        m->avg_resp = (double)sum_r / completed;
    }
    /* El makespan es el denominador de las tasas: sin él quedarían en 0 */
    if (m->makespan > 0) {
        m->cpu_util    = 100.0 * (double)burst / m->makespan;
        m->throughput  = (double)completed / m->makespan;
    }
}

/*
 * @brief  Inicializa la cola como vacía.
 * @param  q  cola a inicializar.
 * @return No retorna valor.
 */
void queue_init(Queue *q)
{
    q->head = q->tail = q->count = 0;
}

/*
 * @brief  Indica si la cola no tiene elementos.
 * @param  q  cola a consultar.
 * @return 1 si está vacía, 0 en caso contrario.
 */
int queue_empty(const Queue *q)
{
    return q->count == 0;
}

/*
 * @brief  Encola un índice por el final (llegada normal a la cola de listos).
 * @param  q  cola con espacio disponible (a lo sumo MAX_PROCESOS elementos).
 * @param  x  índice del proceso en el arreglo de procesos.
 * @return No retorna valor.
 */
void queue_push(Queue *q, int x)
{
    q->data[q->tail] = x;
    q->tail = (q->tail + 1) % MAX_PROCESOS;
    q->count++;
}

/*
 * @brief  Encola un índice por el frente de la cola (lo usa MLQ al expulsar
 *         entre colas).
 * @param  q  cola con espacio disponible.
 * @param  x  índice del proceso en el arreglo de procesos.
 * @return No retorna valor.
 * @note   el proceso expulsado conserva su turno: vuelve al frente de su cola
 */
void queue_push_front(Queue *q, int x)
{
    q->head = (q->head - 1 + MAX_PROCESOS) % MAX_PROCESOS;
    q->data[q->head] = x;
    q->count++;
}

/*
 * @brief  Desencola el elemento del frente.
 * @param  q  cola no vacía (el llamador lo verifica con queue_empty).
 * @return Índice del proceso que estaba al frente.
 */
int queue_pop(Queue *q)
{
    int x = q->data[q->head];
    q->head = (q->head + 1) % MAX_PROCESOS;
    q->count--;
    return x;
}

/*
 * @brief  Inicializa el diagrama de Gantt como vacío.
 * @param  g  diagrama a inicializar.
 * @return No retorna valor.
 */
void gantt_init(Gantt *g)
{
    g->n = 0;
}

/*
 * @brief  Registra el tramo [t0, t1) de un proceso, fusionándolo con el tramo
 *         anterior si es el mismo proceso y son contiguos.
 * @param  g    diagrama destino.
 * @param  pid  id del proceso, o -1 para CPU ociosa.
 * @param  t0   instante de inicio del tramo.
 * @param  t1   instante de fin, excluyente.
 * @return No retorna valor.
 * @note   Los tramos vacíos (t1 <= t0) y el exceso sobre MAX_SLICES se
 *         descartan en silencio.
 */
void gantt_add(Gantt *g, int pid, int t0, int t1)
{
    /* Tramo vacío o diagrama lleno: no hay nada que registrar */
    if (t1 <= t0 || g->n >= MAX_SLICES)
        return;
    /* Fusión con el tramo anterior: si no, RR generaría un segmento por tick */
    if (g->n > 0 && g->slices[g->n - 1].pid == pid && g->slices[g->n - 1].end == t0) {
        g->slices[g->n - 1].end = t1;
        return;
    }
    g->slices[g->n].pid = pid;
    g->slices[g->n].start = t0;
    g->slices[g->n].end = t1;
    g->n++;
}

/*
 * @brief  Imprime el diagrama en una línea compacta, en bloques de seis tramos.
 * @param  g      diagrama a imprimir.
 * @param  limit  máximo de tramos a mostrar; <= 0 significa sin límite.
 * @return No retorna valor.
 */
void gantt_print(const Gantt *g, int limit)
{
    /* Si hay más tramos que limit, se muestran los primeros y se avisa */
    int shown = g->n;
    if (limit > 0 && shown > limit)
        shown = limit;

    printf("  Gantt (%d segmentos", g->n);
    if (shown < g->n)
        printf(", mostrando los primeros %d", shown);
    printf("):\n    ");

    for (int i = 0; i < shown; i++) {
        if (g->slices[i].pid < 0)
            printf("| idle %d-%d ", g->slices[i].start, g->slices[i].end);
        else
            printf("| P%-3d %d-%d ", g->slices[i].pid,
                   g->slices[i].start, g->slices[i].end);
        /* Seis tramos por línea para que el diagrama quepa en la terminal */
        if ((i + 1) % 6 == 0 && i + 1 < shown)
            printf("\n    ");
    }
    printf("|\n");
    if (shown < g->n)
        printf("    ... (%d segmentos más)\n", g->n - shown);
}
