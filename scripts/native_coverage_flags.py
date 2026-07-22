"""Link the gcov runtime into native coverage test binaries."""

Import("env")  # type: ignore  # noqa: F821

env.AppendUnique(LINKFLAGS=["--coverage"])  # type: ignore  # noqa: F821
