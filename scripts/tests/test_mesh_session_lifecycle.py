"""Regression contracts for authenticated MeshCore session liveness."""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/mesh/sigurd_mesh_v2.cpp").read_text(encoding="utf-8")


def function_body(signature: str) -> str:
    start = SOURCE.index(signature)
    opening = SOURCE.index("{", start)
    depth = 0
    for index in range(opening, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
        elif SOURCE[index] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[opening + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def test_non_message_ack_falls_back_to_connection_bookkeeping() -> None:
    body = function_body("SigurdMeshV2::processAck")
    assert "return ack_val == 0 ? nullptr : BaseChatMesh::checkConnectionsAck(data);" in body


def test_authenticated_direct_traffic_refreshes_connection_liveness() -> None:
    for callback in (
        "SigurdMeshV2::onMessageRecv",
        "SigurdMeshV2::onCommandDataRecv",
        "SigurdMeshV2::onSignedMessageRecv",
        "SigurdMeshV2::onContactResponse",
    ):
        body = function_body(callback)
        assert body.count("BaseChatMesh::markConnectionActive(contact);") == 1


def test_login_is_reserved_before_transmit_and_logout_clears_it() -> None:
    for sender in (
        "SigurdMeshV2::sendLoginCompanion",
        "SigurdMeshV2::sendLoginTo",
    ):
        body = function_body(sender)
        assert body.index("addLoginEntry(contact)") < body.index(
            "BaseChatMesh::sendLogin"
        )

    companion_logout = function_body("SigurdMeshV2::companionStopConnection")
    local_logout = function_body("SigurdMeshV2::sendLogoutTo")
    assert "removeLoginEntry(pub_key);" in companion_logout
    assert "removeLoginEntry(contact.id.pub_key);" in local_logout


def test_login_response_matching_uses_full_public_key() -> None:
    body = function_body("SigurdMeshV2::onContactResponse")
    assert "findLoginEntry(contact.id.pub_key)" in body
    assert not re.search(r"findLoginEntry\(contact\.name\)", body)
