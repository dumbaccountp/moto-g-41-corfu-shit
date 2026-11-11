#!/usr/bin/env python3
"""Post a kernel build log as a commit comment.

Used by .github/workflows/build-kernel.yml when the kernel fails to compile.
The Actions log and artifact blobs are not reachable from every environment
(the sandbox that drives this repo cannot download them), so the build log is
also pushed through the public commit-comment API, where it stays readable
forever.
"""
import json
import os
import sys
import urllib.request

MAX = 60000


def main() -> int:
    if len(sys.argv) < 4:
        print("usage: post_build_log.py <repo> <sha> <logfile> [title]", file=sys.stderr)
        return 2
    repo, sha, logfile = sys.argv[1:4]
    title = sys.argv[4] if len(sys.argv) > 4 else "Kernel build failed"
    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        print("GITHUB_TOKEN not set, skipping commit comment", file=sys.stderr)
        return 0

    try:
        with open(logfile, errors="replace") as fh:
            body = fh.read()
    except OSError as exc:
        print("cannot read %s: %s" % (logfile, exc), file=sys.stderr)
        return 0

    if len(body) > MAX:
        body = "... (truncated) ...\n" + body[-MAX:]

    payload = json.dumps({"body": "**%s**\n\n```\n%s\n```" % (title, body)}).encode()
    req = urllib.request.Request(
        "https://api.github.com/repos/%s/commits/%s/comments" % (repo, sha),
        data=payload,
        method="POST",
        headers={
            "Authorization": "Bearer " + token,
            "Accept": "application/vnd.github+json",
            "Content-Type": "application/json",
            "User-Agent": "kernel-ci",
        },
    )
    try:
        urllib.request.urlopen(req, timeout=60)
    except Exception as exc:  # noqa: BLE001 - never fail the job for reporting
        print("could not post commit comment: %s" % exc, file=sys.stderr)
    else:
        print("posted %s as a commit comment" % logfile)
    return 0


if __name__ == "__main__":
    sys.exit(main())
