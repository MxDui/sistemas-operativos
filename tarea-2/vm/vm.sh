#!/usr/bin/env bash
# VM reproducible para compilar y probar sysstats sobre Ubuntu 24.04.
set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd)

IMAGE_SERIAL="20260826"
IMAGE_NAME="noble-server-cloudimg-amd64.img"
IMAGE_URL="https://cloud-images.ubuntu.com/noble/${IMAGE_SERIAL}/${IMAGE_NAME}"
IMAGE_SHA256="d0fe84bb5f80853425fa6be28e2c106f30104c3cfe8611933f2e65c9b63f0e30"
EXPECTED_KERNEL="6.8.0-138-generic"

BASE_IMAGE="$SCRIPT_DIR/$IMAGE_NAME"
DISK_IMAGE="$SCRIPT_DIR/disk.qcow2"
SEED_IMAGE="$SCRIPT_DIR/seed.iso"
PRIVATE_KEY="$SCRIPT_DIR/id_ed25519"
PUBLIC_KEY="$PRIVATE_KEY.pub"
USER_DATA="$SCRIPT_DIR/user-data"
META_DATA="$SCRIPT_DIR/meta-data"
PID_FILE="$SCRIPT_DIR/qemu.pid"
CONSOLE_LOG="$SCRIPT_DIR/console.log"
KNOWN_HOSTS="$SCRIPT_DIR/known_hosts"
SSH_PORT="${SYSSTATS_SSH_PORT:-2222}"
MEMORY_MB="${SYSSTATS_VM_MEMORY_MB:-2048}"
CPUS="${SYSSTATS_VM_CPUS:-2}"
CONFIG_FILE="$SCRIPT_DIR/runtime.conf"

config_read()
{
    local name value
    local -a pairs=()

    [[ -f "$CONFIG_FILE" && ! -L "$CONFIG_FILE" ]] || return 0
    while IFS='=' read -r name value; do
        case $name in
            ''|'#'*)
                continue
                ;;
            SYSSTATS_SSH_PORT|SYSSTATS_VM_MEMORY_MB|SYSSTATS_VM_CPUS)
                pairs+=("$name=$value")
                ;;
            *)
                printf 'Configuración con entrada desconocida: %s\n' "$name" >&2
                exit 2
                ;;
        esac
    done <"$CONFIG_FILE"
    for pair in "${pairs[@]}"; do
        name=${pair%%=*}
        value=${pair#*=}
        case $name in
            SYSSTATS_SSH_PORT)
                SSH_PORT=$value
                ;;
            SYSSTATS_VM_MEMORY_MB)
                MEMORY_MB=$value
                ;;
            SYSSTATS_VM_CPUS)
                CPUS=$value
                ;;
        esac
    done
    validate_integer_range SYSSTATS_SSH_PORT "$SSH_PORT" 1024 65535
    validate_integer_range SYSSTATS_VM_MEMORY_MB "$MEMORY_MB" 512 65536
    validate_integer_range SYSSTATS_VM_CPUS "$CPUS" 1 64
}

config_write()
{
    local tmp

    tmp=$(mktemp "$CONFIG_FILE.tmp.XXXXXX") || {
        printf 'No se pudo crear el archivo temporal de configuración.\n' >&2
        exit 1
    }
    umask 077
    printf 'SYSSTATS_SSH_PORT=%s\nSYSSTATS_VM_MEMORY_MB=%s\nSYSSTATS_VM_CPUS=%s\n' \
        "$SSH_PORT" "$MEMORY_MB" "$CPUS" >"$tmp" || {
        rm -f -- "$tmp"
        printf 'No se pudo escribir la configuración temporal.\n' >&2
        exit 1
    }
    mv -f -- "$tmp" "$CONFIG_FILE"
}

usage()
{
    cat <<'EOF'
Uso: vm/vm.sh COMANDO

  setup           Crea la VM; se niega a reutilizar un disco existente
  setup --fresh   Recrea explícitamente el disco y los datos generados
  start           Inicia QEMU y espera a que cloud-init termine
  ssh             Abre una sesión SSH en la VM
  test            Compila con W=1 y ejecuta test.sh dentro de la VM
  status          Muestra el estado de QEMU
  stop            Apaga la VM

Variables opcionales: SYSSTATS_SSH_PORT, SYSSTATS_VM_MEMORY_MB,
SYSSTATS_VM_CPUS. Si existe runtime.conf los valores, ya validados,
se cargan y tienen prioridad.
EOF
}

require_command()
{
    command -v "$1" >/dev/null 2>&1 || {
        printf 'Falta el comando requerido: %s\n' "$1" >&2
        exit 2
    }
}

validate_integer_range()
{
    local name=$1 value=$2 minimum=$3 maximum=$4
    local decimal

    if [[ ! $value =~ ^[1-9][0-9]*$ || ${#value} -gt ${#maximum} ]]; then
        printf '%s debe ser un entero decimal entre %s y %s: %s\n' \
            "$name" "$minimum" "$maximum" "$value" >&2
        exit 2
    fi
    decimal=$((10#$value))
    if (( decimal < minimum || decimal > maximum )); then
        printf '%s debe estar entre %s y %s: %s\n' \
            "$name" "$minimum" "$maximum" "$value" >&2
        exit 2
    fi
}

validate_ssh_port()
{
    validate_integer_range SYSSTATS_SSH_PORT "$SSH_PORT" 1024 65535
}

validate_vm_settings()
{
    validate_ssh_port
    validate_integer_range SYSSTATS_VM_MEMORY_MB "$MEMORY_MB" 512 65536
    validate_integer_range SYSSTATS_VM_CPUS "$CPUS" 1 64
}

qemu_pid()
{
    [[ -f "$PID_FILE" && ! -L "$PID_FILE" && -r "$PID_FILE" ]] || return 1
    local pid executable arg
    local found_disk=0

    pid=$(<"$PID_FILE")
    [[ $pid =~ ^[0-9]+$ ]] || return 1
    kill -0 "$pid" 2>/dev/null || return 1
    [[ -r "/proc/$pid/cmdline" ]] || return 1
    IFS= read -r -d '' executable <"/proc/$pid/cmdline" || return 1
    [[ ${executable##*/} == qemu-system-x86_64 ]] || return 1
    while IFS= read -r -d '' arg; do
        if [[ $arg == "file=$DISK_IMAGE,"* ]]; then
            found_disk=1
        fi
    done <"/proc/$pid/cmdline"
    (( found_disk == 1 )) || return 1
    printf '%s\n' "$pid"
}

ssh_options()
{
    printf '%s\n' \
        -i "$PRIVATE_KEY" \
        -p "$SSH_PORT" \
        -o BatchMode=yes \
        -o IdentitiesOnly=yes \
        -o ConnectTimeout=5 \
        -o StrictHostKeyChecking=accept-new \
        -o UserKnownHostsFile="$KNOWN_HOSTS"
}

run_ssh()
{
    local -a options=()
    mapfile -t options < <(ssh_options)
    ssh "${options[@]}" ubuntu@127.0.0.1 "$@"
}

download_image()
{
    local partial="$BASE_IMAGE.part"

    if [[ -f "$BASE_IMAGE" ]]; then
        if printf '%s  %s\n' "$IMAGE_SHA256" "$BASE_IMAGE" | sha256sum --check --status; then
            printf 'Imagen base verificada: %s\n' "$BASE_IMAGE"
            return
        fi
        printf 'La imagen existente no coincide con SHA-256; elimínela: %s\n' "$BASE_IMAGE" >&2
        exit 1
    fi

    rm -f -- "$partial"
    printf 'Descargando imagen Ubuntu fijada (%s)...\n' "$IMAGE_SERIAL"
    if command -v curl >/dev/null 2>&1; then
        curl --fail --location --retry 3 --output "$partial" "$IMAGE_URL"
    elif command -v wget >/dev/null 2>&1; then
        wget --output-document="$partial" "$IMAGE_URL"
    else
        printf 'Se requiere curl o wget para descargar la imagen.\n' >&2
        exit 2
    fi
    printf '%s  %s\n' "$IMAGE_SHA256" "$partial" | sha256sum --check --status || {
        rm -f -- "$partial"
        printf 'Falló la verificación SHA-256 de la imagen descargada.\n' >&2
        exit 1
    }
    mv -- "$partial" "$BASE_IMAGE"
}

ensure_ssh_key()
{
    local derived_public public_material

    if [[ -e "$PRIVATE_KEY" && (! -f "$PRIVATE_KEY" || -L "$PRIVATE_KEY") ]]; then
        printf 'La ruta de llave privada no es un archivo regular seguro: %s\n' \
            "$PRIVATE_KEY" >&2
        exit 1
    fi
    if [[ ! -f "$PRIVATE_KEY" ]]; then
        rm -f -- "$PUBLIC_KEY"
        ssh-keygen -q -t ed25519 -N '' -C 'sysstats-vm' -f "$PRIVATE_KEY"
        printf 'Llave SSH dedicada creada en %s\n' "$PRIVATE_KEY"
    fi

    chmod 600 "$PRIVATE_KEY"
    if ! public_material=$(ssh-keygen -y -P '' -f "$PRIVATE_KEY"); then
        printf 'La llave SSH dedicada no es válida o requiere contraseña: %s\n' \
            "$PRIVATE_KEY" >&2
        exit 1
    fi

    derived_public=$(mktemp "$PUBLIC_KEY.tmp.XXXXXX") || {
        printf 'No se pudo crear el archivo temporal para la llave pública.\n' >&2
        exit 1
    }
    if ! printf '%s sysstats-vm\n' "$public_material" >"$derived_public"; then
        rm -f -- "$derived_public"
        printf 'No se pudo escribir la llave pública temporal.\n' >&2
        exit 1
    fi
    chmod 644 "$derived_public"
    mv -f -- "$derived_public" "$PUBLIC_KEY"
}

write_cloud_init()
{
    local authorized_key
    authorized_key=$(<"$PUBLIC_KEY")

    cat >"$META_DATA" <<EOF
instance-id: sysstats-${IMAGE_SERIAL}
local-hostname: sysstats-vm
EOF

    cat >"$USER_DATA" <<EOF
#cloud-config
hostname: sysstats-vm
manage_etc_hosts: true
users:
  - name: ubuntu
    gecos: Ubuntu
    groups: [adm, sudo]
    sudo: ["ALL=(ALL) NOPASSWD:ALL"]
    shell: /bin/bash
    lock_passwd: true
    ssh_authorized_keys:
      - ${authorized_key}
ssh_pwauth: false
disable_root: true
package_update: true
packages:
  - build-essential
  - linux-headers-${EXPECTED_KERNEL}
bootcmd:
  - [mkdir, -p, /mnt/host]
mounts:
  - [host0, /mnt/host, 9p, "trans=virtio,version=9p2000.L,ro,nofail", "0", "0"]
final_message: "sysstats VM lista"
EOF

    genisoimage -quiet -output "$SEED_IMAGE" -volid cidata \
        -joliet -rock "$USER_DATA" "$META_DATA"
}

setup_vm()
{
    local mode=${1:-}
    if [[ -n "$mode" && $mode != "--fresh" ]]; then
        printf 'Opción de setup desconocida: %s\n' "$mode" >&2
        exit 2
    fi

    require_command sha256sum
    require_command ssh-keygen
    require_command mktemp
    require_command genisoimage
    require_command qemu-img

    local pid
    if pid=$(qemu_pid); then
        printf 'La VM está activa (PID %s); deténgala antes de ejecutar setup.\n' "$pid" >&2
        exit 2
    fi
    rm -f -- "$PID_FILE"

    if [[ -e "$DISK_IMAGE" && $mode != "--fresh" ]]; then
        printf 'Ya existe %s. Use start para conservarlo o setup --fresh para recrearlo.\n' \
            "$DISK_IMAGE" >&2
        exit 2
    fi
    if [[ $mode == "--fresh" ]]; then
        printf 'Recreando el estado mutable; se conserva la llave SSH dedicada.\n'
    fi
    # Todo disco nuevo tendrá otra llave de host: no reutilizar su entrada SSH.
    rm -f -- "$DISK_IMAGE" "$SEED_IMAGE" "$USER_DATA" "$META_DATA" \
        "$KNOWN_HOSTS" "$CONSOLE_LOG"

    download_image
    ensure_ssh_key
    write_cloud_init

    qemu-img create -q -f qcow2 -F qcow2 -b "$BASE_IMAGE" "$DISK_IMAGE" 12G
    qemu-img info --backing-chain "$DISK_IMAGE" >/dev/null
    qemu-img check -q "$DISK_IMAGE"
    printf 'Disco de trabajo creado: %s\n' "$DISK_IMAGE"
    config_write
    printf 'Setup completo. Siguiente paso: ./vm/vm.sh start\n'
}

start_vm()
{
    require_command qemu-system-x86_64
    require_command ssh

    local pid
    if pid=$(qemu_pid); then
        printf 'La VM ya está activa (PID %s).\n' "$pid"
    else
        rm -f -- "$PID_FILE"
        for path in "$DISK_IMAGE" "$SEED_IMAGE" "$PRIVATE_KEY"; do
            [[ -e "$path" ]] || {
                printf 'Falta %s; ejecute ./vm/vm.sh setup.\n' "$path" >&2
                exit 2
            }
        done

        local -a accel=(-machine q35,accel=tcg -cpu max)
        if [[ -r /dev/kvm && -w /dev/kvm ]]; then
            accel=(-machine q35,accel=kvm -cpu host)
        fi
        rm -f -- "$CONSOLE_LOG"
        : >"$CONSOLE_LOG"
        qemu-system-x86_64 \
            "${accel[@]}" \
            -m "$MEMORY_MB" -smp "$CPUS" \
            -drive "file=$DISK_IMAGE,format=qcow2,if=virtio" \
            -drive "file=$SEED_IMAGE,format=raw,if=virtio,media=cdrom,readonly=on" \
            -virtfs "local,path=$ROOT_DIR,mount_tag=host0,security_model=none,readonly=on" \
            -nic "user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:${SSH_PORT}-:22" \
            -display none -serial "file:$CONSOLE_LOG" \
            -daemonize -pidfile "$PID_FILE"
        printf 'VM iniciada (PID %s); esperando SSH...\n' "$(<"$PID_FILE")"
    fi

    local ready=0
    local attempt
    for ((attempt = 1; attempt <= 120; attempt++)); do
        if run_ssh true >/dev/null 2>&1; then
            ready=1
            break
        fi
        sleep 2
    done
    if (( ready == 0 )); then
        printf 'SSH no respondió. Revise %s\n' "$CONSOLE_LOG" >&2
        exit 1
    fi

    printf 'Esperando a que cloud-init instale las dependencias...\n'
    run_ssh 'cloud-init status --wait'
    local kernel
    kernel=$(run_ssh 'uname -r')
    if [[ $kernel != "$EXPECTED_KERNEL" ]]; then
        printf 'Kernel inesperado: %s (se esperaba %s).\n' "$kernel" "$EXPECTED_KERNEL" >&2
        exit 1
    fi
    printf 'VM lista: Ubuntu, kernel %s, SSH 127.0.0.1:%s\n' "$kernel" "$SSH_PORT"
}

stop_vm()
{
    local pid
    if ! pid=$(qemu_pid); then
        rm -f -- "$PID_FILE"
        printf 'La VM no está activa.\n'
        return
    fi

    printf 'Apagando la VM (PID %s)...\n' "$pid"
    # SSH suele terminar con 255 cuando poweroff corta la conexión; eso no es
    # motivo para matar QEMU antes de que el huésped complete el apagado.
    run_ssh 'sudo -n poweroff' >/dev/null 2>&1 || true

    local attempt current
    for ((attempt = 1; attempt <= 30; attempt++)); do
        if ! current=$(qemu_pid) || [[ $current != "$pid" ]]; then
            rm -f -- "$PID_FILE"
            printf 'VM apagada.\n'
            return
        fi
        sleep 1
    done

    printf 'El apagado invitado agotó el tiempo; enviando SIGTERM a QEMU.\n' >&2
    if current=$(qemu_pid) && [[ $current == "$pid" ]]; then
        kill -TERM "$pid"
    fi
    for ((attempt = 1; attempt <= 10; attempt++)); do
        if ! current=$(qemu_pid) || [[ $current != "$pid" ]]; then
            rm -f -- "$PID_FILE"
            printf 'VM detenida.\n'
            return
        fi
        sleep 1
    done
    printf 'QEMU no se detuvo; PID %s sigue activo.\n' "$pid" >&2
    exit 1
}

reject_extra_args()
{
    local command_name=$1
    shift
    if (( $# != 0 )); then
        printf 'El comando %s no acepta argumentos adicionales.\n' "$command_name" >&2
        exit 2
    fi
}

command=${1:-}
case "$command" in
    start|test)
        config_read
        validate_vm_settings
        ;;
    ssh|status|stop)
        config_read
        validate_ssh_port
        ;;
    setup)
        validate_ssh_port
        ;;
esac

case "$command" in
    setup)
        shift
        if (( $# > 1 )); then
            printf 'setup acepta únicamente la opción --fresh.\n' >&2
            exit 2
        fi
        setup_vm "${1:-}"
        ;;
    start)
        shift
        reject_extra_args start "$@"
        start_vm
        ;;
    ssh)
        shift
        require_command ssh
        run_ssh "$@"
        ;;
    test)
        shift
        reject_extra_args test "$@"
        start_vm
        run_ssh 'set -eu
workdir=$HOME/sysstats-test
rm -rf -- "$workdir"
mkdir -m 700 -- "$workdir"
cp -- /mnt/host/Makefile /mnt/host/sysstats.c /mnt/host/test.sh "$workdir"/
cd -- "$workdir"
make clean all W=1
./test.sh'
        ;;
    status)
        shift
        reject_extra_args status "$@"
        if pid=$(qemu_pid); then
            printf 'activa (PID %s, SSH 127.0.0.1:%s)\n' "$pid" "$SSH_PORT"
        else
            printf 'detenida\n'
        fi
        ;;
    stop)
        shift
        reject_extra_args stop "$@"
        stop_vm
        ;;
    -h|--help|help|'')
        usage
        ;;
    *)
        printf 'Comando desconocido: %s\n' "$command" >&2
        usage >&2
        exit 2
        ;;
esac
