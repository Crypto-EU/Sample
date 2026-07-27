"""Unit tests for SASEUL PoW + protocol helpers."""

from __future__ import annotations

import json
import time
import unittest

from onepool.pow import (
    MiningJob,
    blockhash_from_root,
    build_header_hash_hex,
    hextime_us,
    job_to_result,
    parse_job_from_upstream,
    receipt_root_hex,
    root_hex_from_parts,
    sha256_hex,
    target_from_difficulty,
    verify_classic_share,
)


class TestPow(unittest.TestCase):
    def test_hextime(self):
        self.assertEqual(len(hextime_us(0)), 14)
        self.assertEqual(hextime_us(1), "00000000000001")

    def test_empty_receipt_root(self):
        self.assertEqual(receipt_root_hex([]), sha256_hex(b""))

    def test_header_hash_stable(self):
        h1 = build_header_hash_hex(
            height=1,
            timestamp_us=123,
            main_height=2,
            main_blockhash="ab" * 39,
            validator="v",
            miner="m",
            receipts=[],
        )
        h2 = build_header_hash_hex(
            height=1,
            timestamp_us=123,
            main_height=2,
            main_blockhash="ab" * 39,
            validator="v",
            miner="m",
            receipts=[],
        )
        self.assertEqual(h1, h2)
        self.assertEqual(len(h1), 64)

    def test_parse_nested_job(self):
        sample = {
            "id": 2,
            "ok": True,
            "result": {
                "job_id": "a" * 78,
                "previous_blockhash": "a" * 78,
                "job_digest": ("b" * 64) + "@node-1#0",
                "main_blockhash": "c" * 78,
                "main_height": 10,
                "height": 5,
                "miner": "mineraddr",
                "validator": "valaddr",
                "share_difficulty": 100,
                "network_difficulty": "1000",
                "difficulty": "100",
                "receipts": [],
                "vardiff": True,
            },
        }
        job = parse_job_from_upstream(sample)
        self.assertEqual(job.height, 5)
        self.assertEqual(job.share_difficulty, 100)
        self.assertEqual(job.digest64, "b" * 64)
        self.assertEqual(len(job.previous_blockhash), 78)

    def test_verify_easy_share(self):
        # Build a job with difficulty=1 (any hash meets target).
        now = int(time.time() * 1_000_000)
        prev = hextime_us(now) + ("11" * 32)
        main = hextime_us(now) + ("22" * 32)
        job = MiningJob(
            job_id=prev,
            previous_blockhash=prev,
            job_digest=("33" * 32) + "@demo",
            main_blockhash=main,
            validator="val",
            miner="min",
            height=1,
            main_height=1,
            share_difficulty=1,
            network_difficulty=1,
            worktime=now,
        )
        nonce = "0000000000000001"
        header = build_header_hash_hex(
            height=job.height,
            timestamp_us=now,
            main_height=job.main_height,
            main_blockhash=job.main_blockhash,
            validator=job.validator,
            miner=job.miner,
            receipts=[],
        )
        root = root_hex_from_parts(job.previous_blockhash, header, nonce)
        bh = blockhash_from_root(root, now)
        ok, reason = verify_classic_share(job, nonce, now, bh, now_us=now)
        self.assertTrue(ok, reason)

        # Wrong blockhash
        ok2, reason2 = verify_classic_share(job, nonce, now, "0" * 78, now_us=now)
        self.assertFalse(ok2)
        self.assertIn("blockhash", reason2)

    def test_job_to_result_shape(self):
        job = MiningJob(
            job_id="j",
            previous_blockhash="a" * 78,
            share_difficulty=42,
            height=7,
        )
        r = job_to_result(job)
        self.assertEqual(r["share_difficulty"], 42)
        self.assertEqual(r["height"], 7)
        self.assertIn("job_digest", r)

    def test_target_difficulty(self):
        t1 = target_from_difficulty(1)
        self.assertEqual(t1, b"\xff" * 32)
        t2 = target_from_difficulty(2)
        self.assertEqual(len(t2), 32)
        self.assertLess(t2, t1)


class TestProtocolRoundtrip(unittest.TestCase):
    def test_json_login_line(self):
        line = json.dumps({"id": 1, "method": "login", "params": ["w", "x", "rig"], "latehex": False})
        obj = json.loads(line)
        self.assertIsInstance(obj["params"], list)


if __name__ == "__main__":
    unittest.main()
