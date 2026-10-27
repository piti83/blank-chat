# Blank Chat final benchmark analysis

Post-processing only. These scripts never modify raw benchmark runs.

Final source runs:
- BT-03: `20260925_001238`
- BT-04: `20260928_002000`
- BT-05: `20260925_064621`
- BT-06: `20260925_233653`

Generated outputs go to `benchmarks/results/final-analysis/`.

Dependencies on Arch Linux:

```bash
sudo pacman -S --needed python-numpy python-pandas python-scipy python-matplotlib
```

Run everything from the repository root:

```bash
python3 benchmarks/final_analysis/run_all.py
```

BT-06 defaults to 10,000 deterministic bootstrap resamples (seed 260928).
