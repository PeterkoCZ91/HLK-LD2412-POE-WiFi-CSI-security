"""Synthetic pipeline verification, not a field accuracy claim."""
import argparse
import contextlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from tools.ml_feedback import FEATURES, train

try:
    import numpy as np
    import sklearn
except ImportError:
    np = None

@unittest.skipIf(np is None, "training dependencies unavailable")
class TrainingTests(unittest.TestCase):
    def test_synthetic_training_exports_compilable_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            rng = np.random.default_rng(42)
            for name in ("train", "test"):
                samples = []
                for i in range(40):
                    label = i % 2
                    features = rng.normal(3 * label, 0.3, 17).tolist()
                    samples.append({"seq": i + 1, "label": label, "feats": features})
                data = {"schema": "poe2412.training.v1", "features": FEATURES,
                        "session": name, "samples": samples}
                (root / (name + ".json")).write_text(json.dumps(data))
            args = argparse.Namespace(train=root / "train.json", test=root / "test.json",
                output=root / "candidate", iterations=500,
                baseline=Path(__file__).resolve().parents[2] / "include/services/ml_weights.h")
            with contextlib.redirect_stdout(io.StringIO()):
                train(args)
            report = json.loads((args.output / "evaluation.json").read_text())
            self.assertEqual(report["test_samples"], 40)
            self.assertEqual(sum(report["candidate"][k] for k in ("tp", "tn", "fp", "fn")), 40)
            header = args.output / "ml_weights.candidate.h"
            subprocess.run(["c++", "-std=c++17", "-fsyntax-only", "-x", "c++",
                            "-include", str(header), "-"], input=
                "static_assert(csi_ml::ML_H1 == 18 && csi_ml::ML_H2 == 9);",
                text=True, check=True, capture_output=True)
            # A test set from the same session must be rejected before writing.
            args.test = args.train
            with self.assertRaisesRegex(ValueError, "distinct"):
                train(args)
