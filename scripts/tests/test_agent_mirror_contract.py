#!/usr/bin/env python3
"""Contract tests for the owner-managed agent-context mirror check."""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SHARED_HEADING = "## Markdown Files in This Repo"


def shared_content(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    _, separator, shared = text.partition(SHARED_HEADING)
    if not separator:
        raise AssertionError(f"missing shared-content heading in {path.name}")
    return (separator + shared).replace(" ← you are here", "")


class AgentMirrorContractTests(unittest.TestCase):
    def test_owner_managed_files_match_after_intentional_differences(self):
        self.assertEqual(
            shared_content(ROOT / "AGENTS.md"),
            shared_content(ROOT / "CLAUDE.md"),
        )

    def test_workflow_anchors_on_shared_heading_not_line_numbers(self):
        workflow = (ROOT / ".github/workflows/pr-ci.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("/^## Markdown Files in This Repo$/", workflow)
        self.assertNotIn("awk 'NR>7' AGENTS.md", workflow)
        self.assertNotIn("awk 'NR>9' CLAUDE.md", workflow)


if __name__ == "__main__":
    unittest.main()
