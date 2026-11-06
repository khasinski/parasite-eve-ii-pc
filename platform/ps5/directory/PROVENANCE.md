# Native directory bridge

pe-game-directory.c, pe-native-directory-bridge.c and provider-headers/errno.h
derive from the public native PS5 support export in
[GUMMIBRI/parasite-eve-ps5](https://github.com/GUMMIBRI/parasite-eve-ps5),
`platform/ps5/native-gpu/support`. They contain directory enumeration/ABI
conversion, not PE1 save or gameplay code. The game-facing diagnostic comment
is generalized for this contribution; implementation is otherwise retained.
This is source reuse, not a clean-room claim. Existing component notices
remain applicable.

Exact contributed source SHA256:

| File | SHA256 |
| --- | --- |
| pe-game-directory.c | 1e20565406ec01e7a57d6e9ee11f3616b65f82beca512890f2abb7d74be06d4d |
| pe-native-directory-bridge.c | ffe64fca26ff755c8a9cad530226674216bea81dde50108467d2de3af909b923 |
| provider-headers/errno.h | 0b360bf11808bfa2a61473f0df3c429f158386f08c461ea638cfff9b4897ef63 |

The provider translation unit is built against the exact retained RADV SDK
headers matching libps5platform. The game-facing translation unit uses the
PE2 public SDK and never dereferences a provider DIR or provider dirent.
Names, types and inode values cross through scalar/copied values; EOF and
real errors remain distinct. The new wrapper routes opendir/readdir, while
the existing adapter supplies closedir/scandir wrappers.

PE2 memory_card.c still owns directory/card naming, save-file bytes, block
limits, temporary writes, fsync and atomic replacement. No save format,
PE1 memory card, settings file, original image or game data was copied.
