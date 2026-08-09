#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

namespace {

std::filesystem::path findProjectRoot() {
    auto candidate = std::filesystem::current_path();
    while (!candidate.empty()) {
        if (std::filesystem::is_regular_file(
                candidate / "scripts" / "tests" / "test_evidence_diagnostics.py")) {
            return candidate;
        }
        const auto parent = candidate.parent_path();
        if (parent == candidate) {
            break;
        }
        candidate = parent;
    }
    return {};
}

}  // namespace

TEST(EvidenceDiagnostics, PythonRegressionSuiteRunsInNativeGate) {
    // The evidence validators are host-side Python tools; keep their
    // deterministic fixtures in the PlatformIO native test gate as well.
    const auto root = findProjectRoot();
    ASSERT_FALSE(root.empty()) << "PlatformIO did not start inside the project tree";

    const auto test_script = root / "scripts" / "tests" / "test_evidence_diagnostics.py";
    const std::string command = "python3 \"" + test_script.string() + "\" -v";
    ASSERT_EQ(std::system(command.c_str()), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
