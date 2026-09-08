import unittest
from tools.ml_feedback import fetch_export, validate_sample, metrics

def sample(seq, label=0):
    return {"seq": seq, "label": label, "feats": [float(seq)] * 17, "ts": seq}

def page(samples, ceiling):
    return {"schema": "poe2412.ml-feedback.v1", "feature_count": 17, "available": True,
            "read_error": False, "samples": samples, "returned": len(samples), "last_seq": ceiling}

class FeedbackTests(unittest.TestCase):
    def test_pagination_stops_at_initial_ceiling(self):
        pages = iter([page([sample(5), sample(6)], 7), page([sample(7), sample(8)], 8)])
        data = fetch_export("unused", "session-a", lambda _: next(pages))
        self.assertEqual([s["seq"] for s in data["samples"]], [5, 6, 7])

    def test_empty_ring(self):
        data = fetch_export("unused", "session-a", lambda _: page([], 0))
        self.assertEqual(data["samples"], [])

    def test_duplicate_missing_and_unreadable_pages_are_rejected(self):
        for following in (page([sample(1)], 3), page([sample(3)], 3), page([], 3),
                          {**page([sample(2)], 3), "read_error": True}):
            with self.subTest(following=following):
                pages = iter([page([sample(1)], 3), following])
                with self.assertRaises(ValueError):
                    fetch_export("unused", "session-a", lambda _: next(pages))

    def test_nonfinite_wrong_shape_and_invalid_labels(self):
        for value in ({**sample(1), "feats": [0]*16},
                      {**sample(1), "feats": [float("nan")]*17},
                      {**sample(1), "label": True}, sample(0)):
            with self.assertRaises(ValueError): validate_sample(value)

    def test_false_positives_and_misses_are_separate(self):
        result = metrics([0,0,1,1], [1,0,0,1])
        self.assertEqual((result["fp"], result["fn"]), (1,1))
        self.assertEqual(result["false_positive_rate"], 0.5)
        self.assertEqual(result["miss_rate"], 0.5)

if __name__ == "__main__":
    unittest.main()

