# Simulador de planificación de CPU

**Proyecto:** algoritmos de planificación — *Sistemas Operativos*
**Algoritmos seleccionados:** Shortest Remaining Time Next (3 pts) + Múltiples colas por prioridad (3 pts) = **6 puntos**
**Datos de prueba:** `Procesos.txt` (100 procesos)
**Repositorio:** https://github.com/MxDui/sistemas-operativos — carpeta `tarea-3/`
**Estado:** ✅ compila con `-Wall -Wextra -Wpedantic`, pasa 10/10 self-tests y coincide proceso por proceso con una implementación de referencia independiente

También se implementaron **FCFS**, **SJF**, **Priority** y **Round-Robin** como líneas base de comparación (no cuentan para el puntaje).

---

## 1. Selección y puntaje

| Integrantes | Mínimo | Esta entrega |
|---|---|---|
| 1 | 3 | SRTN (3) + MLQ (3) = 6 |
| 2 | 4 | SRTN (3) + MLQ (3) = 6 |

Equipo de **2 integrantes**: 6 ≥ 4. Ninguno de los dos algoritmos seleccionados estaba tomado por otro equipo al momento de la elección (regla: máximo dos equipos por algoritmo).

FCFS y Round-Robin **no** se cuentan como algoritmos seleccionados: FCFS solo vale para trabajo individual, y RR ya estaba registrado por otro equipo. Los dos se corren igual, como línea base de comparación (junto con SJF y Priority).

---

## 2. Cómo funciona cada algoritmo

El reloj de la simulación es discreto (unidades enteras). En cada instante `t` se admiten las llegadas con `arrival == t` y después se decide quién usa la CPU.

### 2.1 SRTN (seleccionado, 3 pts)

Versión **expulsiva** de SJF. En cada evento (llegada o fin) se elige el proceso listo con **menor tiempo restante**. Si un recién llegado tiene menos remaining que el actual, lo expulsa.

Empates: menor `arrival`, luego menor `id`. Si el remaining es igual, el actual suele quedarse (no se expulsa “de gusto”).

SRTN minimiza el tiempo medio de espera entre los algoritmos que conocen la ráfaga. El costo: los jobs largos se postergan (inanición).

### 2.2 Múltiples colas por prioridad (seleccionado, 3 pts)

Una cola FIFO por cada prioridad distinta, ordenadas de mayor a menor prioridad (menor número = más prioridad). En cada instante se atiende **siempre la cola no vacía de mayor prioridad**; dentro de cada cola el reparto es **Round-Robin**.

**Prioridad absoluta entre colas** (Silberschatz §5.3.4): si llega un proceso a una cola mejor que la del proceso en CPU, lo expulsa en ese mismo instante. El expulsado vuelve al **frente** de su propia cola (no consumió su quantum) y al reanudar recibe quantum nuevo: el quantum mide servicio continuo.

Decisiones de la implementación:

- **Sin realimentación:** un proceso nunca cambia de cola (eso sería MLFQ, otro algoritmo de la tabla).
- **Quantum `q = 4` por defecto**, igual para todas las colas (configurable con `-q`). Así la comparación contra RR q=4 es a costo igual.
- Mismas convenciones de cola que RR: las llegadas de `t` se admiten antes de reencolar y las llegadas simultáneas entran por `(arrival, id)`.

En `Procesos.txt` (prioridad = ráfaga / 2) esto se traduce en: las colas 1 (ráfaga 2) y 2 (ráfaga 4) corren al instante (respuesta 0); la cola 3 (ráfaga 6) espera poco; las colas 4 (ráfaga 8) y 5 (ráfaga 10) se postergan hasta que las mejores se vacían — la cola 5 no arranca hasta t ≈ 400.

### 2.3 Algoritmos de comparación (línea base)

| Algoritmo | Tipo | Criterio al despachar |
|---|---|---|
| FCFS | no expulsivo | menor llegada, luego menor id |
| SJF | no expulsivo | menor ráfaga original |
| Priority | no expulsivo | menor número de prioridad |
| RR | expulsivo | cola FIFO circular, quantum fijo |

**Prioridad:** número menor = mayor prioridad (convenio clásico del libro, el mismo que usa MLQ).

**RR** se corre con varios quantums (`q ∈ {1, 2, 4, 8}`) para medir la sensibilidad al quantum:

- `q` chico → mejor tiempo de respuesta, muchos cambios de contexto.
- `q` grande → RR se parece a FCFS: menos cambios, peor respuesta.

Sus convenios de cola (llegadas de `t` antes de reencolar; admisión por `(arrival, id)`) son los mismos que usa MLQ dentro de cada cola.

---

## 3. Formato de entrada y métricas

Cada línea de `Procesos.txt`:

```
id   arrival   burst   priority
```

Ejemplo: `1  0  2  1` → P1 llega en t=0, pide 2 u.t., prioridad 1.

Los 100 procesos del archivo de prueba:

- llegan cada 2 u.t. (`0, 2, …, 198`)
- ráfagas cíclicas `2, 4, 6, 8, 10`
- prioridades cíclicas `1, 2, 3, 4, 5`

En **este** dataset, `priority == burst / 2`. Por eso Priority y SJF dan **el mismo** schedule, y por eso la cola 5 de MLQ son exactamente los jobs de ráfaga 10.

Métricas por proceso:

| Métrica | Fórmula |
|---|---|
| Respuesta | primer instante en CPU − `arrival` |
| Retorno (TAT) | `finish − arrival` |
| Espera | `TAT − burst` |

Métricas globales: promedios, utilización (`(makespan − idle) / makespan`), throughput (`n / makespan`), cambios de contexto (cuando la CPU pasa de un proceso a otro; el primer despacho no cuenta).

---

## 4. Estructuras de datos

```
Process[]          arreglo de PCB (id, arrival, burst, priority, remaining,
                   start, finish, wait, TAT, response)
Queue              cola circular (índices al arreglo); push_front reserva
                   el frente para el proceso expulsado
queues[P]          una cola por nivel de prioridad en MLQ (P = prioridades
                   distintas, ordenadas ascendente)
Gantt / Slice      segmentos compactos (pid, start, end); se fusionan
                   rebanadas consecutivas del mismo proceso
Metrics            promedios y totales de una corrida
```

No hace falta un heap: hay 100 procesos. SJF / SRTN / Priority recorren el arreglo y eligen el mejor listo (`pick_ready`). RR y MLQ usan colas FIFO (MLQ una por nivel).

El lazo de SRTN es por **eventos** (salta al próximo arrival o al fin del actual). RR y MLQ avanzan de a 1 tick porque el quantum corta la ráfaga; en MLQ además cada tick se re-evalúa la cola de mayor prioridad, porque la expulsión entre colas es inmediata.

---

## 5. Decisiones de implementación

1. **Copia + reset por algoritmo.** Cada simulación parte de los mismos 100 procesos; `remaining` vuelve a `burst`.
2. **Self-tests embebidos** (`--self-test`) con casos de Silberschatz/Tanenbaum *antes* de correr el archivo oficial. Si el promedio de espera de FCFS no es 17, el de SRTN no es 6.5 o el de MLQ q=2 no es 3, algo está mal.
3. **Invariantes** después de cada corrida: `TAT = finish − arrival`, `wait = TAT − burst`, `response = start − arrival`, `finish ≥ arrival + burst`, `remaining = 0`.
4. **Conservación de tiempo:** `suma(burst) + idle = makespan` (`último finish − primera llegada`). En `Procesos.txt` da `600 + 0 = 600` en todos los algoritmos. El hueco `[0, primera llegada)` no se cuenta como ocioso: no hay nada que planificar todavía. Utilización = `suma(burst) / makespan`.
5. **RR se corre con varios quantums** (`1, 2, 4, 8`) como línea base, para medir la sensibilidad al quantum; **MLQ usa q = 4** por defecto (override con `-q`).
6. **Priority no expulsivo**, para contrastarlo con SRTN (expulsivo) y no duplicar la idea de “siempre el mejor”.
7. **MLQ con prioridad absoluta entre colas** (Silberschatz): la expulsión entre colas es inmediata, no se espera al fin del quantum. El expulsado vuelve al frente de su cola y conserva su turno; al reanudar recibe quantum nuevo.
8. **MLQ sin realimentación:** los procesos nunca cambian de cola. Es la diferencia con MLFQ, que degrada a los que agotan su quantum.

---

## 6. Compilar y ejecutar

```bash
cd tarea-3
make            # gcc -std=c11 -Wall -Wextra -Wpedantic
make test       # self-test + Procesos.txt → results/
./simulador Procesos.txt                  # todos los algoritmos (resumen)
./simulador -a srtn -f -g Procesos.txt    # SRTN, tabla y Gantt
./simulador -a mlq -f Procesos.txt        # MLQ con q=4 (default)
./simulador -a mlq -q 2 Procesos.txt      # MLQ con otro quantum
./simulador -a rr -q 4 -f Procesos.txt    # RR q=4 (línea base)
./simulador --self-test
```

También lee stdin: `./simulador < Procesos.txt`.

```
Uso: ./simulador [opciones] [archivo]
  -a, --algo NOMBRE   fcfs|sjf|srtn|priority|mlq|rr|all   (default: all)
  -q, --quantum N     RR: repetible (default 1 2 4 8); MLQ usa el primero (default 4)
  -f, --full          tabla por proceso
  -g, --gantt         diagrama de Gantt compacto
  -t, --self-test     casos clásicos de validación
  -h, --help          esta ayuda
Archivo '-' = stdin. Sin archivo: Procesos.txt si existe, si no stdin.
```

Archivos:

```
tarea-3/
  include/process.h schedulers.h
  src/process.c schedulers.c simulador.c
  Procesos.txt
  Makefile  test.sh  README.md
  EXPO.md             (guion de la exposición)
  DIAGRAMAS.md        (diagramas de estructuras y flujo)
  results/            (lo genera make test)
```

---

## 7. Resultados con `Procesos.txt`

100 procesos, ráfaga total **600**, primera llegada **0**, última llegada **198**. En todos los algoritmos: **makespan = 600**, **idle = 0**, **CPU = 100 %**, **throughput = 0.1667 proc/u.t.**

### 7.1 Promedios globales

| Algoritmo | Espera | Retorno | Respuesta | Cambios de ctx |
|---|---:|---:|---:|---:|
| **SRTN** | **129.66** | **135.66** | 129.54 | 105 |
| **MLQ q=4** | 154.84 | 160.84 | 108.90 | 179 |
| SJF | 129.78 | 135.78 | 129.78 | 99 |
| Priority | 129.78 | 135.78 | 129.78 | 99 |
| FCFS | 194.00 | 200.00 | 194.00 | 99 |
| RR q=8 | 219.88 | 225.88 | 180.92 | 119 |
| RR q=4 | 240.64 | 246.64 | 122.82 | 179 |
| RR q=2 | 250.16 | 256.16 | 71.52 | 299 |
| RR q=1 | 274.93 | 280.93 | **38.11** | 597 |

`results/simulacion.txt` tiene la tabla de los 100 procesos por algoritmo.

### 7.2 Espera media según la ráfaga

Hay 20 procesos de cada tamaño. Aquí se ve *quién* paga el promedio.

| Ráfaga | FCFS | SJF / Prio | SRTN | MLQ q=4 | RR q=4 | RR q=1 |
|---|---:|---:|---:|---:|---:|---:|
| 2 | 190.00 | 1.90 | 0.70 | **0.00** | 118.70 | 83.40 |
| 4 | 190.00 | 1.90 | 1.90 | **0.00** | 119.40 | 191.00 |
| 6 | 192.00 | 38.10 | 38.70 | 53.20 | 280.40 | 292.40 |
| 8 | 196.00 | 215.00 | 215.00 | 253.00 | 284.80 | 375.90 |
| 10 | 202.00 | 392.00 | 392.00 | 468.00 | 399.90 | 431.95 |

### 7.3 Ejemplos puntuales (SRTN vs MLQ vs RR q=4)

| PID | Burst | SRTN espera | SRTN inicio–fin | MLQ espera | MLQ inicio–fin | RR q=4 espera | RR q=4 inicio–fin |
|---|---:|---:|---|---:|---|---:|---|
| 1 | 2 | 0 | 0–2 | 0 | 0–2 | 0 | 0–2 |
| 5 | 10 | 392 | 400–410 | 544 | 400–562 | 90 | 14–108 |
| 6 | 2 | 2 | 12–14 | 0 | 10–12 | 8 | 18–20 |
| 100 | 10 | 392 | 590–600 | 392 | 476–600 | 392 | 446–600 |

P5 (job largo temprano): SRTN **no le da CPU hasta t=400** y lo termina en 410; MLQ también lo deja hasta t=400, pero además lo hace esperar a que su propia cola (la 5) se reparta por RR, así que termina en 562. En RR q=4 entra desde t=14. Los promedios de SRTN/MLQ ganan porque hay muchos jobs cortos; el job largo individual pierde.

---

## 8. Comparación

1. **SRTN gana en espera/retorno** (129.66 / 135.66). SJF queda a 0.12 porque este patrón de llegadas deja poco margen para expulsar: solo 6 cambios extra (105 vs 99).
2. **MLQ paga la prioridad con espera, pero gana respuesta.** Respeta una política que SRTN ignora: las colas 1–2 (40 procesos) tienen **respuesta 0**; el costo lo pagan las colas 4–5. Su respuesta media (108.90) es mejor que la de SRTN (129.54) y que la de RR q=4 (122.82), con los **mismos 179 cambios** que RR q=4.
3. **MLQ vs RR q=4 a igual quantum:** MLQ gana en espera (154.84 vs 240.64) y en respuesta (108.90 vs 122.82) con el mismo número de cambios. La diferencia es solo el orden por clases de prioridad.
4. **Priority ≡ SJF** en *este* archivo: la prioridad es función lineal de la ráfaga. En un dataset real no coincidirían — y MLQ tampoco: no mira la ráfaga, solo la clase, así que con prioridades no correlacionadas su comportamiento cambiaría por completo.
5. **FCFS es más justo entre tamaños** (espera ~190–202 para todos) pero el promedio es peor: los jobs de 2 u.t. se quedan atrás de los de 10. Es el efecto convoy.
6. **RR no gana el promedio de espera** — y no es su objetivo. Gana **respuesta**: q=1 deja a todos en ~38 u.t.; SRTN deja a los jobs de 10 en 392. A mayor `q`, RR se acerca a FCFS (q=8: espera 219.88 vs FCFS 194; respuesta 180.92 vs 194).
7. **Inanición:** en SRTN/SJF los 20 procesos de ráfaga 10 esperan **exactamente 392** y corren en bloque al final (`400…600`); en MLQ la cola 5 arranca también en t≈400 (P5: espera 544). En este dataset el verdugo es el mismo (prioridad = ráfaga/2), pero por razones distintas: SRTN mira el remaining, MLQ nunca mira la ráfaga. RR los intercala desde el principio.
8. **Costo de RR:** 597 cambios con q=1 frente a 105 de SRTN. En un kernel real cada cambio cuesta.

```
mejor espera ──────────►  SRTN ≈ SJF = Priority  <  MLQ  <  FCFS  <  RR(q grande)  <  RR(q chico)
mejor respuesta ───────►  RR(q=1)  <  RR(q=2)  <  MLQ  <  RR(q=4)  <  SRTN  <  FCFS
más cambios de ctx ────►  RR(q=1)  >>  RR(q=2)  >  RR(q=4) = MLQ  >  RR(q=8)  >  SRTN  >  FCFS/SJF
```

---

## 9. Self-tests (casos de libro)

| Caso | Esperado | Obtenido |
|---|---|---|
| FCFS P1=24, P2=3, P3=3 @ t=0 | espera 17, TAT 27 | 17 / 27 |
| SJF mismos procesos | espera 3, TAT 13 | 3 / 13 |
| RR q=4 mismos procesos | espera 5.6667, TAT 15.6667 | 5.6667 / 15.6667 |
| SRTN 8@0, 4@1, 9@2, 5@3 | espera 6.5, TAT 13 | 6.5 / 13 |
| Priority 10/3, 1/1, 2/4, 1/2 | espera 3.75 | 3.75 |
| RR q=2 con 1.ª llegada en t=3 y hueco ocioso | idle 2, makespan 11, CPU 81.82 % | OK |
| RR q=2 llegadas simultáneas desordenadas en el archivo | admisión por id | OK |
| MLQ q=2: prio 2 espera a que la cola 1 se vacíe; RR dentro de cada cola | espera 3, TAT 19/3 | 3 / 6.3333 |
| MLQ q=4: expulsión entre colas (P1 reanuda en t=4, fin 10) | espera 1, TAT 6, 2 cambios | 1 / 6 / 2 |
| MLQ q=4: el quantum expira justo cuando llega otro a la misma cola | la llegada entra antes que el expirado (P2 corre 4–6) | espera 1 / TAT 6 |

```
$ ./simulador --self-test
Self-test: 10 comprobaciones, 0 fallos.
```

Además, se validó contra una implementación de referencia independiente (Python, tick a tick): tiempos de inicio y fin **idénticos para los 100 procesos** en FCFS, SJF, SRTN, Priority y RR con q ∈ {1, 2, 3, 4, 8}; MLQ se validó igual con q ∈ {1, 2, 3, 4, 5, 8} y en 8 datasets aleatorios con huecos de CPU ociosa, llegadas simultáneas y prioridades arbitrarias (62/62 corridas idénticas en total).

---

## 10. Conclusiones

- Con los datos oficiales, **SRTN es el mejor promedio de espera** (129.66) y **MLQ el mejor compromiso con las prioridades**: respuesta media 108.90 (mejor que SRTN y que RR q=4), pagando en las colas bajas.
- El archivo de prueba **acopla prioridad y ráfaga**. Sirve para ver SJF/MLQ, pero no para defender Priority ni MLQ como políticas independientes del tamaño: hay que decirlo en la expo.
- Elegir el algoritmo es elegir *a quién perjudicar*. SRTN perjudica a los jobs largos (los 20 de ráfaga 10 esperan 392 en bloque); MLQ perjudica a la clase baja (cola 5: espera media 468, arranca en t≈400); FCFS perjudica a los cortos que llegan detrás de un convoy; RR reparte el daño y cobra en cambios de contexto.
- El quantum no es un detalle: en RR, pasar de q=1 a q=4 baja los cambios de 597 a 179 y sube la respuesta de 38 a 123; en MLQ el quantum reparte el servicio *dentro* de cada clase.

---

## 11. Integrantes

> Completar nombres y quién explica cada bloque en clase (sugerencia: uno SRTN + comparación, otro MLQ + prioridades).

| Integrante | Parte |
|---|---|
| | SRTN, comparación de resultados |
| | Múltiples colas por prioridad, prioridad absoluta, quantums |

---

## 12. Cumplimiento de los requisitos de la actividad

| Requisito del reporte | Dónde se cumple |
|---|---|
| Algoritmo(s) seleccionado(s) | §1 — SRTN (3 pts) + Múltiples colas por prioridad (3 pts) = **6 pts** (equipo de 2 → mínimo 4) |
| Cómo funciona cada algoritmo | §2 — SRTN (§2.1), MLQ (§2.2), líneas base (§2.3) |
| Estructuras de datos utilizadas | §4 — `Process`, `Queue`, `Gantt`/`Slice`, `Metrics` |
| Decisiones tomadas durante la implementación | §5 (ocho decisiones) y §2.2 (convenios de MLQ) |
| Resultados de la simulación | §7 — promedios, espera por ráfaga y casos puntuales con `Procesos.txt` |
| Comparación de resultados | §8 — SRTN vs MLQ vs SJF / Priority / FCFS / RR |
| Conclusiones | §10 |
| Round-Robin con varios quantums e inclusión de q = 4 | §2.3 y §5.5 — `q ∈ {1, 2, 4, 8}` (RR no se cuenta como seleccionado, ver §1) |

Entregables: código fuente (§6), repositorio en GitHub (URL arriba), reporte (este README), resultados (§7 y `results/simulacion.txt`, generado por `make test`) y exposición (guion en `EXPO.md`, diagramas en `DIAGRAMAS.md`).

---

*Código: `src/simulador.c` · `src/schedulers.c` · `src/process.c` · `test.sh` · `README.md`*
