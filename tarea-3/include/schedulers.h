/*
 * Archivo: schedulers.h
 * Tarea 3 — Simulación de algoritmos de planificación de CPU
 * Sistemas Operativos
 *
 * Descripción: Declara la interfaz común de los seis planificadores simulados
 * (FCFS, SJF, SRTN, Priority, Round-Robin y múltiples colas por prioridad) y
 * resume los convenios que todos comparten.
 */

#ifndef SCHEDULERS_H
#define SCHEDULERS_H

#include "process.h"

/*
 * Cada simulador trabaja sobre una copia de los procesos (se llama
 * process_reset internamente).  Si gantt != NULL se registra el
 * diagrama compacto.  Las métricas quedan en *m.
 *
 * Convenios:
 *   - número de prioridad menor = mayor prioridad
 *   - empates: menor tiempo de llegada, luego menor id
 *   - SRTN no expulsa si el nuevo proceso tiene el mismo remaining
 *     (el comparador favorece llegada/id, el actual suele quedarse)
 *   - RR: las llegadas del instante t entran a la cola ANTES de
 *     reencolar al proceso cuyo quantum acaba de expirar
 *   - MLQ: prioridad absoluta entre colas (manda la de menor número);
 *     el proceso expulsado vuelve al frente de su propia cola
 */

/*
 * @brief  Simula FCFS (no expulsivo): gana el proceso listo con menor llegada.
 * @param  p  arreglo de procesos a simular; se reescriben sus tiempos.
 * @param  n  número de procesos.
 * @param  g  Gantt de salida, o NULL si no se quiere registrar.
 * @param  m  salida: métricas globales de la corrida.
 * @return No retorna valor.
 */
void simulate_fcfs(Process *p, int n, Gantt *g, Metrics *m);

/*
 * @brief  Simula SJF (no expulsivo): gana el proceso listo de menor ráfaga
 *         original.
 * @param  p  arreglo de procesos a simular; se reescriben sus tiempos.
 * @param  n  número de procesos.
 * @param  g  Gantt de salida, o NULL si no se quiere registrar.
 * @param  m  salida: métricas globales de la corrida.
 * @return No retorna valor.
 */
void simulate_sjf(Process *p, int n, Gantt *g, Metrics *m);

/*
 * @brief  Simula SRTN (expulsivo): en cada evento corre el proceso listo con
 *         menor tiempo restante y se re-evalúa en la próxima llegada.
 * @param  p  arreglo de procesos a simular; se reescriben sus tiempos.
 * @param  n  número de procesos.
 * @param  g  Gantt de salida, o NULL si no se quiere registrar.
 * @param  m  salida: métricas globales de la corrida.
 * @return No retorna valor.
 */
void simulate_srtn(Process *p, int n, Gantt *g, Metrics *m);

/*
 * @brief  Simula Priority (no expulsivo): gana el proceso listo de menor
 *         número de prioridad.
 * @param  p  arreglo de procesos a simular; se reescriben sus tiempos.
 * @param  n  número de procesos.
 * @param  g  Gantt de salida, o NULL si no se quiere registrar.
 * @param  m  salida: métricas globales de la corrida.
 * @return No retorna valor.
 */
void simulate_priority(Process *p, int n, Gantt *g, Metrics *m);

/*
 * @brief  Simula múltiples colas por prioridad: una cola Round-Robin por cada
 *         prioridad distinta, con prioridad absoluta entre colas (los detalles
 *         están documentados en schedulers.c).
 * @param  p        arreglo de procesos a simular; se reescriben sus tiempos.
 * @param  n        número de procesos.
 * @param  quantum  ticks máximos de servicio continuo dentro de cada cola (> 0).
 * @param  g        Gantt de salida, o NULL si no se quiere registrar.
 * @param  m        salida: métricas globales de la corrida.
 * @return No retorna valor.
 */
void simulate_multilevel(Process *p, int n, int quantum, Gantt *g, Metrics *m);

/*
 * @brief  Simula Round-Robin: cola FIFO circular y quantum fijo; las llegadas
 *         del instante t se admiten antes de reencolar al expirado.
 * @param  p        arreglo de procesos a simular; se reescriben sus tiempos.
 * @param  n        número de procesos.
 * @param  quantum  ticks de CPU por turno (> 0).
 * @param  g        Gantt de salida, o NULL si no se quiere registrar.
 * @param  m        salida: métricas globales de la corrida.
 * @return No retorna valor.
 */
void simulate_rr(Process *p, int n, int quantum, Gantt *g, Metrics *m);

#endif
