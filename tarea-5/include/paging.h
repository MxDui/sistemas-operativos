/*
 * Archivo: paging.h
 * Tarea 5: Simulación de algoritmos de administración de memoria
 * Sistemas Operativos, Equipo E04
 *
 * Descripción: Declara las estructuras y funciones del motor de simulación de
 * reemplazo de páginas.  El simulador trabaja con una cadena de referencias y
 * un número fijo de marcos; cada referencia produce un acierto o un fallo, y
 * en caso de fallo se elige una víctima según el algoritmo.
 *
 * Los algoritmos seleccionados por el equipo son Second Chance y Aging.  FIFO,
 * LRU y OPT se incluyen como líneas base de comparación, no como parte de la
 * selección.
 */

#ifndef PAGING_H
#define PAGING_H

#include <stddef.h>

/* Capacidad máxima del simulador (arreglos estáticos). */
#define MAX_FRAMES   64
#define MAX_REFS     200000
#define MAX_PAGES    4096
#define MAX_AGE_BITS 16

/*
 * Algoritmos implementados.  El orden de este enum es el orden en que se
 * ejecutan y se imprimen cuando se pide "all": primero los seleccionados
 * (Second Chance y Aging) y después las líneas base.
 */
typedef enum {
    ALG_SECOND_CHANCE = 0,
    ALG_AGING,
    ALG_FIFO,
    ALG_LRU,
    ALG_OPT,
    ALG_COUNT
} Algo;

/*
 * Estado de un marco de memoria física.
 * page < 0 significa marco libre.  Los campos ref, age, loaded_at y last_used
 * son metadatos que cada algoritmo usa según corresponda; los algoritmos que
 * no los necesitan simplemente los ignoran.
 */
typedef struct {
    int page;       /* número de página residente; -1 = libre */
    unsigned ref;   /* bit de referencia (Second Chance) */
    unsigned age;   /* contador de envejecimiento (Aging) */
    int loaded_at;  /* paso en que se cargó la página */
    int last_used;  /* último paso en que se referenció (LRU) */
} Frame;

/*
 * Contadores acumulados de una corrida.  Las métricas específicas de cada
 * algoritmo quedan en cero cuando no aplican.
 */
typedef struct {
    int faults;             /* fallos de página */
    int hits;               /* aciertos */
    int evictions;          /* páginas expulsadas (fallos con memoria llena) */
    int free_loads;         /* fallos que usaron un marco libre */
    int ref_cleared;        /* Second Chance: bits de referencia puestos a 0 */
    int scans;              /* Second Chance: marcos examinados */
    int hand_steps;         /* Second Chance: avances del puntero */
    int age_ties;           /* Aging: selecciones con empate en el mínimo */
    long victim_age_sum;    /* Aging: suma de edades de las víctimas */
    int victim_age_count;   /* Aging: número de víctimas (para el promedio) */
    long residency_sum;     /* suma de (paso de expulsión - paso de carga) */
    int residency_count;    /* número de expulsiones con residencia medida */
} Stats;

/*
 * Evento producido por una referencia.  Se entrega al callback de traza para
 * que imprima lo que necesite sin que el motor conozca el formato de salida.
 */
typedef struct {
    int step;        /* número de referencia, empieza en 1 */
    int page;        /* página referenciada */
    int hit;         /* 1 = acierto, 0 = fallo */
    int victim;      /* página expulsada; -1 = no hubo expulsión */
    int hand;        /* posición del puntero después del evento (SC) */
    int scans;       /* marcos examinados en este fallo (SC) */
    int cleared;     /* bits limpiados en este fallo (SC) */
    int free_frame;  /* marco libre usado; -1 = no aplica */
    unsigned victim_age; /* contador de la víctima antes de expulsarla (Aging) */
} TraceEvent;

typedef struct Sim Sim;

/*
 * Estado completo de una simulación.  Es una estructura plana para que el
 * callback de traza pueda leer marcos, puntero y estadísticas sin accesores.
 * refs/nrefs/pos solo los usa OPT para mirar el futuro de la cadena.
 */
struct Sim {
    Algo algo;
    int frames;
    int age_bits;
    unsigned age_mask;  /* máscara de age_bits unos */
    unsigned age_msb;   /* bit más significativo del contador */
    Frame fr[MAX_FRAMES];
    int hand;           /* puntero circular (FIFO y Second Chance) */
    int used;           /* marcos ocupados */
    int step;           /* referencia actual (1..nrefs) */
    const int *refs;    /* cadena completa (OPT) */
    int nrefs;
    int pos;            /* índice de la referencia actual en refs */
    Stats st;
};

/* Callback de traza: recibe el estado ya actualizado y el evento. */
typedef void (*TraceFn)(const Sim *sim, const TraceEvent *ev, void *user);

/* Nombre legible y clave corta de cada algoritmo. */
const char *algo_name(Algo a);
const char *algo_key(Algo a);

/*
 * Interpreta una clave de algoritmo ("second", "aging", "fifo", "lru", "opt",
 * "clock", "sc", "age", "optimal").  Devuelve 1 si la reconoce y 0 si no.
 */
int algo_parse(const char *s, Algo *out);

/* Inicializa una simulación vacía.  frames y age_bits deben ser válidos. */
void sim_init(Sim *s, Algo algo, int frames, int age_bits);

/* Procesa una referencia y devuelve el evento correspondiente. */
TraceEvent sim_step(Sim *s, int page);

/* Corre la cadena completa; si fn != NULL, la invoca en cada referencia. */
void sim_run(Sim *s, const int *refs, int n, TraceFn fn, void *user);

/* Métricas derivadas (devuelven 0 si el denominador es 0). */
double stats_fault_rate(const Stats *st, int nrefs);
double stats_avg_victim_age(const Stats *st);
double stats_avg_residency(const Stats *st);

/*
 * Verifica los invariantes de una corrida terminada.  Devuelve 1 si todo está
 * bien; si no, escribe una descripción en err (tamaño errsz) y devuelve 0.
 */
int stats_ok(const Sim *s, int nrefs, char *err, size_t errsz);

#endif
