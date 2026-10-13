#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Public contact indexing above MeshCore's reserved anon-contact slots.
//
// MeshCore's BaseChatMesh reserves MAX_ANON_CONTACTS slots at the start of its
// backing array; getNumContacts() and ContactsIterator already exclude them,
// but upstream's getContactByIdx() indexes the raw array. SigurdOS exposes the
// same zero-based coordinate that getNumContacts() publishes (SigurdOS #1193).

#include <cstdint>

#include <helpers/BaseChatMesh.h>

namespace sigurdos {
namespace mesh {

template <typename MeshT>
inline bool publicContactByIdx(MeshT& mesh, uint32_t idx, ::ContactInfo& out) {
    if (idx >= static_cast<uint32_t>(mesh.getNumContacts())) return false;
    return mesh.getContactByIdx(idx + MAX_ANON_CONTACTS, out);
}

} // namespace mesh
} // namespace sigurdos
