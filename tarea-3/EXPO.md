# Check-list para la exposición (10% de la nota)

**Simulador:** planificación de CPU — SRTN + Múltiples colas por prioridad (6 pts) sobre `Procesos.txt`
**Demo:** laptop, terminal, `cd tarea-3 && make && ./simulador Procesos.txt`

---

## 1. Ya está hecho

- Compila con `-Wall -Wextra -Wpedantic`
- **10/10 self-tests** de casos de libro
- 100 procesos oficiales: makespan 600, CPU 100 %, tablas en `results/simulacion.txt`
- README con algoritmos, estructuras, decisiones, resultados y comparación

## 2. Pendiente

- [ ] README §11: nombres del equipo (somos 2) y quién explica qué
- [ ] Registrar en clase/Excel la selección: SRTN + Colas multinivel (al domingo, 0 equipos cada uno)
- [ ] Leer el README en voz alta una vez — es el guion
- [ ] Tener `./simulador --self-test` y `./simulador -a mlq -f Procesos.txt` listos

## 3. Demo (3–4 min)

```bash
cd tarea-3
make
./simulador --self-test          # 10/10, enseña que no nos inventamos los números
./simulador Procesos.txt         # tabla comparativa al final
./simulador -a srtn -f Procesos.txt | head -20   # P5 empieza en t=400
./simulador -a mlq -f Procesos.txt | head -20    # colas 1-2 al instante; cola 5 hasta t≈400
```

El contraste de P5 (ráfaga 10) es lo más vistoso: SRTN y MLQ lo dejan hasta t=400; RR q=4 le da CPU desde t=14.

## 4. Conceptos que debes poder explicar

| Concepto | Una frase |
|---|---|
| Despachador | Elige, entre los listos, quién toma la CPU ahora |
| Expulsivo vs no | SRTN/RR/MLQ pueden quitar la CPU; FCFS/SJF/Priority no |
| Quantum | Tope de tiempo continuo; si se acaba, el proceso va al final de su cola (RR y MLQ) |
| Remaining | Ráfaga que aún falta; SRTN ordena por eso, no por la ráfaga original |
| Colas multinivel | Una cola por prioridad; la mejor cola manda; RR dentro de cada una |
| Prioridad absoluta | Si llega un proceso a una cola mejor, expulsa al que está en CPU |
| Realimentación | MLQ **no** la tiene (nadie cambia de cola); MLFQ sí — son algoritmos distintos |
| Espera | TAT − burst (incluye tiempo expulsado) |
| Respuesta | Primera vez en CPU − llegada (RR y MLQ brillan aquí) |
| Efecto convoy | Un job largo al frente de FCFS retrasa a todos los cortos |
| Inanición | SRTN: los burst=10 esperan 392 y corren al final; MLQ: la cola 5 arranca en t≈400 |
| Cambio de contexto | Costo de RR: 597 con q=1 vs 105 de SRTN |

## 5. Números que hay que decir (sin leer la tabla entera)

- 100 procesos, ráfaga total 600, CPU 100 % en todos
- **SRTN espera 129.66** — el mejor promedio
- **MLQ q=4: espera 154.84, respuesta 108.90, 179 cambios** — el mejor compromiso con prioridades
- **SJF/Priority 129.78** — idénticos porque en este archivo `prioridad = ráfaga/2`
- **FCFS espera 194**
- **RR q=4 espera 240.64** (línea base); **RR q=1 respuesta 38.11** pero 597 cambios
- En MLQ las colas 1-2 (burst 2 y 4) tienen **respuesta 0**; la cola 5 espera **468** en promedio
- Jobs cortos (burst 2) en SRTN: espera 0.70; jobs largos (10): espera **392**

## 6. Preguntas probables

| Pregunta | Respuesta corta |
|---|---|
| "¿Por qué eligieron esos dos?" | SRTN: óptimo en espera cuando se conoce la ráfaga. MLQ: prioridad de clase con RR dentro (0 equipos, 6 pts). RR ya estaba registrado, quedó de línea base. |
| "¿MLQ y MLFQ son lo mismo?" | No. MLQ no realimenta: el proceso nunca cambia de cola. MLFQ (E09) sí lo degrada cuando agota su quantum. |
| "¿MLQ es expulsivo?" | Sí, en dos sentidos: entre colas (prioridad absoluta) y dentro de cada cola (quantum de RR). |
| "¿Qué pasa si dos procesos tienen la misma prioridad?" | Van a la misma cola y se reparten por RR, en orden de llegada. |
| "¿Por qué Priority = SJF?" | En `Procesos.txt` la prioridad es 1,2,3,4,5 al mismo tiempo que la ráfaga es 2,4,6,8,10. Mismo orden. Por eso MLQ también se parece a SJF *en este archivo*; con otras prioridades cambiaría. |
| "¿SRTN es óptimo?" | Óptimo para *espera media* si se conoce la ráfaga. No es justo con los largos. |
| "¿Por qué varios quantums?" | RR es línea base: varios q muestran el trade-off respuesta vs cambios (y q=4 queda comparable con MLQ). |
| "¿Qué cola usa RR/MLQ?" | Circular FIFO de índices. Las llegadas de t entran *antes* de reencolar al que se le acabó el quantum. |
| "¿Por qué no un heap en SRTN?" | n=100; un barrido del arreglo es simple y suficiente. |
| "¿FCFS cuenta para el equipo?" | No: FCFS solo lo puede seleccionar alguien que trabaje solo. Lo tenemos de baseline. |
| "¿Qué pasa si llegan dos con el mismo remaining?" | Gana el de menor arrival, luego menor id. No se expulsa si el actual empata. |

## 7. Estructura sugerida (8–10 min)

1. **Qué es planificar la CPU** (1 min) — cola de listos, un CPU, métricas
2. **Qué elegimos y por qué** (1 min) — SRTN + MLQ = 6 pts; uno óptimo en espera, otro por prioridad de clase
3. **Cómo corre el simulador** (2 min) — PCB, colas por nivel, `pick_ready`, self-test
4. **Demo y tabla** (3 min) — comparación + P5 en SRTN/MLQ vs RR
5. **Conclusión** (1 min) — SRTN gana el promedio; MLQ gana respuesta respetando prioridades; el que paga siempre existe

## 8. La noche anterior

- [ ] `make test` en limpio
- [ ] Explicar P5 sin mirar el README
- [ ] Decir de memoria: SRTN 129.66, MLQ 154.84/108.90, cola 5 arranca en t≈400, burst 10 espera 392 (SRTN)
