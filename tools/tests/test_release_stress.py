"""Release load-generator contracts; no real device traffic."""
import time
import unittest
from unittest.mock import patch
from tools.soak.verify_release import stress

class ReleaseStressTests(unittest.TestCase):
    def test_reboot_stops_further_http_batches(self):
        with patch("tools.soak.verify_release.fetch", return_value={"uptime": 1}) as fetch:
            result = stress("unused", 0, 1, 10, 100, time.monotonic())
        self.assertTrue(result["reboot_detected"])
        self.assertEqual(fetch.call_count, 1)

    def test_sse_requests_correct_accept_header(self):
        class Stream:
            def __enter__(self): return self
            def __exit__(self, *args): return False
            def readline(self): return b""
        with patch("tools.soak.verify_release.urllib.request.urlopen", return_value=Stream()) as urlopen:
            result = stress("unused", 1, 1, 0, 100, time.monotonic())
        self.assertEqual(result["sse_opened"], 1)
        request = urlopen.call_args.args[0]
        self.assertEqual(request.get_header("Accept"), "text/event-stream")
