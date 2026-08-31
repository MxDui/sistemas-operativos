// SPDX-License-Identifier: GPL-2.0
/*
 * sysstats.c — Módulo de mini-estadísticas del sistema para Linux.
 *
 * Proyecto: Módulos del kernel de Linux (OSC 10th ed., cap. 2)
 * Funcionalidad elegida: mini-estadísticas del sistema.
 *
 * Crea el archivo /proc/sysstats que al leerlo muestra:
 *   - Contador jiffies crudo (HZ configurable; en kernels de 64 bits
 *     arranca en INITIAL_JIFFIES = 2^32 - 300*HZ).
 *   - Uptime con CLOCK_BOOTTIME vía ktime_get_boottime_ns(); el ejemplo
 *     "/proc/jiffies" del libro muestra jiffies crudo, no uptime.
 *   - Load average (1, 5 y 15 minutos) vía el array avenrun[].
 *   - Memoria total/libre/buffers vía si_meminfo().
 *   - CPUs en línea (num_online_cpus()).
 *
 * Parámetro configurable (opcional, complemento):
 *   unit = "si"  -> unidades decimales (1000)  [valor por defecto]
 *   unit = "iec" -> unidades binarias (1024)
 *   Se puede cambiar en caliente desde
 *   /sys/module/sysstats/parameters/unit.
 *
 * Compilar:       make
 * Cargar:         sudo insmod sysstats.ko
 * Probar:         cat /proc/sysstats
 * Descargar:      sudo rmmod sysstats
 * Ver mensajes:   dmesg | tail
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sysinfo.h>          /* struct sysinfo                */
#include <linux/mm.h>               /* si_meminfo()                  */
#include <linux/sched/loadavg.h>    /* avenrun[], LOAD_INT/FRAC      */
#include <linux/cpu.h>              /* num_online_cpus()             */
#include <linux/jiffies.h>          /* HZ, get_jiffies_64()          */
#include <linux/timekeeping.h>      /* ktime_get_boottime_ns()       */
#include <linux/string.h>

#define PROC_NAME "sysstats"

/* ------------------------------------------------------------------ */
/* Parámetro configurable del módulo (complemento a las estadísticas) */
/* ------------------------------------------------------------------ */
static bool unit_is_iec;

/**
 * unit_set - Valida y guarda el parámetro de unidades de memoria.
 * @value: Cadena del usuario ("si" o "iec"; sysfs_streq acepta el '\n' final).
 * @kp: Descriptor del parámetro; @kp->arg apunta a unit_is_iec.
 *
 * Return: 0 si @value es válido; -EINVAL si no es "si" ni "iec".
 */
static int unit_set(const char *value, const struct kernel_param *kp)
{
	bool *is_iec = kp->arg;

	/* sysfs_streq() acepta tanto "iec" como "iec\n". */
	if (sysfs_streq(value, "iec"))
		WRITE_ONCE(*is_iec, true);
	else if (sysfs_streq(value, "si"))
		WRITE_ONCE(*is_iec, false);
	else
		return -EINVAL;

	return 0;
}

/**
 * unit_get - Expone el valor actual de unit por sysfs.
 * @buffer: Destino de una página; el kernel lo pasa a module_param.
 * @kp: Descriptor del parámetro; @kp->arg apunta a unit_is_iec.
 *
 * Return: Número de bytes escritos en @buffer, incluido el '\n'.
 */
static int unit_get(char *buffer, const struct kernel_param *kp)
{
	const bool *is_iec = kp->arg;

	return scnprintf(buffer, PAGE_SIZE, "%s\n",
			 READ_ONCE(*is_iec) ? "iec" : "si");
}

static const struct kernel_param_ops unit_param_ops = {
	.set = unit_set,
	.get = unit_get,
};

module_param_cb(unit, &unit_param_ops, &unit_is_iec, 0644);
MODULE_PARM_DESC(unit,
	"Unidad para la memoria: \"si\" (decimal, 1000) o \"iec\" (binario, 1024)");

static u64 mod_start_ns;          /* boot ns al cargar el módulo    */
static struct proc_dir_entry *proc_entry;

/* ------------------------------------------------------------------ */
/* Helpers de formato                                                 */
/* ------------------------------------------------------------------ */

/**
 * mem_div - Divisor de memoria según el parámetro unit.
 *
 * Return: 1024 si unit=iec; 1000 si unit=si.
 */
static unsigned long mem_div(void)
{
	return READ_ONCE(unit_is_iec) ? 1024UL : 1000UL;
}

/**
 * seq_mem_full - Imprime una cantidad de memoria con unidades SI o IEC.
 * @m: seq_file de /proc/sysstats donde se escribe la línea.
 * @label: Etiqueta de la fila (p. ej. "mem total").
 * @bytes: Cantidad absoluta en bytes.
 * @div: 1000 (KB/MB/GB) o 1024 (KiB/MiB/GiB).
 */
static void seq_mem_full(struct seq_file *m, const char *label,
			 unsigned long long bytes, unsigned long div)
{
	unsigned long long v = bytes / div;      /* K/Ki                */
	unsigned long long frac = (bytes % div) * 100 / div;
	const char *u1 = (div == 1024) ? "KiB" : "KB";
	const char *u2 = (div == 1024) ? "MiB" : "MB";
	const char *u3 = (div == 1024) ? "GiB" : "GB";

	if (v >= div * div)
		seq_printf(m, "%-22s %llu.%02llu %s\n", label,
			   v / (div * div), (v % (div * div)) * 100 / (div * div), u3);
	else if (v >= div)
		seq_printf(m, "%-22s %llu.%02llu %s\n", label,
			   v / div, (v % div) * 100 / div, u2);
	else
		seq_printf(m, "%-22s %llu.%02llu %s\n", label, v, frac, u1);
}

/* ------------------------------------------------------------------ */
/* Contenido de /proc/sysstats (estadísticas del sistema)             */
/* ------------------------------------------------------------------ */

/**
 * sysstats_show - Escribe el informe de estadísticas en /proc/sysstats.
 * @m: seq_file asociado a la lectura actual.
 * @v: Iterador de seq_file (no se usa; single_open entrega una sola posición).
 *
 * Return: 0 siempre.
 */
static int sysstats_show(struct seq_file *m, void *v)
{
	struct sysinfo si = { 0 };
	unsigned long loads[3];
	unsigned long div = mem_div();
	u64 jif = get_jiffies_64();
	u64 ns = ktime_get_boottime_ns();
	u64 age_ns;

	si_meminfo(&si);

	/* avenrun[] está exportada (EXPORT_SYMBOL); get_avenrun() no  */
	/* lo está en 6.8 — dato de investigación para la tabla.       */
	loads[0] = READ_ONCE(avenrun[0]);
	loads[1] = READ_ONCE(avenrun[1]);
	loads[2] = READ_ONCE(avenrun[2]);

	seq_printf(m, "Linux mini-stats  (/proc/%s)\n", PROC_NAME);
	seq_puts(m,  "----------------------------------------\n");

	/* --- Tiempo: jiffies crudo (offset INITIAL_JIFFIES) y uptime --- */
	/* --- con el mismo reloj CLOCK_BOOTTIME usado por /proc/uptime --- */
	seq_printf(m, "jiffies (raw)       : %llu ticks (HZ = %u)\n",
		   (unsigned long long)jif, HZ);
	seq_printf(m, "jiffies since boot  : %llu ticks = jiffies_64 - INITIAL_JIFFIES\n",
		   (unsigned long long)(jif - INITIAL_JIFFIES));
	seq_printf(m, "uptime              : %llu.%02llu s (ktime_get_boottime_ns)\n",
		   (unsigned long long)(ns / 1000000000),
		   (unsigned long long)((ns % 1000000000) / 10000000));

	/* --- Load average (array exportado) --- */
	seq_printf(m, "load average        : %lu.%02lu %lu.%02lu %lu.%02lu (1/5/15 min)\n",
		   LOAD_INT(loads[0]), LOAD_FRAC(loads[0]),
		   LOAD_INT(loads[1]), LOAD_FRAC(loads[1]),
		   LOAD_INT(loads[2]), LOAD_FRAC(loads[2]));

	/* --- Memoria (struct sysinfo + si_meminfo) --- */
	seq_printf(m, "memory unit         : %s\n",
		   (div == 1024) ? "IEC (1024)" : "SI (1000)");
	seq_mem_full(m, "mem total", (u64)si.totalram * si.mem_unit, div);
	seq_mem_full(m, "mem free", (u64)si.freeram * si.mem_unit, div);
	seq_mem_full(m, "mem buffers", (u64)si.bufferram * si.mem_unit, div);

	/* --- CPUs en línea (num_online_cpus()) --- */
	seq_printf(m, "cpus online         : %u\n", num_online_cpus());

	/* --- Datos del módulo (tiempo desde su carga) --- */
	age_ns = ns >= mod_start_ns ? ns - mod_start_ns : 0;
	seq_printf(m, "module loaded since : %llu.%02llu s ago\n",
		   (unsigned long long)(age_ns / 1000000000),
		   (unsigned long long)((age_ns % 1000000000) / 10000000));

	return 0;
}

/**
 * sysstats_open - Abre /proc/sysstats con la API seq_file.
 * @inode: Inodo de /proc/sysstats (lo exige proc_ops; no se usa aquí).
 * @file: Archivo de la lectura que se asocia a sysstats_show.
 *
 * Return: 0 si single_open tuvo éxito; código de error del kernel si no.
 */
static int sysstats_open(struct inode *inode, struct file *file)
{
	return single_open(file, sysstats_show, NULL);
}

static const struct proc_ops sysstats_proc_fops = {
	.proc_open    = sysstats_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/* Init / exit                                                        */
/* ------------------------------------------------------------------ */
/**
 * sysstats_init - Punto de entrada: registra /proc/sysstats.
 *
 * Return: 0 si el archivo /proc se creó; -ENOMEM si proc_create falló.
 */
static int __init sysstats_init(void)
{
	mod_start_ns = ktime_get_boottime_ns();

	proc_entry = proc_create(PROC_NAME, 0444, NULL, &sysstats_proc_fops);
	if (!proc_entry) {
		pr_err("sysstats: no se pudo crear /proc/%s\n", PROC_NAME);
		return -ENOMEM;
	}

	pr_info("sysstats: cargado. Leer estadísticas con: cat /proc/%s\n",
		PROC_NAME);
	return 0;
}

/**
 * sysstats_exit - Punto de salida: elimina /proc/sysstats.
 */
static void __exit sysstats_exit(void)
{
	proc_remove(proc_entry);
	pr_info("sysstats: descargado. Adiós.\n");
}

module_init(sysstats_init);
module_exit(sysstats_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Equipo SO");
MODULE_DESCRIPTION("Mini-estadísticas del sistema vía /proc (proyecto OSC cap. 2)");
MODULE_VERSION("1.0");
