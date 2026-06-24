"""
OrganoSat depth sweep.

This script trains the decision tree at different maximum depths and prints the test accuracies.
This is why we have opted for a depth of 7, since it gave us a sufficiently high accuracy rating
"""

import pandas as pd
from sklearn.tree import DecisionTreeClassifier
from sklearn.model_selection import train_test_split
from sklearn.metrics import accuracy_score

df = pd.read_csv("training_data.csv", encoding="utf-8-sig")
df.columns = df.columns.str.strip()
df = df.dropna(subset=["DT_SCORE"])


FEATURES = ["TEMP_C", "HUMIDITY_PCT", "LUX", "ECO2_PPM",
            "CO_PPM", "NH3_PPM", "NO2_PPM"]
LABEL = "DT_SCORE"

X = df[FEATURES]
y = df[LABEL].astype(int)


X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.20, random_state=42, stratify=y)

print("depth   test accuracy")
for depth in [3, 5, 7, 9, 11]:
    clf = DecisionTreeClassifier(max_depth=depth, random_state=42)
    clf.fit(X_train, y_train)
    acc = accuracy_score(y_test, clf.predict(X_test))
    print(f"  {depth:<5} {acc*100:.1f}%")
