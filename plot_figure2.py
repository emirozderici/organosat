"""
OrganoSat Figure 2 - NO2 for both flights.

This script plots the NO2_ppm column from flight CSVs against the cycle number whilst also dropping corrupted readings
so the scale is readable.

"""

import pandas as pd
import matplotlib.pyplot as plt

# Logs the cumulative row number where launch happens (avoids PACKET_COUNT due to watchdog reboots)
# and how many rows of data to display after that launch point.

FLIGHTS = [
    ("SD1_1.csv", "Flight 1", 710),
    ("SD2_1.csv", "Flight 2", 938),
]
POST = 250
ROOT = 0.504   # root-node NO2 split from the trained tree

fig, axes = plt.subplots(2, 1, figsize=(12, 8))

for ax, (path, name, launch) in zip(axes, FLIGHTS):
    d = pd.read_csv(path, encoding="utf-8-sig")
    d.columns = d.columns.str.strip()
    no2 = pd.to_numeric(d["NO2_ppm"], errors="coerce")

    # drop the corrupted high readings so the scale is readable
    no2 = no2.mask(no2 > 5)

    end = launch + POST
    x = range(end)
    y = no2.iloc[:end]

    ax.plot(x, y, linewidth=0.9, color="#1f4e79")
    ax.axvspan(0, launch, color="#ececec", label="Pre-launch pad")
    ax.axvspan(launch, end, color="#fce8d4", label="Post-launch")
    ax.axhline(ROOT, color="red", linestyle="--",
               label="Root-node threshold (NO2 = 0.504 ppm)")

    ax.set_title(f"{name} - {launch} pre-launch + {POST} post-launch cycles")
    ax.set_ylabel("NO2 concentration (ppm)")
    ax.set_xlim(0, end)
    ax.set_ylim(0, 1.3)

axes[0].legend(loc="upper left", fontsize=8)
axes[1].set_xlabel("Cycle number")
plt.tight_layout()
plt.savefig("Figure2.png", dpi=200, bbox_inches="tight")
print("wrote Figure2.png")
