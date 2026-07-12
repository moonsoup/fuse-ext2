from post_reconsider_comment import build_comment, should_post


def test_should_post_true_for_bug_closed_completed():
    assert should_post(["bug"], "completed") is True


def test_should_post_false_when_not_bug_labeled():
    assert should_post(["enhancement"], "completed") is False


def test_should_post_false_when_closed_not_planned():
    assert should_post(["bug"], "not_planned") is False


def test_should_post_false_when_state_reason_missing():
    assert should_post(["bug"], "") is False


def test_should_post_true_with_multiple_labels_including_bug():
    assert should_post(["needs-triage", "bug", "macos"], "completed") is True


def test_build_comment_links_to_tracking_issue():
    comment = build_comment("moonsoup/fuse-ext2", 42)
    assert "https://github.com/moonsoup/fuse-ext2/issues/42" in comment
    assert "\U0001F44E" in comment
