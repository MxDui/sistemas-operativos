# sysstats — Módulo de kernel: mini-estadísticas del sistema

**Proyecto:** módulos del kernel de Linux — *Operating System Concepts, 10th ed.*, cap. 2
**Funcionalidad elegida:** mini-estadísticas del sistema (vía `/proc`)
**Entorno de desarrollo:** VM Ubuntu 24.04 (QEMU/KVM), kernel `6.8.0-138-generic`
**Estado:** ✅ compila y pasa 10/10 pruebas automatizadas

---

## 1. ¿Qué hace el módulo?

Crea el archivo `/proc/sysstats`. Cada vez que se lee, muestra en vivo:

| Estadística | API del kernel | Notas |
|---|---|---| 
| `jiffies (raw)` | `get_jiffies_64()`, `HZ` | Contador de ticks; en 64 bits arranca en `2³² − 300·HZ` (ver §4) |
| `jiffies since boot` | `jiffies_64 − INITIAL_JIFFIES` | Demuestra la corrección del offset |
| `uptime` | `ktime_get_boot_fast_ns()` | Uptime real (igual al de `/proc/uptime`) |
| `load average` 1/5/15 min | `avenrun[]`, `LOAD_INT()/LOAD_FRAC()` | Array exportado del kernel |
| `mem total/free/buffers`, `swap` | `si_meminfo()` + `struct sysinfo` | Con unidades SI (1000) o IEC (1024) |
| `cpus online` | `num_online_cpus()` | |

**Parámetro configurable:** `unit = si|iec` (cambia las unidades de memoria)
- Al cargar: `sudo insmod sysstats.ko unit=iec`
- En caliente: `printf 'iec' > /sys/module/sysstats/parameters/unit`

```console
$ cat /proc/sysstats
Linux mini-stats  (/proc/sysstats)
----------------------------------------
jiffies (raw)       : 4294965377 ticks (HZ = 1000)
jiffies since boot  : 298081 ticks = jiffies_64 - INITIAL_JIFFIES
uptime              : 298.21 s (ktime_get_boot_fast_ns)
load average        : 0.07 0.04 0.00 (1/5/15 min)
memory unit        : SI (1000)
mem total              3.04 GB
mem free               1.85 GB
mem buffers            38.15 MB
swap total             0.00 KB
swap free              0.00 KB
cpus online         : 4
module loaded since : 0.00 s ago
```

## 2. Compilar, cargar y probar

```bash
make            # compila contra /lib/modules/$(uname -r)/build
sudo insmod sysstats.ko          # cargar
cat /proc/sysstats               # probar
printf 'iec' > /sys/module/sysstats/parameters/unit   # cambiar unidad en caliente
sudo rmmod sysstats              # descargar
sudo dmesg | tail                # mensajes del módulo
./test.sh        # suite automatizada (10 pruebas, verifica contra /proc/uptime y free)
```

Requisitos: `build-essential` + `linux-headers-$(uname -r)`.

## 3. Arquitectura del código (`sysstats.c`)

```
module_init() → sysstats_init()      registra el archivo /proc con proc_create()
module_exit() → sysstats_exit()      proc_remove()
                     │
/proc/sysstats ⇄ proc_ops { .proc_open = single_open, .proc_read = seq_read }
                     │
              sysstats_show()        [seq_file] → escribe las estadísticas
                     ├─ get_jiffies_64() / ktime_get_boot_fast_ns()   (tiempo)
                     ├─ avenrun[] + LOAD_INT/LOAD_FRAC                (load avg)
                     ├─ si_meminfo(&si)                               (memoria)
                     └─ num_online_cpus()                             (CPUs)
                     
module_param(unit, charp, 0644)      parámetro configurable (sysfs)
```

## 4. Tabla de investigación (concepto → API del kernel → uso)

| Concepto (cap. 2) | Ejemplo del libro | Función/variable del kernel | Cómo se usa en `sysstats` |
|---|---|---|---|
| Módulo mínimo de kernel | `hello.c` | `module_init()`, `module_exit()`, `MODULE_LICENSE` | `sysstats_init()/sysstats_exit()` |
| Mensajes al kernel | `hello.c` (`printk`) | `pr_info()`, `dmesg` | Avisos de carga/descarga |
| Compilar fuera del árbol | Makefile del capítulo | `obj-m`, `KERNELDIR=/lib/modules/$(uname -r)/build` | `Makefile` propio |
| /proc con un archivo | `proc_example.c` (/proc/hello) | `proc_create()`, `proc_ops`, `proc_remove()` | `/proc/sysstats` |
| Lectura con seq_file | `proc_example.c` | `single_open()`, `seq_printf()`, `seq_read` | `sysstats_show()` escribe el informe |
| Contador de ticks | `jiffies.c` (/proc/jiffies) | `jiffies_64`, `HZ`, `get_jiffies_64()`, `INITIAL_JIFFIES` | líneas jiffies crudo + corregido |
| Tiempo real (uptime) | — (extensión) | `ktime_get_boot_fast_ns()` | uptime y edad del módulo |
| Parámetros al módulo | `hello-params.c` | `module_param()`, `/sys/module/<m>/parameters/`, `strim()` | parámetro `unit` (SI/IEC) |
| Estadísticas de memoria | — (extensión) | `si_meminfo()`, `struct sysinfo`, `EXPORT_SYMBOL` | memoria y swap |
| Load average | — (extensión) | `avenrun[]`, `LOAD_INT()`, `LOAD_FRAC()`, `FIXED_1` | load avg 1/5/15 min |
| Núm. de CPUs | — (extensión) | `num_online_cpus()` | `cpus online` |
| Herramientas | §2.7.2 | `insmod`, `rmmod`, `lsmod`, `dmesg`, `modinfo` | ciclo completo de prueba |

## 5. Hallazgos de la investigación (diferencia entre el libro y 6.8)

1. **`HZ` es configurable (1–1000)**. Ubuntu 24.04 usa `CONFIG_HZ=1000`; el libro supone 250. El módulo imprime su valor real.
2. **`jiffies` no empieza en 0**: `INITIAL_JIFFIES = (unsigned long)(unsigned int)(−300·HZ)` = `2³² − 300·HZ` (detecta pronto el *wraparound* de 32 bits). Por eso `jiffies/HZ` **≠ uptime** en kernels 64 bits modernos, y el ejemplo `/proc/jiffies` del libro muestra valores enormes. La corrección: `jiffies_64 − INITIAL_JIFFIES` (se demuestra en el módulo) y el uptime real se lee con `ktime_get_boot_fast_ns()`.
3. **Muchas APIs del libro no están exportadas en 6.8** — se comprueba con `grep <símbolo> /usr/src/linux-headers-$(uname -r)/Module.symvers`:
   - `get_avenrun()` → ❌ … la solución es el **array `avenrun[]`** (✅ `EXPORT_SYMBOL`)
   - `nr_threads` → ❌ (los hilos solo se leen desde `/proc/loadavg`)
   - `ktime_get_boottime()` → ❌ … `ktime_get_boot_fast_ns()` ✅
4. **`si_meminfo()` ya no rellena `si.procs`** (queda 0); ese campo lo completa el syscall `sysinfo(2)`.
5. **Escribir un `charp` por sysfs incluye el `\n`** de `echo`; hay que recortarlo (`strim()`) o usar `printf '%s'` — fallo clásico documentado en el código.

## 6. Resultados de las pruebas (VM, kernel 6.8.0-138)

```
Resultado: 10 aprobadas, 0 fallidas — TODO OK ✅
```

- `uptime` del módulo vs `/proc/uptime`: **diff = 0 s** (298 s)
- `mem total` vs `free -b`: **diff 0%** (≈3.04 GB)
- Cambio de unidad SI↔IEC en caliente: ✅ (los valores pasan de GB→GiB)
- Carga con parámetro `unit=iec`: ✅
- Carga/descarga repetida sin fugas ni errores en `dmesg`: ✅

## 7. Reproducción del entorno de pruebas (QEMU/KVM)

El módulo se desarrolló **en una VM**, sin tocar el sistema anfitrión:

```bash
# 1. Descargar imagen cloud y crear seed con clave SSH
wget -q -O vm/noble.img https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img
genisoimage -output vm/seed.iso -volid cidata -joliet -rock vm/user-data vm/meta-data

# 2. Crear disco y arrancar (KVM; SSH en el puerto 2222; carpeta del repo
#    compartida por 9p montada en /mnt/host)
qemu-img create -f qcow2 -b vm/noble.img -F qcow2 vm/disk.qcow2 8G
qemu-system-x86_64 -machine q35,accel=kvm -cpu host -smp 4 -m 3072 \
  -drive file=vm/disk.qcow2,if=virtio,format=qcow2 \
  -drive file=vm/seed.iso,media=cdrom \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:2222-:22 -device virtio-net-pci,netdev=n0 \
  -virtfs local,path=$PWD,mount_tag=host0,security_model=none \
  -display none -serial file:vm/console.log -daemonize -pidfile vm/qemu.pid

# 3. Dentro de la VM (una sola vez)
ssh -p 2222 ubuntu@127.0.0.1 'sudo apt-get install -y linux-headers-$(uname -r) build-essential; \
  sudo mount -t 9p -o trans=virtio,version=9p2000.L host0 /mnt/host'

# 4. Compilar y probar (el 9p comparte la carpeta del repo)
ssh -p 2222 ubuntu@127.0.0.1 'cd /mnt/host && make && bash test.sh'
```

## 8. Integrantes y reparto

> *Pendiente: completar con los nombres del equipo y quién investigó cada fila de la tabla del §4 (sugerencia: una fila por integrante, 2–3 filas cada uno).*

---
*Módulo: `sysstats.c` · Makefile · `test.sh` (suite automatizada) · `README.md`*