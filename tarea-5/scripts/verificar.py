#!/usr/bin/env python3
"""
Verificación cruzada del simulador en C contra la implementación independiente
en Python (scripts/referencia.py).

Para cada una de 30 trazas aleatorias, con 1..8 marcos, se comparan los fallos
de los cinco algoritmos.  En Second Chance también se comparan los bits
limpiados y los marcos examinados.  Cualquier diferencia hace que el script
termine con código 1.
"""

from __future__ import annotations

import csv
import io
import random
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import referencia as ref  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "simulador"

TRIALS = 30
MAX_FRAMES = 8


def check_input() -> None:
    """Regresiones del lector: sintaxis, límites y líneas largas."""
    valid = [
        ("1, 2\t3 # comentario\n2", [1, 2, 3, 2]),
        (" " * 8190 + "123", [123]),
        ("#" + "x" * 8190 + "99\n1\n", [1]),
        ("0,4095\n", [0, 4095]),
        ("1 " * 200000, [1] * 200000),
    ]
    for text, expected in valid:
        proc = subprocess.run(
            [str(BIN), "-a", "second", "-T", "-"],
            input=text, text=True, capture_output=True, check=True,
        )
        actual = [int(row["pagina"]) for row in
                  csv.DictReader(io.StringIO(proc.stdout))]
        if actual != expected:
            raise AssertionError("El lector alteró la cadena de referencias")
    for text in ("1.5 2", "1 basura 2", "-1", "4096", "9" * 100,
                 "1 " * 200001, "", "# solo comentario"):
        proc = subprocess.run(
            [str(BIN), "-a", "second", "-c", "-"],
            input=text, text=True, capture_output=True,
        )
        if proc.returncode == 0 or not proc.stderr:
            raise AssertionError("El lector aceptó una entrada inválida")
    print("Lector de referencias: 13 casos, 0 fallos.")


def run_c(refs: list[int], algo: str, frames: int) -> dict:
    text = " ".join(str(x) for x in refs) + "\n"
    out = subprocess.run(
        [str(BIN), "-a", algo, "-m", str(frames), "-c", "-"],
        input=text, capture_output=True, text=True, check=True,
    ).stdout
    return next(csv.DictReader(io.StringIO(out)))


def main() -> int:
    check_input()
    rnd = random.Random(7)
    comparisons = 0
    errors = 0

    for trial in range(TRIALS):
        n = rnd.randint(50, 300)
        pages = rnd.randint(5, 20)
        refs = [rnd.randrange(pages) for _ in range(n)]

        for frames in range(1, MAX_FRAMES + 1):
            expected = {
                "fifo": ref.fifo(refs, frames),
                "lru": ref.lru(refs, frames),
                "opt": ref.opt(refs, frames),
                "aging": ref.aging(refs, frames),
                "second": ref.second_chance(refs, frames)[0],
            }
            for algo, exp in expected.items():
                row = run_c(refs, algo, frames)
                got = int(row["fallos"])
                comparisons += 1
                if got != exp:
                    print(f"FALLO: traza {trial}, {frames} marcos, {algo}: "
                          f"Python={exp}, C={got}")
                    errors += 1

            sc_exp = ref.second_chance(refs, frames)
            row = run_c(refs, "second", frames)
            got = (int(row["fallos"]), int(row["bits_limpiados"]),
                   int(row["marcos_examinados"]))
            comparisons += 1
            if got != sc_exp:
                print(f"FALLO: traza {trial}, {frames} marcos, Second Chance "
                      f"detalle: Python={sc_exp}, C={got}")
                errors += 1

    print(f"Verificación cruzada: {comparisons} comparaciones, "
          f"{errors} diferencias.")
    return 0 if errors == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
