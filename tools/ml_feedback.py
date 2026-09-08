#!/usr/bin/env python3
"""Export labeled device vectors; train/evaluate a candidate on separate sessions.

Fetch uses only the Python standard library. Training needs numpy and
scikit-learn. Candidate weights are never installed in firmware automatically.
"""
import argparse
import base64
import json
import math
import os
from pathlib import Path
import re
import urllib.request

FEATURES = ("turb_mean turb_std turb_max turb_min turb_zcr turb_skewness turb_kurtosis "
            "turb_entropy turb_autocorr turb_mad turb_slope waveform_length "
            "phase_turbulence ratio_turbulence breathing_score dser plcr").split()

def validate_sample(sample):
    if type(sample.get("seq")) is not int or sample["seq"] < 1:
        raise ValueError("invalid sample sequence")
    if type(sample.get("label")) is not int or sample["label"] not in (0, 1):
        raise ValueError("invalid label")
    values = sample.get("feats")
    if not isinstance(values, list) or len(values) != 17:
        raise ValueError("expected 17 features")
    if any(type(v) not in (int, float) or not math.isfinite(v) for v in values):
        raise ValueError("non-finite or nonnumeric feature")

def fetch_export(host, session, get=None):
    if get is None:
        pair = f"{os.environ.get('POE_HTTP_USER', 'admin')}:{os.environ.get('POE_HTTP_PASSWORD', 'admin')}"
        auth = "Basic " + base64.b64encode(pair.encode()).decode()
        def get(path):
            req = urllib.request.Request(f"http://{host}{path}", headers={"Authorization": auth})
            with urllib.request.urlopen(req, timeout=10) as response:
                return json.load(response)
    cursor, ceiling, samples = 0, None, []
    while True:
        page = get(f"/api/csi/feedback/export?limit=8&after_seq={cursor}")
        if page.get("available") is False or page.get("read_error"):
            raise ValueError("feedback store unavailable or unreadable")
        if page.get("schema") != "poe2412.ml-feedback.v1" or page.get("feature_count") != 17:
            raise ValueError("incompatible feedback schema")
        if ceiling is None:
            ceiling = page["last_seq"]
            if type(ceiling) is not int or ceiling < 0: raise ValueError("invalid export high-water mark")
        batch = page["samples"]
        if page["returned"] != len(batch):
            raise ValueError("incorrect page count")
        if not batch:
            if cursor < ceiling: raise ValueError("export ended before its initial high-water mark")
            break
        for sample in batch:
            validate_sample(sample)
            if sample["seq"] <= cursor: raise ValueError("duplicate or regressing cursor")
            if samples and sample["seq"] != cursor + 1:
                raise ValueError("ring overwritten during export; retry")
            if sample["seq"] > ceiling:
                if cursor < ceiling: raise ValueError("ring overwritten during export; retry")
                break
            samples.append(sample)
            cursor = sample["seq"]
        if cursor >= ceiling: break
    return {"schema": "poe2412.training.v1", "session": session, "features": FEATURES, "samples": samples}

def load_dataset(path):
    data = json.loads(path.read_text())
    if data.get("schema") != "poe2412.training.v1" or data.get("features") != FEATURES:
        raise ValueError("incompatible dataset")
    seen = set()
    for sample in data["samples"]:
        validate_sample(sample)
        if sample["seq"] in seen: raise ValueError("duplicate sequence")
        seen.add(sample["seq"])
    return data

def metrics(y, prediction):
    tp = sum(int(a == 1 and b == 1) for a, b in zip(y, prediction))
    tn = sum(int(a == 0 and b == 0) for a, b in zip(y, prediction))
    fp = sum(int(a == 0 and b == 1) for a, b in zip(y, prediction))
    fn = sum(int(a == 1 and b == 0) for a, b in zip(y, prediction))
    return {"tp": tp, "tn": tn, "fp": fp, "fn": fn,
            "false_positive_rate": fp / (fp + tn) if fp + tn else None,
            "miss_rate": fn / (fn + tp) if fn + tp else None,
            "f1": 2 * tp / (2 * tp + fp + fn) if 2 * tp + fp + fn else 0}

def read_weights(path, np):
    text = path.read_text()
    result = {}
    for name, shape in (("ML_FEATURE_MEAN", (17,)), ("ML_FEATURE_SCALE", (17,)),
                        ("ML_W1", (17,18)), ("ML_B1", (18,)), ("ML_W2", (18,9)),
                        ("ML_B2", (9,)), ("ML_W3", (9,1)), ("ML_B3", (1,))):
        match = re.search(r"constexpr float " + name + r"[^=]*=\s*\{(.*?)\};", text, re.S)
        if not match: raise ValueError(f"missing weights: {name}")
        values = re.findall(r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?(?=f)", match[1])
        result[name] = np.array([float(v) for v in values], dtype=np.float32).reshape(shape)
        if not np.isfinite(result[name]).all(): raise ValueError(f"non-finite weights: {name}")
    return result

def predict(weights, x, np):
    z = (x - weights["ML_FEATURE_MEAN"]) / np.maximum(weights["ML_FEATURE_SCALE"], np.float32(1e-10))
    z = np.maximum(0, z @ weights["ML_W1"] + weights["ML_B1"])
    z = np.maximum(0, z @ weights["ML_W2"] + weights["ML_B2"])
    logits = (z @ weights["ML_W3"] + weights["ML_B3"]).ravel()
    return (logits > 0).astype(int)

def train(args):
    import numpy as np
    from sklearn.neural_network import MLPClassifier
    from sklearn.preprocessing import StandardScaler
    training, testing = load_dataset(args.train), load_dataset(args.test)
    if (not training.get("session") or not testing.get("session")
            or training["session"] == testing["session"]):
        raise ValueError("train and test must be distinct recording sessions")
    train_vectors = {tuple(s["feats"]) for s in training["samples"]}
    if any(tuple(s["feats"]) in train_vectors for s in testing["samples"]):
        raise ValueError("train/test feature overlap; independent holdout required")
    for data in (training, testing):
        counts = [sum(s["label"] == label for s in data["samples"]) for label in (0,1)]
        if min(counts) < 10: raise ValueError("each session needs at least 10 labels per class")
    x = np.array([s["feats"] for s in training["samples"]], dtype=np.float32)
    y = np.array([s["label"] for s in training["samples"]])
    xt = np.array([s["feats"] for s in testing["samples"]], dtype=np.float32)
    yt = np.array([s["label"] for s in testing["samples"]])
    scaler = StandardScaler().fit(x)  # no holdout leakage into normalization
    model = MLPClassifier(hidden_layer_sizes=(18,9), activation="relu", solver="lbfgs",
                          max_iter=args.iterations, random_state=42).fit(scaler.transform(x), y)
    weights = {"ML_FEATURE_MEAN": scaler.mean_, "ML_FEATURE_SCALE": scaler.scale_}
    for i in range(3):
        weights[f"ML_W{i+1}"] = model.coefs_[i]
        weights[f"ML_B{i+1}"] = model.intercepts_[i]
    args.output.mkdir(parents=True, exist_ok=True)
    header = args.output / "ml_weights.candidate.h"
    lines = ["#pragma once", "#include <stdint.h>", "// Candidate; evaluate before installing.",
             "namespace csi_ml {", "constexpr uint8_t ML_H1 = 18;", "constexpr uint8_t ML_H2 = 9;"]
    for name, value in weights.items():
        def initializer(a):
            return "{" + ", ".join(initializer(v) if hasattr(v, "__len__") else f"{float(v):.9e}f" for v in a) + "}"
        shape = "".join(f"[{n}]" for n in value.shape)
        lines.append(f"constexpr float {name}{shape} = {initializer(value)};")
    lines.append("}")
    with header.open("x") as f: f.write("\n".join(lines) + "\n")
    candidate = read_weights(header, np)
    baseline = read_weights(args.baseline, np)
    new_result, old_result = metrics(yt, predict(candidate, xt, np)), metrics(yt, predict(baseline, xt, np))
    report = {"train_samples": len(y), "test_samples": len(yt), "candidate": new_result,
              "baseline": old_result, "threshold": 0.5,
              "no_regression": new_result["fp"] <= old_result["fp"] and new_result["fn"] <= old_result["fn"]}
    with (args.output / "evaluation.json").open("x") as f: json.dump(report, f, indent=2)
    print(json.dumps(report, indent=2))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    fetch = sub.add_parser("fetch")
    fetch.add_argument("--host", required=True)
    fetch.add_argument("--session", required=True)
    fetch.add_argument("--output", type=Path, required=True)
    fit = sub.add_parser("train")
    fit.add_argument("--train", type=Path, required=True)
    fit.add_argument("--test", type=Path, required=True)
    fit.add_argument("--output", type=Path, required=True)
    fit.add_argument("--iterations", type=int, default=500)
    fit.add_argument("--baseline", type=Path, default=Path(__file__).resolve().parents[1] / "include/services/ml_weights.h")
    args = parser.parse_args()
    if args.command == "train":
        train(args)
    else:
        data = fetch_export(args.host, args.session)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x") as f: json.dump(data, f, indent=2, allow_nan=False)
        print(f"Exported {len(data['samples'])} samples")

if __name__ == "__main__":
    main()

