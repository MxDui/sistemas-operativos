#!/usr/bin/env bash
# Ejecuta la suite de validación y genera los resultados de texto/CSV.
# Las gráficas de results/ ya están versionadas; no se regeneran aquí.
set -euo pipefail
cd "$(dirname "$0")"

make -s

echo "== self-test =="
mkdir -p results
./simulador --self-test | tee results/self_test.txt

echo
echo "== resumen a 4 marcos =="
for t in clasica belady ciclica localidad mixta; do
    ./simulador -m 4 "traces/$t.txt" > "results/resumen_$t.txt"
    echo "  results/resumen_$t.txt"
done

echo
echo "== trazas paso a paso (clásica, 4 marcos) =="
./simulador -a second -m 4 -t traces/clasica.txt > results/traza_second_clasica.txt
./simulador -a aging  -m 4 -t traces/clasica.txt > results/traza_aging_clasica.txt
echo "  results/traza_second_clasica.txt"
echo "  results/traza_aging_clasica.txt"

echo
echo "== CSV de resumen =="
for t in clasica belady ciclica localidad mixta; do
    ./simulador -m 4 -c "traces/$t.txt" > "results/resumen_$t.csv"
done
echo "  results/resumen_*.csv"

if command -v python3 >/dev/null 2>&1; then
    echo
    echo "== verificación cruzada (Python) =="
    python3 scripts/verificar.py | tee results/verificacion_cruzada.txt
fi

echo
echo "Todo OK."
