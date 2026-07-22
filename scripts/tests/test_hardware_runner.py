"""Include the trusted hardware-runner tests in the normal CI contract suite."""

import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from hw_test.tests.test_hw_test import (  # noqa: F401
    ConstantsAndFlashTests,
    ReportTests,
    SerialParsingTests,
)
