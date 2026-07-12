import json
import os
import tempfile

import pytest

from update_feedback_badges import (
    build_badge,
    extract_reaction_counts,
    write_badges,
)


def test_extract_reaction_counts_reads_plus_and_minus_one():
    issue = {"reactions": {"+1": 12, "-1": 3, "laugh": 5}}
    assert extract_reaction_counts(issue) == (12, 3)


def test_extract_reaction_counts_defaults_to_zero_when_missing():
    assert extract_reaction_counts({}) == (0, 0)
    assert extract_reaction_counts({"reactions": {}}) == (0, 0)


def test_build_badge_shape():
    badge = build_badge("\U0001F44D works", 7, "success")
    assert badge == {
        "schemaVersion": 1,
        "label": "\U0001F44D works",
        "message": "7",
        "color": "success",
    }


def test_write_badges_produces_valid_shields_endpoint_json():
    with tempfile.TemporaryDirectory() as tmp:
        write_badges(tmp, up_count=10, down_count=2)

        with open(os.path.join(tmp, "feedback-up.json")) as f:
            up = json.load(f)
        with open(os.path.join(tmp, "feedback-down.json")) as f:
            down = json.load(f)

        assert up["message"] == "10"
        assert up["color"] == "success"
        assert down["message"] == "2"
        assert down["color"] == "critical"


def test_write_badges_uses_neutral_color_when_zero_issues():
    with tempfile.TemporaryDirectory() as tmp:
        write_badges(tmp, up_count=5, down_count=0)
        with open(os.path.join(tmp, "feedback-down.json")) as f:
            down = json.load(f)
        assert down["message"] == "0"
        assert down["color"] == "lightgrey"


def test_write_badges_creates_out_dir_if_missing():
    with tempfile.TemporaryDirectory() as tmp:
        nested = os.path.join(tmp, "nested", "badges")
        write_badges(nested, up_count=1, down_count=1)
        assert os.path.isfile(os.path.join(nested, "feedback-up.json"))
