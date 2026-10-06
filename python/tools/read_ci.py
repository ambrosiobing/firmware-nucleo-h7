#!/usr/bin/env python3
"""Read this repository's CI state without spending the API allowance.

    python python/tools/read_ci.py                 the recent runs, one line each
    python python/tools/read_ci.py <sha>           the jobs of one commit
    python python/tools/read_ci.py --limit 20      more rows

WHY THIS EXISTS, AND IT IS A MISTAKE RATHER THAN A FEATURE REQUEST. Checking a
run through api.github.com without a token is allowed 60 requests an hour per
address. On Monday 5 October 2026 a twenty second poll waiting for a run spent
180 of them inside one hour, which left none, and the answer to the question the
poll was asking had to wait for the quota to reset. The pages under github.com
are served from a different budget, generous and not published as a number, and
they carry the same facts. So this reads the pages.

WHAT IT READS.
  https://github.com/<owner>/<repo>/actions                  the run list
  https://github.com/<owner>/<repo>/commit/<sha>/checks      the jobs of a commit
Both are server rendered HTML. That is worth stating because the first guess was
that they are not: the Actions view is a React application, and the expectation
was an empty shell. The run rows are in the HTML. The job list on a run page is
not, which is why the per commit view here is the checks page and not the run
page.

WHAT IT CANNOT DO, so that nobody reaches for it and finds out the hard way. It
reads no step, no log, no annotation and no artefact, it cannot re-run anything,
and it sees only what a signed out visitor sees. For a step list or a log, open
the run in a browser, or spend the API quota deliberately and once.

WHAT IT REFUSES TO DO, which is the part that matters. This parses markup that
belongs to somebody else and can change without notice. A parser that stops
matching returns an empty list of runs, and an empty list of runs reads exactly
like a repository with nothing red in it. That failure mode is silent and it
points the wrong way, so zero parsed rows is a refusal with exit status 2 and
the word REFUSED, never an empty table. It is the same rule the parity suite
applies to itself when it finds fewer languages than it expected to compare.

HOW TO READ A CANCELLED RUN, which cost an hour before it was understood. On
Monday 5 October 2026 six runs across two commits and three workflows each
lasted 15m 2s. The run list calls every one of them failed, the run page's own
icon calls it cancelled, and the jobs say "This job was cancelled" with not one
step having run. A green run of the same three workflows takes 32 to 39 seconds.
So a run of about fifteen minutes whose jobs show no steps is a job that never
got a runner. It is not a defect in this repository, nothing in the workflows
asks for it, no timeout-minutes and no concurrency block exists in any of the
three, and editing a workflow cannot fix it.

Nothing in the build or the test suite calls this file, and nothing should.
check_links.py says why in its own header: a check that needs the network fails
for reasons that have nothing to do with this repository. This is a tool for a
person at a keyboard who wants to know what CI did, and it is committed so that
the next person does not write the twenty second poll again.
"""
from __future__ import annotations

import argparse
import html
import re
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent   # python/tools -> repo root

OWNER_REPO = "ambrosiobing/firmware-nucleo-h7"
BASE = "https://github.com/" + OWNER_REPO
TIMEOUT_S = 30

# One run row on the Actions list. The conclusion is in the row's aria-label,
# which reads: failed:  Run 75 of firmware.
ROW_SPLIT = 'id="check_suite_'
ROW_LABEL = re.compile(r'aria-label="([a-z ]+): +Run (\d+) of (\w+)\.')
ROW_SHA = re.compile(r"commit/([0-9a-f]{40})")
ROW_WHEN = re.compile(r'datetime="([^"]+)"')
ROW_DURATION = re.compile(r'aria-label="Run duration".*?<span>\s*([^<]+?)\s*</span>', re.S)

# One job row on the checks page: a status icon, then an anchor whose text is the
# job name. They alternate in document order, so they are paired by position.
JOB_TOKEN = re.compile(
    r'aria-label="This job ([a-z ]+)"'
    r'|class="d-inline-block SideNav-subItem[^>]*>(.*?)</a>',
    re.S,
)

# The words GitHub uses, mapped to the words this volume uses. A status that is
# not in here is printed as it was found rather than guessed at, which is how
# the first live run taught this table two entries it was missing: the list says
# "currently running" and "queued", not the "is in progress" and "is queued"
# that were guessed at on Monday 5 October 2026 when every run on the page was
# already finished. Observed on Tuesday 6 October 2026.
PLAIN = {
    "completed successfully": "green",
    "succeeded": "green",
    "failed": "RED",
    "failure": "RED",
    "was cancelled": "cancelled",
    "cancelled": "cancelled",
    "currently running": "running",
    "is in progress": "running",
    "in progress": "running",
    "was skipped": "skipped",
    "skipped": "skipped",
    "queued": "queued",
    "is queued": "queued",
    "waiting": "waiting",
    "is waiting": "waiting",
}


def fetch(url: str) -> str:
    try:
        with urllib.request.urlopen(url, timeout=TIMEOUT_S) as response:
            if response.status != 200:
                refuse("got HTTP " + str(response.status) + " from " + url)
            return response.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as error:
        refuse("got HTTP " + str(error.code) + " from " + url)
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        refuse("could not reach " + url + ": " + str(error))
    return ""   # unreachable; refuse() exits


def refuse(why: str) -> None:
    print("REFUSED: " + why, file=sys.stderr)
    raise SystemExit(2)


def text_of(markup: str) -> str:
    return " ".join(html.unescape(re.sub(r"<[^>]+>", " ", markup)).split())


def full_sha(given: str) -> str:
    """GitHub's checks page wants all forty characters.

    A short SHA there answers 404, which looks exactly like a commit that does
    not exist, so it is resolved here against the local clone instead of being
    sent and misread.
    """
    if re.fullmatch(r"[0-9a-f]{40}", given):
        return given
    try:
        done = subprocess.run(
            ["git", "-C", str(ROOT), "rev-parse", given],
            capture_output=True, text=True, check=False,
        )
    except OSError as error:
        refuse("no git here to expand " + given + ": " + str(error))
        return ""
    out = done.stdout.strip()
    if done.returncode != 0 or not re.fullmatch(r"[0-9a-f]{40}", out):
        refuse(
            given + " is not forty characters and this clone cannot expand it. "
            "The checks page answers 404 for a short SHA, which would read as "
            "a commit that does not exist."
        )
    return out


def show_runs(limit: int) -> None:
    page = fetch(BASE + "/actions")
    rows = page.split(ROW_SPLIT)[1:]
    found = []
    for row in rows:
        label = ROW_LABEL.search(row)
        if label is None:
            continue
        status, number, workflow = label.group(1), label.group(2), label.group(3)
        sha = ROW_SHA.search(row)
        when = ROW_WHEN.search(row)
        duration = ROW_DURATION.search(row)
        found.append((
            workflow,
            number,
            PLAIN.get(status, status),
            sha.group(1)[:7] if sha else "?",
            when.group(1) if when else "?",
            duration.group(1) if duration else "?",
        ))
        if len(found) >= limit:
            break

    if not found:
        refuse(
            "parsed no run rows out of " + str(len(page)) + " bytes of "
            + BASE + "/actions. Either this repository has never run a "
            "workflow, or the markup moved and this parser is reporting "
            "silence as good news. Open the page in a browser before "
            "believing either."
        )

    wide_workflow = max([len("workflow")] + [len(row[0]) for row in found])
    wide_state = max([len("state")] + [len(row[2]) for row in found])
    head = "{:<" + str(wide_workflow) + "} {:<6} {:<" + str(wide_state) + "} {:<8} {:<21} {}"
    print(head.format("workflow", "run", "state", "commit", "pushed (UTC)", "duration"))
    for workflow, number, status, sha, when, duration in found:
        print(head.format(workflow, "#" + number, status, sha, when, duration))


def show_jobs(given: str) -> None:
    sha = full_sha(given)
    page = fetch(BASE + "/commit/" + sha + "/checks")

    status = None
    found = []
    for token in JOB_TOKEN.finditer(page):
        if token.group(1) is not None:
            status = token.group(1)
        elif status is not None:
            found.append((PLAIN.get(status, status), text_of(token.group(2))))
            status = None

    if not found:
        refuse(
            "parsed no job rows for " + sha[:7] + " out of " + str(len(page))
            + " bytes. Either no workflow ran on that commit, or the markup "
            "moved. An empty job list reads as nothing red, so this refuses "
            "rather than print it."
        )

    print(sha[:7] + ": " + str(len(found)) + " jobs")
    for status, name in found:
        print("  {:<10} {}".format(status, name))

    cancelled = sum(1 for status, _ in found if status == "cancelled")
    if cancelled:
        print(
            "\n" + str(cancelled) + " of " + str(len(found)) + " never got a "
            "runner. A cancelled job has no steps, and the run list calls the "
            "whole run failed. See this file's header."
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Read CI state from github.com pages, not the rate limited API.",
    )
    parser.add_argument(
        "sha", nargs="?",
        help="show the jobs of one commit instead of the run list; "
             "a short SHA is expanded against this clone",
    )
    parser.add_argument(
        "--limit", type=int, default=12,
        help="how many runs to list (default 12)",
    )
    args = parser.parse_args()

    if args.limit < 1:
        refuse("--limit " + str(args.limit) + " would ask for no rows at all")

    if args.sha:
        show_jobs(args.sha)
    else:
        show_runs(args.limit)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
