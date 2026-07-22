#pragma once

namespace sigurdos {
namespace mesh {
namespace detail {

// Run every persistence backend even when an earlier one fails so callers get
// the strongest checkpoint possible while still receiving aggregate failure.
template <typename SavePrefs, typename SaveChannels, typename SaveIdentity,
          typename SaveContacts, typename SaveRegions>
bool saveStateCheckpoint(bool regions_dirty,
                         SavePrefs save_prefs,
                         SaveChannels save_channels,
                         SaveIdentity save_identity,
                         SaveContacts save_contacts,
                         SaveRegions save_regions)
{
    const bool prefs_saved = save_prefs();
    const bool channels_saved = save_channels();
    const bool identity_saved = save_identity();
    const bool contacts_saved = save_contacts();
    const bool regions_saved = !regions_dirty || save_regions();
    return prefs_saved && channels_saved && identity_saved &&
           contacts_saved && regions_saved;
}

} // namespace detail
} // namespace mesh
} // namespace sigurdos
