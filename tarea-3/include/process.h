/*
 * Archivo: process.h
 * Tarea 3 — Simulación de algoritmos de planificación de CPU
 * Sistemas Operativos
 *
 * Descripción: Declara las estructuras de datos compartidas por el simulador
 * (PCB de un proceso, cola circular de índices, tramos del diagrama de Gantt
 * y métricas globales de una corrida) y las utilidades de procesos, colas y
 * Gantt que implementa process.c.
 */

#ifndef PROCESS_H
#define PROCESS_H

/*
 * Capacidad máxima del arreglo de procesos.  Es también la capacidad de cada
 * cola: las colas guardan índices, y nunca pueden tener más elementos que
 * procesos existen.
 */
#define MAX_PROCESOS 200
/* Tope de tramos del Gantt (arreglo estático); gantt_add descarta el excedente. */
#define MAX_SLICES   8192

/*
 * PCB de un proceso.  Los cuatro primeros campos vienen del archivo de entrada
 * y no se modifican durante la simulación; el resto es estado derivado que
 * cada corrida reinicia con process_reset.
 * Convenio: un número de prioridad menor significa mayor prioridad.
 */
typedef struct {
    int id;            /* identificador; último criterio de desempate */
    int arrival;       /* instante de llegada (>= 0) */
    int burst;         /* ráfaga total solicitada (> 0) */
    int priority;      /* número de prioridad (menor = más prioritario) */
    int remaining;     /* tiempo restante; llega a 0 al terminar */
    int start_time;    /* primer instante en CPU; -1 = todavía no ha corrido */
    int finish_time;   /* instante de fin; 0 = aún sin terminar */
    int waiting_time;  /* espera = turnaround - burst */
    int turnaround;    /* retorno = finish_time - arrival */
    int response_time; /* respuesta = start_time - arrival; -1 = sin arrancar */
} Process;

/*
 * Cola circular FIFO de índices al arreglo de procesos (nunca copia PCBs).
 * head apunta al frente y tail a la próxima posición libre; ambos avanzan en
 * módulo MAX_PROCESOS.  La capacidad alcanza porque cada proceso está a lo
 * sumo una vez en la cola y hay n <= MAX_PROCESOS procesos.
 */
typedef struct {
    int data[MAX_PROCESOS]; /* índices a Process[] */
    int head;               /* posición del próximo elemento a extraer */
    int tail;               /* posición donde se insertará el próximo */
    int count;              /* elementos almacenados (0 = vacía) */
} Queue;

/*
 * Tramo continuo de CPU [start, end) atribuido a un proceso.
 * Invariantes: start < end y pid == -1 significa CPU ociosa.
 */
typedef struct {
    int pid;   /* -1 = CPU ociosa */
    int start;
    int end;
} Slice;

/*
 * Diagrama de Gantt compacto: tramos en orden temporal y sin solapes.
 * gantt_add fusiona tramos contiguos del mismo proceso, así que n suele ser
 * bastante menor que el número de ticks simulados.
 */
typedef struct {
    Slice slices[MAX_SLICES]; /* tramos registrados, en orden temporal */
    int n;                    /* tramos usados (0 = diagrama vacío) */
} Gantt;

/*
 * Métricas globales de una corrida.  Los promedios quedan en 0 si no hubo
 * procesos contabilizados y las tasas (utilización, throughput) en 0 si el
 * makespan es 0.  Las fórmulas están documentadas en README.md §3.
 */
typedef struct {
    double avg_wait;        /* espera media: suma(espera) / completed */
    double avg_tat;         /* retorno medio: suma(TAT) / completed */
    double avg_resp;        /* respuesta media: suma(respuesta) / completed */
    double cpu_util;        /* % de CPU ocupada: 100 * total_burst / makespan */
    double throughput;      /* procesos terminados por unidad de tiempo */
    int context_switches;   /* cambios de proceso en CPU (el 1.º no cuenta) */
    int makespan;           /* último fin - primera llegada */
    int idle_time;          /* ticks ociosos dentro del makespan */
    int completed;          /* procesos contabilizados (burst >= 0) */
    int first_arrival;      /* menor instante de llegada */
    int last_finish;        /* mayor instante de fin */
    long total_burst;       /* suma de las ráfagas */
} Metrics;

/*
 * @brief  Prepara n procesos para una corrida: remaining = burst y resultados
 *         en blanco; los datos de entrada (id, arrival, burst, priority) se
 *         conservan.
 * @param  p  arreglo de PCBs ya cargados.
 * @param  n  número de procesos a reiniciar.
 * @return No retorna valor.
 */
void process_reset(Process *p, int n);

/*
 * @brief  Copia n PCBs de src a dst, para que cada algoritmo corra sobre su
 *         propia copia de los datos originales.
 * @param  dst  destino, con capacidad para n procesos.
 * @param  src  origen (no se modifica).
 * @param  n    número de procesos a copiar.
 * @return No retorna valor.
 */
void process_copy(Process *dst, const Process *src, int n);

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
int  load_processes(const char *path, Process *p, int max);

/*
 * @brief  Comprueba los invariantes de una corrida terminada: start/finish
 *         válidos, response = start - arrival, turnaround = finish - arrival,
 *         waiting = turnaround - burst, finish >= arrival + burst y
 *         remaining = 0.
 * @param  p  arreglo de procesos ya simulados.
 * @param  n  número de procesos.
 * @return 1 si todos los procesos cumplen los invariantes, 0 si alguno falla.
 */
int  processes_ok(const Process *p, int n);

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
 */
void compute_metrics(const Process *p, int n, int idle_time, int switches, Metrics *m);

/*
 * @brief  Inicializa la cola como vacía.
 * @param  q  cola a inicializar.
 * @return No retorna valor.
 */
void queue_init(Queue *q);

/*
 * @brief  Indica si la cola no tiene elementos.
 * @param  q  cola a consultar.
 * @return 1 si está vacía, 0 en caso contrario.
 */
int  queue_empty(const Queue *q);

/*
 * @brief  Encola un índice por el final (llegada normal a la cola de listos).
 * @param  q  cola con espacio disponible (a lo sumo MAX_PROCESOS elementos).
 * @param  x  índice del proceso en el arreglo de procesos.
 * @return No retorna valor.
 */
void queue_push(Queue *q, int x);

/*
 * @brief  Encola un índice por el frente; se usa para devolver el turno al
 *         proceso expulsado entre colas de MLQ.
 * @param  q  cola con espacio disponible.
 * @param  x  índice del proceso en el arreglo de procesos.
 * @return No retorna valor.
 */
void queue_push_front(Queue *q, int x);

/*
 * @brief  Desencola el elemento del frente.
 * @param  q  cola no vacía (el llamador lo verifica con queue_empty).
 * @return Índice del proceso que estaba al frente.
 */
int  queue_pop(Queue *q);

/*
 * @brief  Inicializa el diagrama de Gantt como vacío.
 * @param  g  diagrama a inicializar.
 * @return No retorna valor.
 */
void gantt_init(Gantt *g);

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
void gantt_add(Gantt *g, int pid, int t0, int t1);

/*
 * @brief  Imprime el diagrama en una línea compacta, en bloques de seis tramos.
 * @param  g      diagrama a imprimir.
 * @param  limit  máximo de tramos a mostrar; <= 0 significa sin límite.
 * @return No retorna valor.
 */
void gantt_print(const Gantt *g, int limit);

#endif
