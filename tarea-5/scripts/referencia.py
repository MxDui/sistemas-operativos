#!/usr/bin/env python3
"""
Implementación independiente (Python) de los cinco algoritmos de reemplazo.

Se usa solo para validar el motor en C: no forma parte de la simulación que se
presenta.  Las decisiones de diseño son deliberadamente las mismas que las del
simulador (empates por menor índice de marco, contador de Aging de N bits,
etc.) para que las comparaciones sean significativas.
"""

from __future__ import annotations


def fifo(refs: list[int], frames: int) -> int:
    mem: list[int | None] = [None] * frames
    hand = 0
    faults = 0
    for p in refs:
        if p in mem:
            continue
        faults += 1
        mem[hand] = p
        hand = (hand + 1) % frames
    return faults


def lru(refs: list[int], frames: int) -> int:
    mem: list[int | None] = [None] * frames
    last = [0] * frames
    faults = 0
    for t, p in enumerate(refs, 1):
        if p in mem:
            last[mem.index(p)] = t
            continue
        faults += 1
        if None in mem:
            i = mem.index(None)
        else:
            i = min(range(frames), key=lambda j: (last[j], j))
        mem[i] = p
        last[i] = t
    return faults


def opt(refs: list[int], frames: int) -> int:
    mem: list[int | None] = [None] * frames
    faults = 0
    for t, p in enumerate(refs):
        if p in mem:
            continue
        faults += 1
        if None in mem:
            i = mem.index(None)
        else:
            def next_use(j: int) -> int:
                try:
                    return refs.index(mem[j], t + 1)
                except ValueError:
                    return len(refs) + 1
            i = max(range(frames), key=lambda j: (next_use(j), -j))
        mem[i] = p
    return faults


def second_chance(refs: list[int], frames: int) -> tuple[int, int, int]:
    """Devuelve (fallos, bits_limpiados, marcos_examinados)."""
    mem: list[int | None] = [None] * frames
    bits = [0] * frames
    hand = 0
    faults = cleared = scans = 0
    for p in refs:
        if p in mem:
            bits[mem.index(p)] = 1
            continue
        faults += 1
        if None in mem:
            i = mem.index(None)
        else:
            while bits[hand] == 1:
                bits[hand] = 0
                cleared += 1
                scans += 1
                hand = (hand + 1) % frames
            scans += 1
            i = hand
            hand = (hand + 1) % frames
        mem[i] = p
        bits[i] = 1
    return faults, cleared, scans


def aging(refs: list[int], frames: int, bits: int = 8) -> int:
    mem: list[int | None] = [None] * frames
    age = [0] * frames
    msb = 1 << (bits - 1)
    faults = 0
    for p in refs:
        for j in range(frames):
            if mem[j] is not None:
                age[j] >>= 1
        if p in mem:
            age[mem.index(p)] |= msb
            continue
        faults += 1
        if None in mem:
            i = mem.index(None)
        else:
            i = min(range(frames), key=lambda j: (age[j], j))
        mem[i] = p
        age[i] = msb
    return faults


if __name__ == "__main__":
    # Pequeña demostración con la cadena clásica.
    classic = [7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1]
    for frames in (3, 4):
        print(f"{frames} marcos: "
              f"FIFO={fifo(classic, frames)} "
              f"LRU={lru(classic, frames)} "
              f"OPT={opt(classic, frames)} "
              f"SC={second_chance(classic, frames)} "
              f"Aging={aging(classic, frames)}")
