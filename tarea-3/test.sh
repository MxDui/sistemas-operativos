#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

make -s

echo "== self-test =="
./simulador --self-test

echo
echo "== Procesos.txt (resumen) =="
mkdir -p results
./simulador Procesos.txt | tee results/resumen.txt

echo
echo "== Procesos.txt (tablas completas) =="
./simulador -f Procesos.txt > results/simulacion.txt
echo "Escrito results/simulacion.txt"

echo
echo "Todo OK."
