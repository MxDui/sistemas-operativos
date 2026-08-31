# 📋 Check-list para la exposición (10% de la nota)

**Módulo:** `sysstats` — mini-estadísticas del sistema (`/proc/sysstats`)
**Máquina para la demo:** VM Ubuntu 24.04 (QEMU/KVM) — arrancarla con el comando del §6 del README

---

## 1. ✅ Ya está hecho (no te estreses)
- Módulo compila y pasa **10/10 pruebas** en kernel `6.8.0-138-generic`
- `README.md` completo con **tabla de investigación** (90% de la nota)
- 5 hallazgos de investigación reales documentados
- Repo git con el trabajo versionado

## 2. 🔲 Pendiente — documentación
- [ ] **README §8**: poner nombres de los integrantes y qué investigó cada quien
      (sugerencia: repartir 2–3 filas de la tabla por integrante, y que cada uno
      pueda explicar "su" fila en clase)
- [ ] Leer el `README.md` entero **en voz alta una vez** — es tu guion

## 3. 🔲 Pendiente — demo práctica (3–5 min, guion)

```bash
# dentro de la VM:
cd /mnt/host            # carpeta compartida con el repo
make                    # 1. compilar
sudo insmod sysstats.ko # 2. cargar
cat /proc/sysstats      # 3. mostrar estadísticas
# 4. comparar en vivo contra el sistema:
cat /proc/uptime        #    uptime debe cuadrar (diff 0 s)
free -h                 #    memoria debe cuadrar
# 5. cambio de unidad EN CALIENTE (lo más vistoso):
printf 'iec' | sudo tee /sys/module/sysstats/parameters/unit >/dev/null
cat /proc/sysstats      #    GB → GiB
printf 'si' | sudo tee /sys/module/sysstats/parameters/unit >/dev/null
# 6. descargar y enseñar los mensajes del kernel:
sudo rmmod sysstats
sudo dmesg | tail -3    # "sysstats: cargado..." / "descargado. Adiós."
```
- [ ] Practicar 2 veces en voz alta apuntando a la pantalla
- [ ] Tener `test.sh` como respaldo ("si quieren ver la suite completa, 10 pruebas")

## 4. 🔲 Pendiente — conceptos que DEBES poder explicar

| Concepto | Explicación de 1 frase |
|---|---|
| `insmod`/`rmmod`/`lsmod` | Cargar/descargar/listar módulos en el kernel |
| `module_init`/`module_exit` | Puntos de entrada/salida del módulo |
| `printk`/`pr_info` + `dmesg` | Cómo el kernel "imprime" mensajes |
| `/proc` | "Ventana" del kernel al espacio de usuario (ps, free, uptime lo usan) |
| `seq_file` | API del kernel para escribir archivos de lectura secuencial |
| `EXPORT_SYMBOL` | Qué funciones del kernel pueden usar los módulos (verificar con `Module.symvers`) |
| `module_param` | Parámetros configurables al cargar o en caliente por `/sys/module/` |
| Espacio kernel vs usuario | El módulo corre en el espacio del kernel; `copy_to_user`/`copy_from_user` no aplican aquí porque nunca tocamos memoria del usuario |

## 5. 🔲 Pendiente — los 5 hallazgos (tu "arma secreta" para la expo)

1. **`HZ=1000`** en Ubuntu 24.04 — el libro supone 250 → `HZ` es configurable, el módulo imprime el real
2. **`jiffies` no arranca en 0**: `INITIAL_JIFFIES = 2³² − 300·HZ` → el `/proc/jiffies` del libro mostraría ~49 días en un kernel moderno. El módulo lo demuestra y lo corrige
3. **`get_avenrun()`, `nr_threads`, `ktime_get_boottime()` NO se exportan en 6.8** → se usa `avenrun[]` y `ktime_get_boot_fast_ns()` (verificado en `Module.symvers`)
4. **`si_meminfo()` ya no rellena `si.procs`**
5. **Quirk de `charp` en sysfs**: `echo` añade `\n` → hay que usar `strim()` (¡bug real que encontramos y arreglamos!)

*Anécdota de calidad: "el libro muestra jiffies gigantes; investigamos, encontramos INITIAL_JIFFIES, y lo demostramos en pantalla" → eso es investigación de verdad.*

## 6. 🔲 Preguntas probables del profe (y respuestas)

| Pregunta | Respuesta corta |
|---|---|
| "¿Por qué `insmod` requiere sudo?" | Cargar un módulo ejecuta código privilegiado en el kernel |
| "¿Por qué el módulo no imprime nada en pantalla?" | Los mensajes van a `dmesg`, no a la consola |
| "¿Se puede cargar en cualquier kernel?" | No: debe compilarse contra los headers exactos (`uname -r`); el `.ko` es específico de la versión |
| "¿Qué pasa si el kernel cambia mientras está cargado?" | No puede: el kernel en ejecución es fijo; al reiniciar hay que recargar |
| "¿Cuál es la diferencia con un programa normal?" | Un programa usa syscalls; un módulo vive en el kernel, sin protección de memoria, y puede tumbar el sistema |
| "¿Qué es `Module.symvers`?" | La lista de símbolos que el kernel exporta para los módulos |
| "¿Por qué `MODULE_LICENSE("GPL")`?" | API de kernel "legacy" solo disponible para módulos GPL |

## 7. 🔲 Estructura sugerida (10–12 min total)

1. **Qué es un módulo y por qué** (1 min) — cargar código en el kernel sin recompilarlo
2. **Nuestra funcionalidad** (2 min) — mini-estadísticas vs ejemplos del libro (hello, /proc/hello, jiffies)
3. **Demo en vivo** (4–5 min) — guion del §3
4. **Hallazgos de investigación** (2–3 min) — los 5 del §5, con la tabla
5. **Cierre** (1 min) — qué aprendimos: kernel real ≠ libro, importancia de verificar APIs

## 8. 🔲 La noche anterior
- [ ] Cargar/descargar el módulo 3 veces seguidas sin mirar las notas
- [ ] Verificar que la VM arranca y el 9p monta (el 90% de los demos mueren por el setup, no por el módulo)
- [ ] Dormir 😴