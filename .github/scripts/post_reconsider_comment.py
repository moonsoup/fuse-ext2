#!/usr/bin/env python3
"""When a bug report is closed as fixed, comment asking the reporter to
revisit the community feedback issue and update their reaction if it's
resolved for them.

Run by .github/workflows/reconsider-feedback.yml on issues.closed events.
Only fires for issues labeled "bug" that were closed as completed (not
"not planned" / duplicate / etc) — see should_post().
"""

import json
import os
import sys
import urllib.error
import urllib.request

API_ROOT = "https://api.github.com"


def should_post(labels: list[str], state_reason: str) -> bool:
    return "bug" in labels and state_reason == "completed"


def build_comment(tracking_repo: str, tracking_issue_number: int) -> str:
    return (
        "This was just fixed and closed. If you reacted \U0001F44E on the "
        f"[community feedback issue](https://github.com/{tracking_repo}/issues/{tracking_issue_number}) "
        "because of this, please give it another try and update your reaction "
        "there if it's resolved for you now — it helps keep that signal accurate "
        "for everyone else deciding whether to use this. \U0001F64F"
    )


def post_comment(repo: str, issue_number: int, body: str, token: str, api_root: str = API_ROOT) -> None:
    url = f"{api_root}/repos/{repo}/issues/{issue_number}/comments"
    data = json.dumps({"body": body}).encode()
    req = urllib.request.Request(
        url,
        data=data,
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "Content-Type": "application/json",
            "User-Agent": "reconsider-feedback-script",
        },
    )
    with urllib.request.urlopen(req, timeout=15) as resp:
        resp.read()


def main() -> int:
    repo = os.environ.get("GITHUB_REPOSITORY")
    issue_number = os.environ.get("ISSUE_NUMBER")
    state_reason = os.environ.get("STATE_REASON", "")
    labels_raw = os.environ.get("ISSUE_LABELS", "")
    tracking_issue_number = os.environ.get("FEEDBACK_ISSUE_NUMBER")
    token = os.environ.get("GITHUB_TOKEN")

    if not repo or not issue_number or not tracking_issue_number or not token:
        print(
            "missing required env vars: GITHUB_REPOSITORY, ISSUE_NUMBER, "
            "FEEDBACK_ISSUE_NUMBER, GITHUB_TOKEN",
            file=sys.stderr,
        )
        return 2

    labels = [l.strip() for l in labels_raw.split(",") if l.strip()]

    if not should_post(labels, state_reason):
        print(f"skipping: labels={labels} state_reason={state_reason!r} does not qualify")
        return 0

    body = build_comment(repo, int(tracking_issue_number))
    try:
        post_comment(repo, int(issue_number), body, token)
    except urllib.error.HTTPError as e:
        print(f"GitHub API error posting comment on issue {issue_number}: {e.code} {e.reason}", file=sys.stderr)
        return 1

    print(f"posted reconsider-feedback comment on issue #{issue_number}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
