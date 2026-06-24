"""
OrganoSat decision tree trainer.

Trains the decision tree on synthetic data, prints its accuracy
and size, saves the model, draws the two tree figures from that same model
and writes the tree out as a C function to be used by the firmware. The decision tree will have a depth
of 7.
"""

import joblib
import pandas as pd
import matplotlib.pyplot as plt
from sklearn.tree import DecisionTreeClassifier, export_text, plot_tree
from sklearn.model_selection import train_test_split
from sklearn.metrics import accuracy_score, classification_report

# load the training data
df = pd.read_csv("training_data.csv", encoding="utf-8-sig")
df.columns = df.columns.str.strip()
df = df.dropna(subset=["DT_SCORE"])

# use the same order the firmware code reads them in
FEATURES = ["TEMP_C", "HUMIDITY_PCT", "LUX", "ECO2_PPM",
            "CO_PPM", "NH3_PPM", "NO2_PPM"]
DISPLAY_FEATURES = ["TEMP", "HUM", "LUX", "ECO2", "CO", "NH3", "NO2"]
CLASS_NAMES = ["improbable (0)", "marginal (1)", "probable (2)"]
LABEL = "DT_SCORE"

X = df[FEATURES]
y = df[LABEL].astype(int)

# 80/20 split to make sure it is stratified so each class keeps its share
X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.20, random_state=42, stratify=y)

# train the tree - the depth capped at 7
clf = DecisionTreeClassifier(max_depth=7, random_state=42)
clf.fit(X_train, y_train)
joblib.dump(clf, "organosat_tree.joblib")   # save it so there is only one reference point for the figures and compute life score code

train_acc = accuracy_score(y_train, clf.predict(X_train))
test_acc  = accuracy_score(y_test,  clf.predict(X_test))
print(f"Train accuracy: {train_acc*100:.2f}%")
print(f"Test  accuracy: {test_acc*100:.2f}%")
print(f"Tree depth: {clf.get_depth()}, nodes: {clf.tree_.node_count}, "
      f"leaves: {clf.get_n_leaves()}\n")
print("Classification report (test set):")
print(classification_report(y_test, clf.predict(X_test),
                            target_names=CLASS_NAMES))

# full depth-7 tree for supplementary S2 of the paper
fig, ax = plt.subplots(figsize=(22, 12), dpi=150)
plot_tree(clf, feature_names=DISPLAY_FEATURES, class_names=CLASS_NAMES,
          filled=True, rounded=True, fontsize=7, impurity=True, ax=ax)
plt.tight_layout()
plt.savefig("decision_tree_depth7.png", dpi=300, bbox_inches="tight")
plt.savefig("decision_tree_depth7.pdf", bbox_inches="tight")
plt.close(fig)

# creating the top two levels for figure 1 (max_depth just cuts the drawing, the model is untouched)
fig, ax = plt.subplots(figsize=(11, 6), dpi=150)
plot_tree(clf, max_depth=2,
          feature_names=DISPLAY_FEATURES, class_names=CLASS_NAMES,
          filled=True, rounded=True, fontsize=10, impurity=True, ax=ax)
plt.tight_layout()
plt.savefig("decision_tree_depth2.png", dpi=300, bbox_inches="tight")
plt.savefig("decision_tree_depth2.pdf", bbox_inches="tight")
plt.close(fig)
print("Figures written: decision_tree_depth7.{png,pdf}, "
      "decision_tree_depth2.{png,pdf}")

# text version of the tree
tree_txt = export_text(clf, feature_names=FEATURES)
with open("tree_diagram.txt", "w") as f:
    f.write(tree_txt)
print("\nTree diagram written to tree_diagram.txt\n")
print(tree_txt)  

# export the tree as a C function for the firmware
def tree_to_arduino(tree_clf, feature_names):
    tree_ = tree_clf.tree_
    var_map = {
        "TEMP_C":       "gTemp",
        "HUMIDITY_PCT": "gHum",
        "LUX":          "gLux",
        "ECO2_PPM":     "gECO2",
        "CO_PPM":       "gCO_ppm",
        "NH3_PPM":      "gNH3_ppm",
        "NO2_PPM":      "gNO2_ppm",
    }

    lines = [
        "int computeLifeScore() {",
        "    // if any sensor reads no data, skip the tree and return -1",
        "    if (gECO2 == NO_DATA_I || gTemp == NO_DATA_F ||",
        "        gHum == NO_DATA_F || gLux == NO_DATA_F ||",
        "        gCO_ppm == NO_DATA_F || gNH3_ppm == NO_DATA_F ||",
        "        gNO2_ppm == NO_DATA_F) {",
        "        return -1;",
        "    }",
        "",
    ]

    def recurse(node, depth):
        indent = "    " * (depth + 1)
        if tree_.feature[node] >= 0:
            feat = feature_names[tree_.feature[node]]
            var = var_map[feat]
            thr = tree_.threshold[node]
            lines.append(f"{indent}if ({var} <= {thr:.3f}f) {{")
            recurse(tree_.children_left[node], depth + 1)
            lines.append(f"{indent}}} else {{")
            recurse(tree_.children_right[node], depth + 1)
            lines.append(f"{indent}}}")
        else:
            values = tree_.value[node][0]
            prediction = int(values.argmax())
            lines.append(f"{indent}return {prediction};")

    recurse(0, 0)
    lines.append("}")
    return "\n".join(lines)


c_code = tree_to_arduino(clf, FEATURES)
with open("computeLifeScore.ino", "w") as f:
    f.write(c_code + "\n")
print("Arduino C function written to computeLifeScore.ino")
