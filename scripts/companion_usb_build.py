"""Keep BLE-only MeshCore sources out of the companion USB build."""

Import("env")  # type: ignore  # noqa: F821


def skip_meshcore_ble_source(node):
    print("Companion USB: excluding", node.srcnode().get_path())
    return None


env.AddBuildMiddleware(  # type: ignore  # noqa: F821
    skip_meshcore_ble_source,
    "*SerialBLEInterface.cpp",
)
