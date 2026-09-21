/*
 * Archivo: schedulers.c
 * Tarea 3 — Simulación de algoritmos de planificación de CPU
 * Sistemas Operativos
 *
 * Descripción: Implementa los seis planificadores: FCFS, SJF, SRTN, Priority,
 * Round-Robin y múltiples colas por prioridad (MLQ).  Contiene los criterios
 * de selección, los motores de simulación y el registro opcional del Gantt y
 * de las métricas de cada corrida.
 */

#include "schedulers.h"

/* ---------- selectores (1 = a es mejor que b) ---------- */

/*
 * @brief  Criterio de FCFS: gana el de menor llegada y, en empate, el de menor
 *         id.
 * @param  a  primer proceso a comparar.
 * @param  b  segundo proceso a comparar.
 * @return 1 si a es mejor que b, 0 en caso contrario.
 */
static int better_fcfs(const Process *a, const Process *b)
{
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;
    return a->id < b->id;
}

/*
 * @brief  Criterio de SJF: menor ráfaga original; desempata por llegada y
 *         luego por id.
 * @param  a  primer proceso a comparar.
 * @param  b  segundo proceso a comparar.
 * @return 1 si a es mejor que b, 0 en caso contrario.
 */
static int better_sjf(const Process *a, const Process *b)
{
    if (a->burst != b->burst)
        return a->burst < b->burst;
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;
    return a->id < b->id;
}

/*
 * @brief  Criterio de Priority: menor número de prioridad (menor = más
 *         prioritario); desempata por llegada y luego por id.
 * @param  a  primer proceso a comparar.
 * @param  b  segundo proceso a comparar.
 * @return 1 si a es mejor que b, 0 en caso contrario.
 */
static int better_priority(const Process *a, const Process *b)
{
    if (a->priority != b->priority)
        return a->priority < b->priority;
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;
    return a->id < b->id;
}

/*
 * @brief  Criterio de SRTN: menor tiempo restante; desempata por llegada y
 *         luego por id.
 * @param  a  primer proceso a comparar.
 * @param  b  segundo proceso a comparar.
 * @return 1 si a es mejor que b, 0 en caso contrario.
 * @note   Con remaining igual gana el de menor llegada/id; como el proceso en
 *         CPU llegó antes o al mismo tiempo, un recién llegado con el mismo
 *         remaining no lo expulsa (no se expulsa "de gusto").
 */
static int better_srtn(const Process *a, const Process *b)
{
    if (a->remaining != b->remaining)
        return a->remaining < b->remaining;
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;
    return a->id < b->id;
}

/*
 * @brief  Busca el mejor proceso listo en el instante t según el comparador
 *         recibido.
 * @param  p       arreglo de procesos (solo lectura).
 * @param  n       número de procesos.
 * @param  t       instante actual.
 * @param  better  comparador "1 = a es mejor que b".
 * @return Índice en p del mejor proceso listo (remaining > 0 y arrival <= t),
 *         o -1 si no hay ninguno.
 * @note   Barrido lineal: con n <= MAX_PROCESOS no se justifica un heap.
 */
static int pick_ready(const Process *p, int n, int t,
                      int (*better)(const Process *, const Process *))
{
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (p[i].remaining <= 0 || p[i].arrival > t)
            continue;
        if (best < 0 || better(&p[i], &p[best]))
            best = i;
    }
    return best;
}

/*
 * @brief  Calcula la próxima llegada estrictamente posterior a t entre los
 *         procesos que aún tienen trabajo pendiente.
 * @param  p  arreglo de procesos (solo lectura).
 * @param  n  número de procesos.
 * @param  t  instante actual.
 * @return El menor arrival > t de los procesos con remaining > 0, o -1 si no
 *         queda ninguna llegada por delante.
 * @note   Es lo que permite saltar un hueco ocioso en un solo paso.
 */
static int next_arrival_after(const Process *p, int n, int t)
{
    int next = -1;
    for (int i = 0; i < n; i++) {
        if (p[i].remaining > 0 && p[i].arrival > t) {
            if (next < 0 || p[i].arrival < next)
                next = p[i].arrival;
        }
    }
    return next;
}

/*
 * @brief  Marca la primera entrada de un proceso a la CPU: fija start_time y
 *         response_time.
 * @param  p  proceso a marcar (se modifica).
 * @param  t  instante en que usa la CPU.
 * @return No retorna valor.
 * @note   Solo actúa la primera vez (start_time < 0): en RR y MLQ el proceso
 *         puede recibir la CPU varias veces, pero la respuesta se mide desde
 *         la primera.
 */
static void mark_start(Process *p, int t)
{
    if (p->start_time < 0) {
        p->start_time = t;
        p->response_time = t - p->arrival;
    }
}

/*
 * @brief  Cierra la cuenta de un proceso que terminó: finish_time, turnaround
 *         y waiting_time según las fórmulas de README.md §3.
 * @param  p  proceso terminado (se modifica).
 * @param  t  instante de fin.
 * @return No retorna valor.
 */
static void mark_finish(Process *p, int t)
{
    p->finish_time = t;
    p->turnaround = t - p->arrival;
    p->waiting_time = p->turnaround - p->burst;
}

/*
 * @brief  Motor común de los algoritmos no expulsivos (FCFS, SJF y Priority):
 *         en cada hueco de CPU elige el mejor proceso listo y lo ejecuta de
 *         corrido hasta que termina.
 * @param  p       arreglo de procesos; se reinicia y se reescriben sus tiempos.
 * @param  n       número de procesos.
 * @param  g       Gantt de salida, o NULL si no se quiere registrar.
 * @param  m       salida: métricas globales de la corrida.
 * @param  better  comparador que define la política de despacho.
 * @return No retorna valor.
 * @note   Si no hay procesos listos salta a la próxima llegada (hueco ocioso);
 *         si ya no quedan llegadas pendientes, corta el lazo.
 */
static void run_nonpreemptive(Process *p, int n, Gantt *g, Metrics *m,
                              int (*better)(const Process *, const Process *))
{
    int t = 0, completed = 0, idle = 0, switches = 0, last = -1;

    process_reset(p, n);
    if (g)
        gantt_init(g);

    while (completed < n) {
        int i = pick_ready(p, n, t, better);
        if (i < 0) {
            /* Nada listo: se registra el hueco ocioso y se salta a la próxima
             * llegada, sin consumir tiempo tick a tick */
            int nxt = next_arrival_after(p, n, t);
            if (nxt < 0)
                break;
            if (g)
                gantt_add(g, -1, t, nxt);
            idle += nxt - t;
            t = nxt;
            continue;
        }

        mark_start(&p[i], t);
        /* El primer despacho (last == -1) no cuenta como cambio de contexto */
        if (last >= 0 && last != i)
            switches++;
        last = i;

        /* No expulsivo: el elegido consume toda su ráfaga de una sola vez */
        int start = t;
        t += p[i].remaining;
        p[i].remaining = 0;
        mark_finish(&p[i], t);
        if (g)
            gantt_add(g, p[i].id, start, t);
        completed++;
    }

    compute_metrics(p, n, idle, switches, m);
}

/*
 * @brief  Simula FCFS (no expulsivo): delega en el motor no expulsivo con el
 *         criterio de menor llegada.
 * @param  p  arreglo de procesos a simular; se reescriben sus tiempos.
 * @param  n  número de procesos.
 * @param  g  Gantt de salida, o NULL si no se quiere registrar.
 * @param  m  salida: métricas globales de la corrida.
 * @return No retorna valor.
 */
void simulate_fcfs(Process *p, int n, Gantt *g, Metrics *m)
{
    run_nonpreemptive(p, n, g, m, better_fcfs);
}

/*
 * @brief  Simula SJF (no expulsivo): delega en el motor no expulsivo con el
 *         criterio de menor ráfaga original.
 * @param  p  arreglo de procesos a simular; se reescriben sus tiempos.
 * @param  n  número de procesos.
 * @param  g  Gantt de salida, o NULL si no se quiere registrar.
 * @param  m  salida: métricas globales de la corrida.
 * @return No retorna valor.
 */
void simulate_sjf(Process *p, int n, Gantt *g, Metrics *m)
{
    run_nonpreemptive(p, n, g, m, better_sjf);
}

/*
 * @brief  Simula Priority (no expulsivo): delega en el motor no expulsivo con
 *         el criterio de menor número de prioridad.
 * @param  p  arreglo de procesos a simular; se reescriben sus tiempos.
 * @param  n  número de procesos.
 * @param  g  Gantt de salida, o NULL si no se quiere registrar.
 * @param  m  salida: métricas globales de la corrida.
 * @return No retorna valor.
 */
void simulate_priority(Process *p, int n, Gantt *g, Metrics *m)
{
    run_nonpreemptive(p, n, g, m, better_priority);
}

/*
 * @brief  Simula SRTN (expulsivo): elige el proceso listo con menor tiempo
 *         restante y lo ejecuta como máximo hasta la próxima llegada, donde
 *         vuelve a evaluar si el recién llegado lo expulsa.
 * @param  p  arreglo de procesos a simular; se reescriben sus tiempos.
 * @param  n  número de procesos.
 * @param  g  Gantt de salida, o NULL si no se quiere registrar.
 * @param  m  salida: métricas globales de la corrida.
 * @return No retorna valor.
 * @note   Como solo se re-evalúa en las llegadas y en los fines, el lazo
 *         avanza por eventos (no tick a tick).
 */
void simulate_srtn(Process *p, int n, Gantt *g, Metrics *m)
{
    int t = 0, completed = 0, idle = 0, switches = 0, last = -1;

    process_reset(p, n);
    if (g)
        gantt_init(g);

    while (completed < n) {
        int i = pick_ready(p, n, t, better_srtn);
        if (i < 0) {
            /* Nada listo: hueco ocioso hasta la próxima llegada */
            int nxt = next_arrival_after(p, n, t);
            if (nxt < 0)
                break;
            if (g)
                gantt_add(g, -1, t, nxt);
            idle += nxt - t;
            t = nxt;
            continue;
        }

        mark_start(&p[i], t);
        /* El primer despacho (last == -1) no cuenta como cambio de contexto */
        if (last >= 0 && last != i)
            switches++;
        last = i;

        /* Se corre como máximo hasta la próxima llegada: ahí se decide si
         * conviene expulsar al actual.  El recorte del tramo es lo que hace
         * expulsivo al algoritmo. */
        int nxt = next_arrival_after(p, n, t);
        int run = p[i].remaining;
        if (nxt >= 0 && nxt - t < run)
            run = nxt - t;
        if (run < 1)
            run = 1;   /* salvaguarda: el reloj siempre avanza al menos un tick */

        if (g)
            gantt_add(g, p[i].id, t, t + run);
        p[i].remaining -= run;
        t += run;

        if (p[i].remaining == 0) {
            /* Terminó: se cierra su cuenta y el lazo elige de nuevo */
            mark_finish(&p[i], t);
            completed++;
        }
    }

    compute_metrics(p, n, idle, switches, m);
}

/*
 * @brief  Ordena los índices de los procesos por (arrival, id).
 *
 * Orden de admisión a la cola de listos: por (arrival, id), no por la
 * posición en el archivo.  Así las llegadas simultáneas entran de forma
 * determinista y con el mismo desempate que usan los demás algoritmos.
 *
 * @param  p      arreglo de procesos (solo lectura).
 * @param  n      número de procesos.
 * @param  order  salida: permutación de índices 0..n-1 en ese orden.
 * @return No retorna valor.
 * @note   Ordenamiento por inserción: n <= MAX_PROCESOS y el comparador ya
 *         define un orden total, así que el algoritmo es estable y suficiente.
 */
static void sort_by_arrival(const Process *p, int n, int *order)
{
    for (int i = 0; i < n; i++)
        order[i] = i;
    for (int i = 1; i < n; i++) {
        int k = order[i], j = i - 1;
        /* Desplaza mientras el anterior no sea mejor que k */
        while (j >= 0 && !better_fcfs(&p[order[j]], &p[k])) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = k;
    }
}

/*
 * @brief  Simula Round-Robin: cola FIFO circular con quantum fijo; el lazo
 *         avanza un tick por iteración.
 * @param  p        arreglo de procesos a simular; se reescriben sus tiempos.
 * @param  n        número de procesos.
 * @param  quantum  ticks de CPU por turno (> 0).
 * @param  g        Gantt de salida, o NULL si no se quiere registrar.
 * @param  m        salida: métricas globales de la corrida.
 * @return No retorna valor.
 * @note   El quantum mide servicio continuo: slice cuenta solo los ticks
 *         seguidos del proceso en CPU y se reinicia en cada despacho.
 */
void simulate_rr(Process *p, int n, int quantum, Gantt *g, Metrics *m)
{
    Queue ready;
    int order[MAX_PROCESOS];
    int next_adm = 0;
    int t = 0, completed = 0, idle = 0, switches = 0, last = -1;
    /* current: proceso en CPU (-1 = ninguna); slice: ticks seguidos que lleva;
     * pending: proceso que agotó su quantum y espera a ser reencolado */
    int current = -1, slice = 0, pending = -1;

    process_reset(p, n);
    queue_init(&ready);
    sort_by_arrival(p, n, order);
    if (g)
        gantt_init(g);

    while (completed < n) {
        /* 1) llegadas del instante t (en orden arrival, id) */
        while (next_adm < n && p[order[next_adm]].arrival <= t) {
            queue_push(&ready, order[next_adm]);
            next_adm++;
        }
        /* 2) después, el proceso cuyo quantum acaba de expirar */
        if (pending >= 0) {
            queue_push(&ready, pending);
            pending = -1;
        }

        if (current < 0) {
            if (!queue_empty(&ready)) {
                /* Despacho: el frente de la cola pasa a la CPU con quantum
                 * nuevo (slice = 0) */
                current = queue_pop(&ready);
                slice = 0;
                mark_start(&p[current], t);
                /* El primer despacho (last == -1) no cuenta como cambio de contexto */
                if (last >= 0 && last != current)
                    switches++;
                last = current;
            } else {
                /* Cola vacía: hueco ocioso hasta la próxima llegada */
                int nxt = next_arrival_after(p, n, t);
                if (nxt < 0)
                    break;
                if (g)
                    gantt_add(g, -1, t, nxt);
                idle += nxt - t;
                t = nxt;
                continue;
            }
        }

        /* Un tick de ejecución del proceso en CPU */
        int start = t;
        p[current].remaining--;
        slice++;
        t++;
        if (g)
            gantt_add(g, p[current].id, start, t);

        if (p[current].remaining == 0) {
            /* Terminó: libera la CPU */
            mark_finish(&p[current], t);
            completed++;
            current = -1;
            slice = 0;
        } else if (slice >= quantum) {
            /* Agotó el quantum: se aparta en pending para reencolarlo en la
             * próxima iteración, después de admitir las llegadas de t */
            pending = current;
            current = -1;
            slice = 0;
        }
    }

    compute_metrics(p, n, idle, switches, m);
}

/*
 * @brief  Simula múltiples colas por prioridad (multilevel queues,
 *         Silberschatz §5.3.4).
 *
 * Una cola por cada prioridad distinta, ordenadas de mayor a menor
 * prioridad (menor número = más prioridad).  En cada instante se atiende
 * la cola no vacía de mayor prioridad y, dentro de ella, Round-Robin.
 * Prioridad absoluta entre colas: si llega un proceso a una cola mejor
 * que la del proceso en CPU, lo expulsa en ese mismo instante.  El
 * expulsado vuelve al FRENTE de su propia cola (no consumió su quantum;
 * al reanudar recibe quantum nuevo, porque el quantum mide servicio
 * continuo).  No hay realimentación: un proceso nunca cambia de cola
 * (eso sería MLFQ).  Las llegadas del instante t se admiten antes de
 * reencolar, igual que en RR.
 *
 * @param  p        arreglo de procesos a simular; se reescriben sus tiempos.
 * @param  n        número de procesos.
 * @param  quantum  ticks máximos de servicio continuo dentro de una cola (> 0).
 * @param  g        Gantt de salida, o NULL si no se quiere registrar.
 * @param  m        salida: métricas globales de la corrida.
 * @return No retorna valor.
 */
void simulate_multilevel(Process *p, int n, int quantum, Gantt *g, Metrics *m)
{
    Queue queues[MAX_PROCESOS]; /* una cola por nivel: queues[l] <-> levels[l] */
    int levels[MAX_PROCESOS];   /* prioridades distintas, ascendente */
    int lvl_of[MAX_PROCESOS];   /* índice de proceso -> nivel en levels[] */
    int order[MAX_PROCESOS];
    int nl = 0;
    int next_adm = 0;
    int t = 0, completed = 0, idle = 0, switches = 0, last = -1;
    /* cur_q: nivel de la cola a la que pertenece el proceso en CPU */
    int current = -1, cur_q = -1, slice = 0, pending = -1;

    process_reset(p, n);
    sort_by_arrival(p, n, order);
    if (g)
        gantt_init(g);

    /* niveles: prioridades distintas en orden ascendente */
    for (int i = 0; i < n; i++) {
        /* Deduplicación por búsqueda lineal: nl <= n, no compensa otra cosa */
        int lv = 0;
        while (lv < nl && levels[lv] != p[i].priority)
            lv++;
        if (lv == nl)
            levels[nl++] = p[i].priority;
    }
    /* Ordenamiento por inserción de los niveles: el nivel 0 es el mejor */
    for (int i = 1; i < nl; i++) {
        int k = levels[i], j = i - 1;
        while (j >= 0 && levels[j] > k) {
            levels[j + 1] = levels[j];
            j--;
        }
        levels[j + 1] = k;
    }
    for (int l = 0; l < nl; l++)
        queue_init(&queues[l]);
    for (int i = 0; i < n; i++) {
        /* Búsqueda segura: levels[] contiene todas las prioridades distintas */
        int lv = 0;
        while (levels[lv] != p[i].priority)
            lv++;
        lvl_of[i] = lv;
    }

    while (completed < n) {
        /* 1) llegadas del instante t, cada una a la cola de su prioridad */
        while (next_adm < n && p[order[next_adm]].arrival <= t) {
            int i = order[next_adm++];
            queue_push(&queues[lvl_of[i]], i);
        }
        /* después, el proceso cuyo quantum acaba de expirar (como en RR) */
        if (pending >= 0) {
            /* Consumió su quantum completo: va al final de su propia cola */
            queue_push(&queues[lvl_of[pending]], pending);
            pending = -1;
        }

        /* 2) cola no vacía de mayor prioridad */
        int qstar = -1;
        for (int l = 0; l < nl; l++) {
            if (!queue_empty(&queues[l])) {
                qstar = l;
                break;
            }
        }

        /* 3) prioridad absoluta: si hay una cola mejor, se expulsa */
        if (current >= 0 && qstar >= 0 && qstar < cur_q) {
            /* El expulsado no consumió su quantum: vuelve al frente de SU
             * cola y al reanudar recibirá quantum nuevo */
            queue_push_front(&queues[cur_q], current);
            current = -1;
            slice = 0;
        }

        /* 4) despachar si la CPU quedó libre */
        if (current < 0) {
            if (qstar < 0) {
                /* Ninguna cola tiene listos: solo queda esperar la próxima
                 * llegada (los procesos pendientes aún no han llegado) */
                int nxt = next_arrival_after(p, n, t);
                if (nxt < 0)
                    break;
                if (g)
                    gantt_add(g, -1, t, nxt);
                idle += nxt - t;
                t = nxt;
                continue;
            }
            /* Se toma el frente de la mejor cola no vacía */
            current = queue_pop(&queues[qstar]);
            cur_q = qstar;
            slice = 0;
            mark_start(&p[current], t);
            /* El primer despacho (last == -1) no cuenta como cambio de contexto */
            if (last >= 0 && last != current)
                switches++;
            last = current;
        }

        /* 5) un tick de ejecución */
        int start = t;
        p[current].remaining--;
        slice++;
        t++;
        if (g)
            gantt_add(g, p[current].id, start, t);

        if (p[current].remaining == 0) {
            /* Terminó: libera la CPU */
            mark_finish(&p[current], t);
            completed++;
            current = -1;
            slice = 0;
        } else if (slice >= quantum) {
            /* Agotó el quantum: se aparta en pending para reencolarlo en la
             * próxima iteración, después de admitir las llegadas de t */
            pending = current;
            current = -1;
            slice = 0;
        }
    }

    compute_metrics(p, n, idle, switches, m);
}
