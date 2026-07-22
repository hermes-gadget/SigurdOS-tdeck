import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))

import audit_launcher_artifact as launcher  # noqa: E402
from release_artifact_contract import (  # noqa: E402
    COMPONENT_OFFSETS,
    FULL_IMAGE_NAMES,
    GENERATED_WEB_OFFSETS,
    REQUIRED_RELEASE_ARTIFACTS,
    ReleaseArtifactError,
    sha256_file,
    validate_release_directory,
)


def partition_entry(partition_type, subtype, offset, size, label):
    return struct.pack(
        "<HBBII16sI",
        launcher.ENTRY_MAGIC,
        partition_type,
        subtype,
        offset,
        size,
        label.encode().ljust(16, b"\0"),
        0,
    )


def app_image(segment=b"test"):
    common = struct.pack("<BBBBI", launcher.ESP_IMAGE_MAGIC, 1, 2, 0, 0x40370000)
    extended = struct.pack(
        "<BBBBHBHHBBBBB",
        0xEE,
        0,
        0,
        0,
        launcher.ESP32S3_CHIP_ID,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        1,
    )
    image = common + extended + struct.pack("<II", 0x3FC80000, len(segment)) + segment
    checksum = 0xEF
    for value in segment:
        checksum ^= value
    checksum_end = launcher.align_up(len(image) + 1, 16)
    image += b"\0" * (checksum_end - len(image) - 1) + bytes([checksum])
    return image + hashlib.sha256(image).digest()


def merged_fixture():
    app = app_image()
    bootloader = app_image(b"boot")
    entries = b"".join(
        [
            partition_entry(launcher.DATA_TYPE, launcher.DATA_SUBTYPE_OTA, 0xE000, 0x2000, "otadata"),
            partition_entry(launcher.APP_TYPE, 0x10, 0x10000, 0x20000, "app0"),
            partition_entry(launcher.DATA_TYPE, launcher.DATA_SUBTYPE_SPIFFS, 0x30000, 0x10000, "spiffs"),
        ]
    )
    table = (
        entries
        + struct.pack("<H", launcher.MD5_MAGIC)
        + b"\xFF" * 14
        + hashlib.md5(entries).digest()  # nosec: ESP partition format mandates MD5
    )
    merged = bytearray(b"\xFF" * (0x10000 + len(app)))
    merged[: len(bootloader)] = bootloader
    merged[0x8000 : 0x8000 + len(table)] = table
    merged[0xE000 : 0xE000 + 32] = (
        struct.pack("<I", 1) + b"\xFF" * 24 + struct.pack("<I", 0x4743989A)
    )
    merged[0xF000 : 0xF004] = b"\0" * 4
    merged[0x10000:] = app
    return bytes(merged), bootloader, bytes(merged[0x8000:0x8C00]), bytes(merged[0xE000:0x10000]), app


def make_release_directory(path, commit="a" * 40, version="beta-test"):
    merged, bootloader, partitions, boot_app0, firmware = merged_fixture()
    components = {
        "sigurdos-tdeck-bootloader.bin": bootloader,
        "sigurdos-tdeck-partitions.bin": partitions,
        "sigurdos-tdeck-boot_app0.bin": boot_app0,
        "sigurdos-tdeck-firmware.bin": firmware,
    }
    for name, data in components.items():
        (path / name).write_bytes(data)
    for name in FULL_IMAGE_NAMES:
        (path / name).write_bytes(merged)
    (path / "firmware.bin").write_bytes(firmware)
    (path / "firmware-debug.bin").write_bytes(merged)

    manifest = {
        "name": "SigurdOS T-Deck",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "parts": [
                    {"path": name, "offset": offset}
                    for name, offset in COMPONENT_OFFSETS.items()
                ],
            }
        ],
    }
    (path / "manifest.json").write_text(json.dumps(manifest))
    records = {}
    for name, offset in GENERATED_WEB_OFFSETS.items():
        key = name.removeprefix("sigurdos-tdeck-").removesuffix(".bin")
        file_path = path / name
        records[key] = {
            "file": name,
            "offset": offset,
            "size": file_path.stat().st_size,
            "sha256": sha256_file(file_path),
        }
    metadata = {
        "schema_version": 1,
        "version": version,
        "platformio_board": "t-deck",
        "mcu": "esp32s3",
        "chip_family": "ESP32-S3",
        "git_sha": commit,
        "git_dirty": False,
        "meshcore_sha": "b" * 40,
        "build_environment": "SigurdOS_TDeck",
        "partition_table": "default_16MB.csv",
        "source_timestamp_utc": "2026-01-01T00:00:00Z",
        "flash_mode": "keep",
        "flash_size": "16MB",
        "artifacts": records,
    }
    (path / "build-metadata.json").write_text(json.dumps(metadata))


class ReleaseArtifactTests(unittest.TestCase):
    def test_tag_workflow_gates_sanitizers_graph_and_actual_artifacts(self):
        workflow = (ROOT / ".github/workflows/build-release.yml").read_text()
        self.assertIn("pio test -e native_sanitize -v", workflow)
        self.assertIn("scripts/platformio_lock.py --check", workflow)
        self.assertIn("scripts/validate_release_artifacts.py", workflow)
        self.assertIn("--artifacts-dir artifacts", workflow)
        self.assertIn("--write-attestation artifacts/release-evidence.json", workflow)
        self.assertIn("! -name SHA256SUMS.sigstore.json", workflow)
        self.assertIn("cosign sign-blob --yes", workflow)
        self.assertIn("artifacts/SHA256SUMS.sigstore.json", workflow)
        self.assertIn("actions/attest-build-provenance@0f67c3f4856b2e3261c31976d6725780e5e4c373", workflow)
        self.assertIn("subject-path: artifacts/*", workflow)
        self.assertIn("attestations: write", workflow)
        self.assertIn("id-token: write", workflow)

    def test_tracked_firmware_directory_contains_no_binary_images(self):
        self.assertEqual([], list((ROOT / "firmware").glob("*.bin")))

    def release_dir(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        path = Path(temporary.name)
        make_release_directory(path)
        return path

    def run_script(self, name, *args):
        return subprocess.run(
            [sys.executable, str(SCRIPTS / name), *map(str, args)],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )

    def completed_source_evidence(self, tag="beta-test"):
        requirements = json.loads(
            (ROOT / "ci/release_evidence_requirements.json").read_text()
        )["requirements"]
        today = datetime.now(timezone.utc).date().isoformat()
        records = []
        for requirement in requirements:
            record = {
                "id": requirement["id"],
                "outcome": "pass",
                "evidence_url": "https://example.invalid/evidence",
                "tested_at": today,
                "firmware_version": tag,
            }
            if requirement["hardware"]:
                record["peer_version"] = "test-peer"
            records.append(record)
        return {
            "schema_version": 2,
            "tag": tag,
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "requirements": records,
        }

    def test_complete_release_directory_passes(self):
        hashes = validate_release_directory(
            self.release_dir(), expected_commit="a" * 40, expected_version="beta-test"
        )
        self.assertEqual(len(hashes), 12)

    def test_missing_file_and_string_offset_fail(self):
        path = self.release_dir()
        (path / "sigurdos-tdeck-boot_app0.bin").unlink()
        with self.assertRaisesRegex(ReleaseArtifactError, "missing release artifacts"):
            validate_release_directory(path)

        path = self.release_dir()
        manifest_path = path / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["builds"][0]["parts"][0]["offset"] = "0x0"
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ReleaseArtifactError, "numeric integers"):
            validate_release_directory(path)

    def test_metadata_hash_and_full_alias_mismatch_fail(self):
        path = self.release_dir()
        metadata_path = path / "build-metadata.json"
        metadata = json.loads(metadata_path.read_text())
        metadata["artifacts"]["firmware"]["sha256"] = "0" * 64
        metadata_path.write_text(json.dumps(metadata))
        with self.assertRaisesRegex(ReleaseArtifactError, "digest/size mismatch"):
            validate_release_directory(path)

        path = self.release_dir()
        with (path / "sigurdos-tdeck-full.bin").open("ab") as output:
            output.write(b"corrupt")
        with self.assertRaisesRegex(ReleaseArtifactError, "digest/size mismatch|byte-identical"):
            validate_release_directory(path)

    def test_cli_rejects_wrong_commit(self):
        result = self.run_script(
            "validate_release_artifacts.py",
            "--artifacts-dir",
            self.release_dir(),
            "--expected-commit",
            "b" * 40,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("does not match release commit", result.stderr)

    def test_post_build_attestation_is_bound_to_every_artifact(self):
        path = self.release_dir()
        source = path / "source-evidence.json"
        attestation = path / "release-evidence.json"
        source.write_text(json.dumps(self.completed_source_evidence()))
        create = self.run_script(
            "verify_release_evidence.py",
            "--evidence",
            source,
            "--commit",
            "a" * 40,
            "--tag",
            "beta-test",
            "--artifacts-dir",
            path,
            "--write-attestation",
            attestation,
        )
        self.assertEqual(create.returncode, 0, create.stderr)
        declared = json.loads(attestation.read_text())["artifacts"]
        self.assertEqual(len(declared), 12)

        verify = self.run_script(
            "verify_release_evidence.py",
            "--attestation",
            attestation,
            "--commit",
            "a" * 40,
            "--tag",
            "beta-test",
            "--artifacts-dir",
            path,
        )
        self.assertEqual(verify.returncode, 0, verify.stderr)

        with (path / "firmware-debug.bin").open("ab") as output:
            output.write(b"changed")
        verify = self.run_script(
            "verify_release_evidence.py",
            "--attestation",
            attestation,
            "--commit",
            "a" * 40,
            "--tag",
            "beta-test",
            "--artifacts-dir",
            path,
        )
        self.assertEqual(verify.returncode, 1)
        self.assertIn("attested digest does not match", verify.stderr)

    def test_legacy_invented_hashes_cannot_pass_post_build_gate(self):
        path = self.release_dir()
        evidence = self.completed_source_evidence()
        evidence["schema_version"] = 1
        evidence["commit"] = "a" * 40
        evidence["artifacts"] = {
            name: "0" * 64 for name in REQUIRED_RELEASE_ARTIFACTS
        }
        source = path / "legacy-evidence.json"
        source.write_text(json.dumps(evidence))
        result = self.run_script(
            "verify_release_evidence.py",
            "--evidence",
            source,
            "--commit",
            "a" * 40,
            "--tag",
            "beta-test",
            "--artifacts-dir",
            path,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("artifact hash does not match produced bytes", result.stderr)


if __name__ == "__main__":
    unittest.main()
