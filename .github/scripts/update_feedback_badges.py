#!/usr/bin/env python3
"""Fetch reaction counts on the community feedback issue and write shields.io
endpoint-badge JSON files so README badges reflect live counts.

Run by .github/workflows/update-feedback-badges.yml on a schedule. GitHub
enforces at most one +1 and one -1 reaction per logged-in user per issue, so
these counts are already deduplicated per unique user.
"""

import json
import os
import sys
import urllib.error
import urllib.request

API_ROOT = "https://api.github.com"


def fetch_issue(repo: str, issue_number: int, token: str, api_root: str = API_ROOT) -> dict:
    url = f"{api_root}/repos/{repo}/issues/{issue_number}"
    req = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "User-Agent": "update-feedback-badges-script",
        },
    )
    with urllib.request.urlopen(req, timeout=15) as resp:
        return json.load(resp)


def extract_reaction_counts(issue: dict) -> tuple[int, int]:
    reactions = issue.get("reactions", {})
    return int(reactions.get("+1", 0)), int(reactions.get("-1", 0))


def build_badge(label: str, count: int, color: str) -> dict:
    return {
        "schemaVersion": 1,
        "label": label,
        "message": str(count),
        "color": color,
    }


def write_badges(out_dir: str, up_count: int, down_count: int) -> None:
    os.makedirs(out_dir, exist_ok=True)
    up_badge = build_badge("\U0001F44D works", up_count, "success")
    down_badge = build_badge("\U0001F44E issues", down_count, "critical" if down_count > 0 else "lightgrey")
    with open(os.path.join(out_dir, "feedback-up.json"), "w") as f:
        json.dump(up_badge, f, indent=2)
        f.write("\n")
    with open(os.path.join(out_dir, "feedback-down.json"), "w") as f:
        json.dump(down_badge, f, indent=2)
        f.write("\n")


def main() -> int:
    repo = os.environ.get("GITHUB_REPOSITORY")
    issue_number = os.environ.get("FEEDBACK_ISSUE_NUMBER")
    token = os.environ.get("GITHUB_TOKEN")
    out_dir = os.environ.get("BADGES_OUT_DIR", ".github/badges")

    if not repo or not issue_number or not token:
        print(
            "missing required env vars: GITHUB_REPOSITORY, FEEDBACK_ISSUE_NUMBER, GITHUB_TOKEN",
            file=sys.stderr,
        )
        return 2

    try:
        issue = fetch_issue(repo, int(issue_number), token)
    except urllib.error.HTTPError as e:
        print(f"GitHub API error fetching issue {issue_number}: {e.code} {e.reason}", file=sys.stderr)
        return 1

    up_count, down_count = extract_reaction_counts(issue)
    write_badges(out_dir, up_count, down_count)
    print(f"wrote badges: +1={up_count} -1={down_count} -> {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
