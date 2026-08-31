#!/usr/bin/env bash
# Pruebas funcionales de sysstats. Deben ejecutarse en una VM de prueba:
# el módulo es código privilegiado y un fallo puede afectar al kernel.
set -uo pipefail

MOD="sysstats"
ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
KO="$ROOT_DIR/$MOD.ko"
PASS=0
FAIL=0
TMP_DIR=""
CLEANUP_MODULE=0
EXPECTED_HZ=1000
DMESG_MARKER="sysstats-test[$$.$RANDOM.$SECONDS]: inicio"

if (( EUID == 0 )); then
    SUDO=()
else
    SUDO=(sudo)
fi

ok()  { printf '  [PASS] %s\n' "$1"; PASS=$((PASS + 1)); }
bad() { printf '  [FAIL] %s\n' "$1"; FAIL=$((FAIL + 1)); }

cleanup()
{
    local rc=$?

    trap - EXIT INT TERM
    if (( CLEANUP_MODULE )) && [[ -d "/sys/module/$MOD" ]]; then
        if ! "${SUDO[@]}" rmmod "$MOD"; then
            printf '  [FAIL] limpieza: no se pudo descargar %s\n' "$MOD" >&2
            rc=1
        fi
    fi
    if [[ -n "$TMP_DIR" && -d "$TMP_DIR" ]]; then
        rm -f -- "$TMP_DIR"/*
        rmdir -- "$TMP_DIR"
    fi
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

load_module()
{
    "${SUDO[@]}" insmod "$KO" "$@"
}

unload_module()
{
    "${SUDO[@]}" rmmod "$MOD"
}

report_bytes()
{
    local label=$1
    local report=$2

    awk -v wanted="$label" '
        $1 " " $2 == wanted {
            factor = 0
            if ($4 == "B")   factor = 1
            if ($4 == "KB")  factor = 1000
            if ($4 == "MB")  factor = 1000000
            if ($4 == "GB")  factor = 1000000000
            if ($4 == "KiB") factor = 1024
            if ($4 == "MiB") factor = 1048576
            if ($4 == "GiB") factor = 1073741824
            if (factor == 0 || $3 !~ /^[0-9]+([.][0-9]+)?$/)
                exit 2
            printf "%.0f\n", $3 * factor
            found = 1
            exit
        }
        END { if (!found) exit 1 }
    ' "$report"
}

close_enough()
{
    local reference=$1
    local observed=$2
    local fraction=$3

    awk -v a="$reference" -v b="$observed" -v f="$fraction" 'BEGIN {
        if (a <= 0 || b < 0) exit 1
        d = a - b
        if (d < 0) d = -d
        exit !(d / a <= f)
    }'
}

report_units_are_consistent()
{
    local report=$1

    awk '
        /^memory unit[[:space:]]+:/ {
            if ($4 == "SI" && $5 == "(1000)") expected = "^(KB|MB|GB)$"
            else if ($4 == "IEC" && $5 == "(1024)") expected = "^(KiB|MiB|GiB)$"
            else exit 1
        }
        $1 == "mem" && ($2 == "total" || $2 == "free" || $2 == "buffers") {
            if ($3 !~ /^[0-9]+([.][0-9]+)?$/) exit 1
            units[++seen] = $4
        }
        END {
            if (expected == "" || seen != 3) exit 1
            for (i = 1; i <= seen; i++)
                if (units[i] !~ expected) exit 1
        }
    ' "$report"
}

required_commands=(awk grep mktemp cat dmesg tee insmod rmmod getconf rm rmdir)
for command in "${required_commands[@]}"; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'Falta el comando requerido: %s\n' "$command" >&2
        exit 2
    fi
done
TMP_DIR=$(mktemp -d) || {
    printf 'No se pudo crear el directorio temporal.\n' >&2
    exit 2
}
if (( EUID != 0 )); then
    if ! command -v sudo >/dev/null 2>&1 || ! sudo -n true; then
        printf 'Se requiere sudo no interactivo (NOPASSWD) para cargar el módulo.\n' >&2
        exit 2
    fi
fi
if [[ ! -r "$KO" ]]; then
    printf 'No existe %s; ejecute make primero.\n' "$KO" >&2
    exit 2
fi
if [[ -d "/sys/module/$MOD" ]]; then
    printf '%s ya está cargado; descárguelo antes de ejecutar la suite.\n' "$MOD" >&2
    exit 2
fi
CLEANUP_MODULE=1

if ! printf '<6>%s\n' "$DMESG_MARKER" | \
        "${SUDO[@]}" tee /dev/kmsg >/dev/null; then
    printf 'No se pudo delimitar la ejecución en /dev/kmsg.\n' >&2
    exit 2
fi

echo '== 1. Carga y contrato de /proc =='
if ! load_module; then
    bad 'insmod no aceptó la configuración predeterminada'
    exit 1
fi
ok 'insmod acepta la configuración predeterminada'
if [[ -r "/proc/$MOD" ]]; then
    ok "/proc/$MOD existe y es legible"
else
    bad "/proc/$MOD no existe o no es legible"
    exit 1
fi
if cat "/proc/$MOD" >"$TMP_DIR/report.si"; then
    ok 'el informe se puede leer completo'
else
    bad 'falló la lectura del informe'
    exit 1
fi

required_patterns=(
    '^jiffies \(raw\)[[:space:]]+:'
    '^jiffies since boot[[:space:]]+:'
    '^uptime[[:space:]]+:'
    '^load average[[:space:]]+:'
    '^memory unit[[:space:]]+:'
    '^mem total[[:space:]]+'
    '^mem free[[:space:]]+'
    '^mem buffers[[:space:]]+'
    '^cpus online[[:space:]]+:'
    '^module loaded since[[:space:]]+:'
)
missing=0
for pattern in "${required_patterns[@]}"; do
    grep -Eq "$pattern" "$TMP_DIR/report.si" || missing=1
done
if (( missing == 0 )); then
    ok 'el informe contiene todos los campos documentados'
else
    bad 'faltan campos documentados en el informe'
fi

echo '== 2. Comparaciones semánticas =='
read -r HOST_UPTIME < <(awk '{print $1}' /proc/uptime)
MOD_UPTIME=$(awk '$1 == "uptime" {print $3}' "$TMP_DIR/report.si")
if awk -v a="$HOST_UPTIME" -v b="$MOD_UPTIME" 'BEGIN { d=a-b; if (d<0) d=-d; exit !(d<=1.0) }'; then
    ok "uptime coincide con /proc/uptime (tolerancia simétrica de 1 s)"
else
    bad "uptime no coincide: /proc=$HOST_UPTIME, módulo=$MOD_UPTIME"
fi

JBOOT=$(awk '$1 == "jiffies" && $2 == "since" {print $5}' "$TMP_DIR/report.si")
HZ=$(awk '$1 == "jiffies" && $2 == "(raw)" { for (i=1; i<=NF; i++) if ($i == "=") { gsub(/[^0-9]/, "", $(i+1)); print $(i+1) } }' "$TMP_DIR/report.si")
if [[ $JBOOT =~ ^[0-9]+$ && $HZ =~ ^[0-9]+$ ]] && \
   awk -v j="$JBOOT" -v hz="$HZ" -v up="$MOD_UPTIME" 'BEGIN { d=j/hz-up; if (d<0) d=-d; exit !(hz>0 && d<=1.0) }'; then
    ok 'jiffies since boot / HZ coincide con el uptime'
else
    bad 'jiffies corregidos no coinciden con el uptime'
fi
if [[ $HZ == "$EXPECTED_HZ" ]]; then
    ok "HZ reportado es $EXPECTED_HZ"
else
    bad "HZ reportado no es $EXPECTED_HZ: $HZ"
fi

MEM_REFERENCE=$(awk '/^MemTotal:/ {printf "%.0f", $2 * 1024}' /proc/meminfo)
if MEM_SI=$(report_bytes 'mem total' "$TMP_DIR/report.si") && \
   close_enough "$MEM_REFERENCE" "$MEM_SI" 0.01; then
    ok 'mem total SI coincide con /proc/meminfo (<=1 %)'
else
    bad "mem total SI no coincide: referencia=$MEM_REFERENCE, módulo=${MEM_SI:-inválido}"
fi

CPU_REFERENCE=$(getconf _NPROCESSORS_ONLN)
CPU_MODULE=$(awk '$1 == "cpus" && $2 == "online" {print $4}' "$TMP_DIR/report.si")
if [[ $CPU_MODULE == "$CPU_REFERENCE" ]]; then
    ok 'cpus online coincide con sysconf(_SC_NPROCESSORS_ONLN)'
else
    bad "cpus online no coincide: referencia=$CPU_REFERENCE, módulo=$CPU_MODULE"
fi

LOAD_REFERENCE=$(awk '{print $1}' /proc/loadavg)
LOAD_MODULE=$(awk '$1 == "load" && $2 == "average" {print $4}' "$TMP_DIR/report.si")
if awk -v a="$LOAD_REFERENCE" -v b="$LOAD_MODULE" 'BEGIN { d=a-b; if (d<0) d=-d; exit !(d<=0.25) }'; then
    ok 'load average de 1 min coincide dentro de 0.25'
else
    bad "load average no coincide: referencia=$LOAD_REFERENCE, módulo=$LOAD_MODULE"
fi

echo '== 3. Parámetro unit y concurrencia =='
if [[ $(<"/sys/module/$MOD/parameters/unit") == si ]] && grep -q 'SI (1000)' "$TMP_DIR/report.si"; then
    ok 'unit inicia en si y sysfs refleja el estado real'
else
    bad 'estado inicial de unit incorrecto'
fi

if printf 'iec\n' | "${SUDO[@]}" tee "/sys/module/$MOD/parameters/unit" >/dev/null && \
   cat "/proc/$MOD" >"$TMP_DIR/report.iec" && \
   grep -q 'IEC (1024)' "$TMP_DIR/report.iec"; then
    ok 'unit acepta iec en caliente, incluso con salto de línea'
else
    bad 'no se pudo activar IEC en caliente'
fi

if MEM_IEC=$(report_bytes 'mem total' "$TMP_DIR/report.iec") && \
   close_enough "$MEM_REFERENCE" "$MEM_IEC" 0.01; then
    ok 'la conversión IEC conserva la cantidad de bytes (<=1 %)'
else
    bad "mem total IEC no coincide: referencia=$MEM_REFERENCE, módulo=${MEM_IEC:-inválido}"
fi

if printf 'basura\n' | "${SUDO[@]}" tee "/sys/module/$MOD/parameters/unit" >/dev/null 2>&1; then
    bad 'unit aceptó un valor inválido'
else
    if [[ $(<"/sys/module/$MOD/parameters/unit") == iec ]]; then
        ok 'unit rechaza valores inválidos sin alterar el estado'
    else
        bad 'el rechazo de unit alteró el estado'
    fi
fi

reader_status=0
writer_status=0
(
    for ((i = 1; i <= 100; i++)); do
        cat "/proc/$MOD" >"$TMP_DIR/concurrent.$i" || exit 1
    done
) &
reader_pid=$!
(
    for ((i = 1; i <= 100; i++)); do
        if (( i % 2 )); then value=si; else value=iec; fi
        printf '%s\n' "$value" | "${SUDO[@]}" tee "/sys/module/$MOD/parameters/unit" >/dev/null || exit 1
    done
) &
writer_pid=$!
wait "$reader_pid" || reader_status=1
wait "$writer_pid" || writer_status=1
concurrent_consistent=1
if (( reader_status == 0 && writer_status == 0 )); then
    for ((i = 1; i <= 100; i++)); do
        report="$TMP_DIR/concurrent.$i"
        if ! bytes=$(report_bytes 'mem total' "$report") || \
           ! close_enough "$MEM_REFERENCE" "$bytes" 0.01 || \
           ! report_units_are_consistent "$report"; then
            concurrent_consistent=0
            break
        fi
    done
else
    concurrent_consistent=0
fi
if (( concurrent_consistent )); then
    ok '100 lecturas y 100 escrituras concurrentes producen informes SI/IEC coherentes'
else
    bad 'falló la prueba concurrente o produjo un informe incoherente'
fi

if printf 'si\n' | "${SUDO[@]}" tee "/sys/module/$MOD/parameters/unit" >/dev/null; then
    ok 'unit vuelve a SI'
else
    bad 'no se pudo restaurar SI'
fi

echo '== 4. Ciclo de vida y parámetros de carga =='
if unload_module && [[ ! -e "/proc/$MOD" ]]; then
    ok 'rmmod elimina /proc/sysstats'
else
    bad 'la descarga no eliminó /proc/sysstats'
    exit 1
fi

if load_module unit=iec && grep -q 'IEC (1024)' "/proc/$MOD"; then
    ok 'unit=iec funciona al cargar'
else
    bad 'unit=iec no funciona al cargar'
fi
unload_module || exit 1

if "${SUDO[@]}" insmod "$KO" unit=basura >/dev/null 2>&1; then
    bad 'insmod aceptó unit=basura'
    unload_module || true
else
    if [[ ! -d "/sys/module/$MOD" ]]; then
        ok 'insmod rechaza unit=basura y no deja el módulo cargado'
    else
        bad 'insmod inválido dejó el módulo cargado'
        unload_module || true
    fi
fi

cycles_ok=1
for _ in 1 2 3; do
    load_module || { cycles_ok=0; break; }
    unload_module || { cycles_ok=0; break; }
done
if (( cycles_ok )); then
    ok 'tres ciclos adicionales de carga/descarga terminan bien'
else
    bad 'falló un ciclo adicional de carga/descarga'
fi

echo '== 5. Diagnóstico del kernel durante esta ejecución =='
"${SUDO[@]}" dmesg >"$TMP_DIR/dmesg.after" || {
    bad 'no se pudo leer dmesg al finalizar'
    exit 1
}
if ! awk -v marker="$DMESG_MARKER" '
    index($0, marker) { found = 1; next }
    found { print }
    END { if (!found) exit 1 }
' "$TMP_DIR/dmesg.after" >"$TMP_DIR/dmesg.new"; then
    bad 'no se pudo recuperar el marcador en dmesg'
    exit 1
fi
if grep -Eqi 'BUG:|WARNING:|Oops:|general protection fault|KASAN:|UBSAN:' "$TMP_DIR/dmesg.new"; then
    bad 'apareció una alerta grave del kernel durante la suite'
    grep -Ei 'BUG:|WARNING:|Oops:|general protection fault|KASAN:|UBSAN:' "$TMP_DIR/dmesg.new" >&2
else
    ok 'no aparecieron BUG/WARNING/Oops/KASAN/UBSAN durante la suite'
fi
if grep -q 'sysstats: cargado' "$TMP_DIR/dmesg.new" && grep -q 'sysstats: descargado' "$TMP_DIR/dmesg.new"; then
    ok 'dmesg contiene mensajes de carga y descarga de sysstats'
else
    bad 'faltan mensajes esperados de sysstats en dmesg'
fi

echo
printf 'Resultado: %d aprobadas, %d fallidas\n' "$PASS" "$FAIL"
if (( FAIL != 0 )); then
    exit 1
fi
printf 'TODAS LAS PRUEBAS PASARON\n'
