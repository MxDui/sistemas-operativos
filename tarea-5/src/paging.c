/*
 * Archivo: paging.c
 * Tarea 5: Simulación de algoritmos de administración de memoria
 * Sistemas Operativos, Equipo E04
 *
 * Descripción: Motor de simulación de reemplazo de páginas.  Implementa los
 * dos algoritmos seleccionados (Second Chance y Aging) y tres líneas base
 * (FIFO, LRU y OPT).  El motor no imprime nada: devuelve eventos y acumula
 * estadísticas; la interfaz de línea de comandos se encarga de la salida.
 */

#include "paging.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/*
 * @brief  Compara dos cadenas sin distinguir mayúsculas de minúsculas.
 * @param  a  primera cadena.
 * @param  b  segunda cadena.
 * @return 1 si son iguales, 0 si no.
 */
static int ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

const char *algo_name(Algo a)
{
    switch (a) {
    case ALG_SECOND_CHANCE: return "Second Chance";
    case ALG_AGING:         return "Aging";
    case ALG_FIFO:          return "FIFO";
    case ALG_LRU:           return "LRU";
    case ALG_OPT:           return "OPT";
    default:                return "?";
    }
}

const char *algo_key(Algo a)
{
    switch (a) {
    case ALG_SECOND_CHANCE: return "second";
    case ALG_AGING:         return "aging";
    case ALG_FIFO:          return "fifo";
    case ALG_LRU:           return "lru";
    case ALG_OPT:           return "opt";
    default:                return "?";
    }
}

int algo_parse(const char *s, Algo *out)
{
    if (ieq(s, "second") || ieq(s, "second-chance") || ieq(s, "sc") ||
        ieq(s, "clock") || ieq(s, "reloj")) {
        *out = ALG_SECOND_CHANCE;
        return 1;
    }
    if (ieq(s, "aging") || ieq(s, "age") || ieq(s, "envejecimiento")) {
        *out = ALG_AGING;
        return 1;
    }
    if (ieq(s, "fifo")) {
        *out = ALG_FIFO;
        return 1;
    }
    if (ieq(s, "lru")) {
        *out = ALG_LRU;
        return 1;
    }
    if (ieq(s, "opt") || ieq(s, "optimal") || ieq(s, "optimo") ||
        ieq(s, "óptimo")) {
        *out = ALG_OPT;
        return 1;
    }
    return 0;
}

void sim_init(Sim *s, Algo algo, int frames, int age_bits)
{
    memset(s, 0, sizeof *s);
    s->algo = algo;
    s->frames = frames;
    s->age_bits = age_bits;
    s->age_mask = (age_bits >= 32) ? ~0u : ((1u << age_bits) - 1u);
    s->age_msb = 1u << (age_bits - 1);
    s->hand = 0;
    s->used = 0;
    s->step = 0;
    s->pos = -1;
    for (int i = 0; i < frames; i++)
        s->fr[i].page = -1;
}

/*
 * @brief  Envejece todos los contadores residentes (un desplazamiento a la
 *         derecha por referencia).  Solo se usa en Aging.
 * @param  s  simulación activa.
 * @return No retorna valor.
 */
static void aging_tick(Sim *s)
{
    for (int i = 0; i < s->frames; i++) {
        if (s->fr[i].page >= 0)
            s->fr[i].age >>= 1;
    }
}

/*
 * @brief  Busca el índice del marco que contiene una página.
 * @param  s     simulación activa.
 * @param  page  página buscada.
 * @return Índice del marco, o -1 si la página no está residente.
 */
static int find_page(const Sim *s, int page)
{
    for (int i = 0; i < s->frames; i++) {
        if (s->fr[i].page == page)
            return i;
    }
    return -1;
}

/*
 * @brief  Elige el marco libre de menor índice.
 * @param  s  simulación activa.
 * @return Índice libre, o -1 si no hay marcos libres.
 */
static int find_free(const Sim *s)
{
    for (int i = 0; i < s->frames; i++) {
        if (s->fr[i].page < 0)
            return i;
    }
    return -1;
}

/*
 * @brief  Elige la víctima de LRU: el marco con last_used más antiguo.
 *         Los empates se resuelven por el menor índice de marco.
 * @param  s  simulación activa.
 * @return Índice del marco víctima.
 */
static int pick_lru(const Sim *s)
{
    int idx = 0;
    for (int i = 1; i < s->frames; i++) {
        if (s->fr[i].last_used < s->fr[idx].last_used)
            idx = i;
    }
    return idx;
}

/*
 * @brief  Elige la víctima de OPT: la página cuyo próximo uso está más lejos
 *         (o que no vuelve a usarse).  Los empates se resuelven por el menor
 *         índice de marco.
 * @param  s  simulación activa; s->refs y s->pos deben estar definidos.
 * @return Índice del marco víctima.
 */
static int pick_opt(const Sim *s)
{
    int best = 0;
    int best_next = -1;

    for (int i = 0; i < s->frames; i++) {
        int next = s->nrefs + 1; /* nunca se vuelve a usar */
        for (int j = s->pos + 1; j < s->nrefs; j++) {
            if (s->refs[j] == s->fr[i].page) {
                next = j;
                break;
            }
        }
        if (next > best_next) {
            best_next = next;
            best = i;
        }
    }
    return best;
}

/*
 * @brief  Elige la víctima de Aging: el marco con menor contador.  Cuenta
 *         cuántas selecciones tuvieron al menos un empate en el mínimo.
 * @param  s  simulación activa.
 * @return Índice del marco víctima.
 */
static int pick_aging(Sim *s)
{
    int idx = 0;
    for (int i = 1; i < s->frames; i++) {
        if (s->fr[i].age < s->fr[idx].age)
            idx = i;
    }

    int ties = 0;
    for (int i = 0; i < s->frames; i++) {
        if (i != idx && s->fr[i].age == s->fr[idx].age)
            ties++;
    }
    if (ties > 0)
        s->st.age_ties++;

    s->st.victim_age_sum += (long)s->fr[idx].age;
    s->st.victim_age_count++;
    return idx;
}

TraceEvent sim_step(Sim *s, int page)
{
    TraceEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.step = s->step + 1;
    ev.page = page;
    ev.victim = -1;
    ev.free_frame = -1;
    s->step++;

    /* Aging envejece a todos los residentes al inicio de cada referencia. */
    if (s->algo == ALG_AGING)
        aging_tick(s);

    int hit_idx = find_page(s, page);
    if (hit_idx >= 0) {
        s->st.hits++;
        ev.hit = 1;

        switch (s->algo) {
        case ALG_SECOND_CHANCE:
            s->fr[hit_idx].ref = 1;
            break;
        case ALG_AGING:
            s->fr[hit_idx].age |= s->age_msb;
            break;
        case ALG_LRU:
            s->fr[hit_idx].last_used = s->step;
            break;
        case ALG_FIFO:
        case ALG_OPT:
            break;
        default:
            break;
        }

        ev.hand = s->hand;
        return ev;
    }

    /* Fallo de página. */
    s->st.faults++;

    int idx = find_free(s);
    if (idx >= 0) {
        s->used++;
        s->st.free_loads++;
        ev.free_frame = idx;
    } else {
        s->st.evictions++;

        switch (s->algo) {
        case ALG_SECOND_CHANCE: {
            /*
             * Reloj: se examina el marco apuntado por hand.  Si su bit de
             * referencia está en 1, se le da una segunda oportunidad
             * (se limpia y se avanza).  El primero con bit 0 es la víctima.
             */
            int scans = 0;
            int cleared = 0;
            while (s->fr[s->hand].ref) {
                s->fr[s->hand].ref = 0;
                cleared++;
                scans++;
                s->st.ref_cleared++;
                s->st.hand_steps++;
                s->hand = (s->hand + 1) % s->frames;
            }
            scans++;
            idx = s->hand;
            ev.victim = s->fr[idx].page;
            s->st.scans += scans;
            s->st.hand_steps++;
            s->hand = (s->hand + 1) % s->frames;
            ev.scans = scans;
            ev.cleared = cleared;
            break;
        }
        case ALG_AGING:
            idx = pick_aging(s);
            ev.victim = s->fr[idx].page;
            ev.victim_age = s->fr[idx].age;
            break;
        case ALG_FIFO:
            idx = s->hand;
            ev.victim = s->fr[idx].page;
            s->hand = (s->hand + 1) % s->frames;
            break;
        case ALG_LRU:
            idx = pick_lru(s);
            ev.victim = s->fr[idx].page;
            break;
        case ALG_OPT:
            idx = pick_opt(s);
            ev.victim = s->fr[idx].page;
            break;
        default:
            idx = 0;
            ev.victim = s->fr[idx].page;
            break;
        }

        s->st.residency_sum += (long)(s->step - s->fr[idx].loaded_at);
        s->st.residency_count++;
    }

    /* Carga de la página referenciada en el marco elegido. */
    s->fr[idx].page = page;
    s->fr[idx].loaded_at = s->step;
    s->fr[idx].last_used = s->step;
    s->fr[idx].ref = (s->algo == ALG_SECOND_CHANCE) ? 1u : 0u;
    s->fr[idx].age = (s->algo == ALG_AGING) ? s->age_msb : 0u;

    /* En FIFO y Second Chance el puntero avanza también al llenar marcos. */
    if (s->algo == ALG_FIFO || s->algo == ALG_SECOND_CHANCE) {
        if (ev.free_frame >= 0) {
            s->hand = (idx + 1) % s->frames;
            s->st.hand_steps += (s->algo == ALG_SECOND_CHANCE) ? 1 : 0;
        }
    }

    ev.hand = s->hand;
    return ev;
}

void sim_run(Sim *s, const int *refs, int n, TraceFn fn, void *user)
{
    s->refs = refs;
    s->nrefs = n;
    for (int i = 0; i < n; i++) {
        s->pos = i;
        TraceEvent ev = sim_step(s, refs[i]);
        if (fn)
            fn(s, &ev, user);
    }
}

double stats_fault_rate(const Stats *st, int nrefs)
{
    return nrefs > 0 ? (double)st->faults / (double)nrefs : 0.0;
}

double stats_avg_victim_age(const Stats *st)
{
    return st->victim_age_count > 0
               ? (double)st->victim_age_sum / (double)st->victim_age_count
               : 0.0;
}

double stats_avg_residency(const Stats *st)
{
    return st->residency_count > 0
               ? (double)st->residency_sum / (double)st->residency_count
               : 0.0;
}

int stats_ok(const Sim *s, int nrefs, char *err, size_t errsz)
{
#define FAIL(...)                                                              \
    do {                                                                       \
        if (err && errsz)                                                      \
            snprintf(err, errsz, __VA_ARGS__);                                 \
        return 0;                                                              \
    } while (0)

    if (s->st.faults + s->st.hits != nrefs)
        FAIL("fallos + aciertos = %d, se esperaban %d referencias",
             s->st.faults + s->st.hits, nrefs);
    if (s->st.evictions != s->st.faults - s->st.free_loads)
        FAIL("expulsiones = %d, fallos - cargas libres = %d",
             s->st.evictions, s->st.faults - s->st.free_loads);
    if (s->st.free_loads < 0 || s->st.free_loads > s->frames)
        FAIL("cargas en marcos libres fuera de rango: %d", s->st.free_loads);
    if (s->st.evictions < 0)
        FAIL("expulsiones negativas: %d", s->st.evictions);

    int expected_used = s->st.faults < s->frames ? s->st.faults : s->frames;
    if (s->used != expected_used)
        FAIL("marcos ocupados = %d, se esperaban %d", s->used, expected_used);

    for (int i = 0; i < s->frames; i++) {
        if (s->fr[i].page < 0)
            continue;
        if (s->fr[i].loaded_at < 1 || s->fr[i].loaded_at > s->step)
            FAIL("marco %d con loaded_at inválido: %d", i, s->fr[i].loaded_at);
        if (s->fr[i].last_used < 1 || s->fr[i].last_used > s->step)
            FAIL("marco %d con last_used inválido: %d", i, s->fr[i].last_used);
        for (int j = i + 1; j < s->frames; j++) {
            if (s->fr[j].page == s->fr[i].page)
                FAIL("página %d duplicada en los marcos %d y %d",
                     s->fr[i].page, i, j);
        }
    }

    if (s->st.residency_count != s->st.evictions)
        FAIL("residencias medidas = %d, expulsiones = %d",
             s->st.residency_count, s->st.evictions);

    if (s->algo == ALG_AGING) {
        if (s->st.victim_age_count != s->st.evictions)
            FAIL("edades de víctima medidas = %d, expulsiones = %d",
                 s->st.victim_age_count, s->st.evictions);
    } else if (s->st.victim_age_count != 0) {
        FAIL("algoritmo sin Aging acumuló edades de víctima");
    }

    if (s->algo == ALG_SECOND_CHANCE) {
        int full_faults = s->st.faults - s->st.free_loads;
        if (s->st.scans != s->st.ref_cleared + full_faults)
            FAIL("marcos examinados = %d, bits limpiados + fallos con memoria "
                 "llena = %d",
                 s->st.scans, s->st.ref_cleared + full_faults);
    } else if (s->st.ref_cleared != 0 || s->st.scans != 0) {
        FAIL("algoritmo sin Second Chance acumuló métricas de reloj");
    }

    if (s->step != nrefs)
        FAIL("paso final = %d, se esperaban %d referencias", s->step, nrefs);

#undef FAIL
    return 1;
}
