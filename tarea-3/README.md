# Simulador de planificación de CPU

---

## 1. Selección y puntaje

Equipo de 2 integrantes. Los algoritmos seleccionados son SRTN y Múltiples colas por prioridad (MLQ). Ninguno de los dos estaba tomado ya por otro equipo al momento de la elección (regla: máximo dos equipos por algoritmo).

El simulador también incluye FCFS, SJF, Priority y Round-Robin como referencia interna para validar la implementación y dar contexto a los resultados, pero no forman parte de la selección del equipo (no cuentan para el puntaje ni son el foco del reporte).

---

## 2. Cómo funciona cada algoritmo

El reloj de la simulación es discreto (unidades enteras). En cada instante `t` primero entran los procesos cuyo `arrival` es igual a `t`, y luego se decide quién ocupa la CPU.

### 2.1 SRTN

Es la variante expulsiva de SJF: en cada evento (una llegada o el fin de un proceso) toma la CPU el proceso listo con menor tiempo restante. Cuando llega uno nuevo con menos remaining que el que está corriendo, lo saca y toma su lugar.

Los empates se resuelven por menor `arrival` y después por menor `id`. Si dos procesos quedan con el mismo remaining, se deja correr al que ya estaba (no hay expulsión sin una ventaja real).

De los algoritmos que conocen la duración de la ráfaga, SRTN es el que logra menor espera promedio. A cambio, los procesos largos quedan relegados una y otra vez mientras existan procesos más cortos por atender (ahí aparece la inanición).

### 2.2 Múltiples colas por prioridad

Cada nivel de prioridad tiene su propia cola FIFO, y las colas quedan ordenadas de la más alta a la más baja (un número menor significa mayor prioridad). El planificador siempre atiende la cola no vacía de más alto nivel, y dentro de esa cola el reparto es por Round-Robin.

Entre colas rige prioridad absoluta: en cuanto llega un proceso a una cola superior a la del que está en CPU lo desaloja. El proceso desalojado no pierde su lugar  (regresa al frente de su propia cola, y cuando vuelva a correr arrancará con un quantum completo, ya que el quantum mide servicio continuo, no acumulado).

Otras decisiones de diseño:

- No hay realimentación: ningún proceso cambia de cola durante la corrida (eso sería MLFQ, que es otro algoritmo de la tabla).
- El quantum por defecto es `q = 4` y aplica igual a todas las colas (se puede ajustar con `-q`), para que comparar contra RR q=4 sea justo.
- Las colas siguen las mismas reglas de admisión que RR: las llegadas del instante `t` entran antes de reencolar al proceso saliente, y ante llegadas simultáneas se admite primero por `(arrival, id)`.

Como en `Procesos.txt` la prioridad equivale a ráfaga entre 2, esto se traduce en un patrón: las colas 1 (ráfaga 2) y 2 (ráfaga 4) reciben CPU de inmediato (respuesta 0), la cola 3 (ráfaga 6) espera poco, y las colas 4 y 5 (ráfagas 8 y 10) quedan pausadas hasta que las de arriba se vacían. La cola 5 no arranca antes de t ≈ 400.

---

## 3. Formato de entrada y métricas

Cada línea de `Procesos.txt` trae cuatro valores:

```
id   arrival   burst   priority
```

Por ejemplo, `1  0  2  1` describe a P1: llega en t=0, necesita 2 u.t. de CPU y tiene prioridad 1.

El archivo de prueba tiene 100 procesos con este patrón:

- una llegada cada 2 u.t. (`0, 2, …, 198`)
- ráfagas que se repiten en ciclos de `2, 4, 6, 8, 10`
- prioridades que también ciclan en `1, 2, 3, 4, 5`

Al construir el archivo así, la prioridad terminó siendo `burst / 2` en todos los casos. Esa coincidencia explica por qué Priority y SJF producen exactamente el mismo orden de ejecución, y por qué la cola 5 de MLQ termina siendo, sin proponérselo, la misma que agrupa a los procesos de ráfaga 10.

Métricas por proceso:

| Métrica | Fórmula |
|---|---|
| Respuesta | primer instante en CPU − `arrival` |
| Retorno (TAT) | `finish − arrival` |
| Espera | `TAT − burst` |

Métricas globales: promedios de las tres anteriores, utilización ((makespan − idle) / makespan), throughput (n / makespan) y cambios de contexto (cuando la CPU pasa de un proceso a otro; el primer despacho no cuenta).

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

Con solo 100 procesos, un heap sería sobrekill. SRTN recorre el arreglo completo en cada evento y se queda con el proceso listo de menor remaining (función pick_ready). MLQ sí necesita colas FIFO — una por cada nivel de prioridad distinto.

El lazo de SRTN avanza por eventos: brinca directo a la próxima llegada o al momento en que termina el proceso actual, sin recorrer tick por tick. MLQ, en cambio, avanza de uno en uno, porque el quantum puede cortar la ráfaga en cualquier punto, y en cada tick hay que revisar si apareció una cola de mayor prioridad, ya que esa expulsión ocurre al instante.

---

## 5. Decisiones de implementación

1. **Copia y reinicio antes de cada ejecucion.** SRTN y MLQ parten del mismo conjunto de 100 procesos; `remaining` se restaura a `burst` en cada una.
2. **Self-tests integrados** (`--self-test`), con casos tomados de Silberschatz y Tanenbaum, que se ejecutan antes de tocar el archivo oficial. Incluyen casos específicos de SRTN (espera 6.5) y de MLQ (expulsión entre colas, empate quantum/llegada) — son la red de seguridad de la implementación.
3. **Verificación de invariantes** al terminar: `TAT = finish − arrival`, `wait = TAT − burst`, `response = start − arrival`, `finish ≥ arrival + burst`, `remaining = 0`.
4. **Conservación de tiempo.** La suma de ráfagas más el tiempo ocioso debe igualar el makespan (`último finish − primera llegada`); con `Procesos.txt` da `600 + 0 = 600` tanto en SRTN como en MLQ. El hueco antes de la primera llegada no cuenta como tiempo ocioso, porque ahí todavía no hay nada que planificar.
5. **MLQ usa `q = 4`** por defecto, con la opción de cambiarlo vía `-q` para explorar otros valores.
6. **Prioridad absoluta entre colas en MLQ** (Silberschatz): la expulsión entre niveles ocurre de inmediato, sin esperar a que se agote el quantum. El proceso desalojado conserva su turno — vuelve al frente de su cola y arranca con quantum nuevo al reanudar.
7. **Sin realimentación entre colas:** ningún proceso migra de nivel durante la simulación. Ahí está la diferencia con MLFQ, que sí degrada a quienes agotan su quantum.

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

### 7.3 Ejemplos puntuales (SRTN vs MLQ)

| PID | Burst | SRTN espera | SRTN inicio–fin | MLQ espera | MLQ inicio–fin |
|---|---:|---:|---|---:|---|
| 1 | 2 | 0 | 0–2 | 0 | 0–2 |
| 5 | 10 | 392 | 400–410 | 544 | 400–562 |
| 6 | 2 | 2 | 12–14 | 0 | 10–12 |
| 100 | 10 | 392 | 590–600 | 392 | 476–600 |

P5 (job largo temprano): SRTN no le da CPU hasta t=400 y lo termina en 410; MLQ también lo deja hasta t=400, pero además lo hace esperar a que su propia cola (la 5) se reparta por Round-Robin, así que termina hasta t=562.

---

## 8. Comparación

1. **SRTN gana en espera y retorno** (129.66 / 135.66 contra 154.84 / 160.84 de MLQ). Al no atarse a clases de prioridad, SRTN siempre elige globalmente al proceso más corto disponible; MLQ, en cambio, respeta el orden por cola aunque eso implique dejar esperando a un proceso corto si está en una cola de menor prioridad.

2. **MLQ gana en respuesta** (108.90 contra 129.54 de SRTN). Las colas 1 y 2 (40 procesos, prioridades más altas) tienen respuesta 0 — entran a la CPU en cuanto llegan — y ese beneficio se refleja en el promedio general, aunque el costo lo paguen las colas 4 y 5.

3. **Cada uno perjudica a un grupo distinto.** En SRTN, los 20 procesos de ráfaga 10 esperan exactamente 392 u.t. y corren en bloque al final (t=400 a 600) — el criterio es puramente el tamaño de la ráfaga. En MLQ, la cola 5 arranca también en t≈400, pero por una razón distinta: no es que MLQ mire la ráfaga (nunca lo hace), sino que esos procesos comparten la prioridad más baja. En este dataset ambos criterios terminan castigando al mismo grupo porque prioridad y ráfaga están correlacionadas por construcción — en un dataset donde no lo estuvieran, MLQ perjudicaría a una clase distinta.

4. **Costo en cambios de contexto:** MLQ generó 179 cambios contra 105 de SRTN — el reparto por Round-Robin dentro de cada cola cuesta más cambios que el estilo de SRTN, que solo cambia de proceso cuando de verdad conviene.

```
mejor espera      ──────►  SRTN  <  MLQ
mejor respuesta   ──────►  MLQ  <  SRTN
más cambios de ctx ─────►  MLQ  >  SRTN
```

---

## 9. Self-tests (casos de libro)
Estos self-tests validan el motor completo (incluye FCFS/SJF/Priority/RR), pero los casos relevantes a la selección del equipo son los de SRTN y MLQ:

| Caso | Esperado | Obtenido |
|---|---|---|
| SRTN 8@0, 4@1, 9@2, 5@3 | espera 6.5, TAT 13 | 6.5 / 13 |
| MLQ q=2: prio 2 espera a que la cola 1 se vacíe; RR dentro de cada cola | espera 3, TAT 19/3 | 3 / 6.3333 |
| MLQ q=4: expulsión entre colas (P1 reanuda en t=4, fin 10) | espera 1, TAT 6, 2 cambios | 1 / 6 / 2 |
| MLQ q=4: el quantum expira justo cuando llega otro a la misma cola | la llegada entra antes que el expirado (P2 corre 4–6) | espera 1 / TAT 6 |

```
$ ./simulador --self-test
Self-test: 10 comprobaciones, 0 fallos.
```

Además, se validó SRTN y MLQ contra una implementación de referencia independiente (Python, tick a tick): SRTN dio tiempos de inicio y fin idénticos para los 100 procesos; MLQ se validó igual con q ∈ {1, 2, 3, 4, 5, 8} y en 8 datasets aleatorios con huecos de CPU ociosa, llegadas simultáneas y prioridades arbitrarias, con resultados idénticos en todas las corridas.

---

## 10. Conclusiones

- Con los datos de `Procesos.txt`, **SRTN logra la mejor espera promedio** (129.66) porque siempre elige globalmente al proceso más corto, sin importar ninguna otra clasificación.
- **MLQ logra mejor respuesta** (108.90) porque garantiza atención inmediata a las clases de mayor prioridad, aunque eso implique postergar más a las clases bajas.
- El archivo de prueba acopla prioridad y ráfaga (`priority = burst / 2`), así que en este dataset ambos algoritmos terminan perjudicando al mismo grupo de procesos (los de ráfaga 10) — pero por criterios distintos: SRTN mira directamente el tiempo restante, MLQ nunca lo hace, solo mira la clase asignada. Hay que dejarlo claro en la expo: esta coincidencia es del dataset, no de los algoritmos.
- Elegir entre SRTN y MLQ es elegir qué se prioriza: tiempo total de espera (SRTN) o garantía de atención rápida por clase (MLQ), aunque la segunda cueste más cambios de contexto (179 vs 105).

---

## 11. Integrantes

| Integrante | Número de cuenta |
|---|---|
| David Rivera Morales | 320176876 |
| Yessica Vianney Montes de Oca Aguilar | 315050116 |
