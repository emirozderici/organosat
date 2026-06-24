[![reproduce](https://github.com/emirozderici/organosat/actions/workflows/reproduce.yml/badge.svg)](https://github.com/emirozderici/organosat/actions/workflows/reproduce.yml)
# OrganoSat

Companion code and data for the OrganoSat post-flight evaluation paper. *(Citation and DOI to be added on publication.)*

Code and data for the OrganoSat habitability classifier. The decision
tree is trained in Python on synthetic environment data then exported as a
C function that runs on the ESP32 in the CanSat firmware. The flight logs from
both launches are here as well.

## Files

- `train_model.py` - trains the decision tree, saves it, makes the two tree
  figures and writes the C function for the firmware.
- `plot_figure2.py` - reads the flight logs and plots the NO2 figure (Figure 2),
  with the pre- and post-launch split and the six corrupted Flight 2 readings
  left out.
- `depth_sweep.py` - trains the tree at depths 3, 5, 7, 9 and 11 and prints the
  test accuracy for each, which is how depth 7 was chosen.
- `training_data.csv` - the 7,500 row synthetic dataset (15 environments). The
  label column is DT_SCORE: 0 = improbable, 1 = marginal, 2 = probable.
- `OrganoSat_firmware.ino` - the CanSat firmware. Reads
  the sensors every loop, runs computeLifeScore() and logs everything to the SD card.
- `SD1_1.csv`, `SD2_1.csv` - the raw flight logs from Flight 1 and Flight 2. The
  DT_SCORE column is the score the tree gave in flight (-1 means a sensor had no
  data so the tree was skipped).

## Running it

Needs Python 3.11 and the packages in requirements.txt:

    pip install -r requirements.txt
    python train_model.py

It prints the accuracy and the tree size, saves the model
(organosat_tree.joblib), writes the two tree figures and writes
computeLifeScore.ino. That C function is the same logic the firmware uses.

Important: the scikit-learn version matters because the tree picks its
own split values and these can move between versions. I used 1.3.2
(it's pinned in requirements.txt).

## Reproducing the results

- The tree, the 99.8% accuracy and the two tree figures (Figure 1 and the full
  tree in Supplementary S2) all come from train_model.py.
- The firmware's computeLifeScore() is the same tree exported to C. it
  matches the figures and the thresholds.
- Figure 2 comes from plot_figure2.py, which plots the NO2_ppm column from the
  flight logs against the cycle number (split between pre- and post-launch). The
  6 corrupted Flight 2 readings (NO2 between 93 and 1157 ppm) are left out.
- The depth comparison in the paper (accuracy at depths 3, 5, 7, 9, 11) comes
  from depth_sweep.py.
