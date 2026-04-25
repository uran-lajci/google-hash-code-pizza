'''pizza_sampler.py

Random instance generator for the Hash Code 2017 'Pizza' practice problem.
Generated instances interpolate between and around the three official
reference instances:

    small:  R=6     C=7     L=1  H=5    (~3.5 H/L ratio)
    medium: R=200   C=250   L=4  H=12   (~3.0 H/L ratio)
    big:    R=1000  C=1000  L=6  H=14   (~2.3 H/L ratio)

Strategy:
  * R and C are sampled on a log scale between [3, 1500] so the full
    4-orders-of-magnitude range is covered uniformly in log-space.
  * L is sampled in [1, 12] (a bit above the max reference of 6).
  * H is derived as round(L * ratio) where ratio is sampled in [2.0, 5.0],
    matching the H/L ratios seen in the reference instances. This keeps
    instances solvable (H >= 2L) and realistically shaped.
'''

import math
import os
import random
import secrets
import sys
from argparse import ArgumentParser


def sample_params(rng: random.Random) -> tuple[int, int, int, int]:
    R = int(round(math.exp(rng.uniform(math.log(3), math.log(1500)))))
    C = int(round(math.exp(rng.uniform(math.log(3), math.log(1500)))))
    R = min(R, 1000)
    C = min(C, 1000)

    L = rng.randint(1, min(12, max(1, R * C // 2)))
    H = min(1000, R * C, max(2 * L, round(L * rng.uniform(2.0, 5.0))))

    return R, C, L, H


def generate_case(path: str, rng: random.Random) -> tuple[int, int, int, int]:
    R, C, L, H = sample_params(rng)

    p_tomato = rng.uniform(0.35, 0.65)
    weights = (p_tomato, 1.0 - p_tomato)

    lines = [f'{R} {C} {L} {H}']
    for _ in range(R):
        lines.append(''.join(rng.choices('TM', weights=weights, k=C)))

    with open(path, 'w', encoding='ascii', newline='') as f:
        f.write('\n'.join(lines) + '\n')

    return R, C, L, H


def main(num_cases: int, seed: int | None, outdir: str) -> None:
    if num_cases <= 0:
        sys.exit('Error: num_cases must be positive')

    if seed is None:
        seed = secrets.randbits(64)
    rng = random.Random(seed)

    os.makedirs(outdir, exist_ok=True)
    print(f"Generating {num_cases} case(s) into '{outdir}' (seed={seed}).")

    for t in range(num_cases):
        R, C, L, H = generate_case(os.path.join(outdir, f'{t}.txt'), rng)
        print(f'  {t}.txt: R={R} C={C} L={L} H={H} (cells={R * C})')

    print('Done.')


if __name__ == '__main__':
    parser = ArgumentParser(description='Generate Hash Code 2017 Pizza instances around the reference set.')
    parser.add_argument('-n', '--num_cases', type=int, required=True)

    parser.add_argument('-s', '--seed', type=int, default=None)
    parser.add_argument('-o', '--outdir', type=str, default='benchmark/in')

    args = parser.parse_args()
    main(args.num_cases, args.seed, args.outdir)
