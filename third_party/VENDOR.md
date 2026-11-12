# Native voice dependencies

`third_party/libdave` is the upstream Discord libdave C API source (MIT, see LICENSE),
vendored from discord/libdave commit `6396c90` with its `cpp/vcpkg` submodule and build
directory removed. `third_party/voice` holds the DLLs produced from it.

- libdave: built from this tree as a shared library, OpenSSL 3.0.7 and mlspp statically linked.
- libsodium 1.0.20: `crypto_aead_xchacha20poly1305_ietf_*` for `aead_xchacha20_poly1305_rtpsize`.
- opus 1.5.2: Opus encode/decode.

Rebuild with `tools/build-voice-deps.ps1`; it fetches vcpkg and installs the pinned
versions and copies the DLLs plus `dave.h` into `third_party/voice`.

SHA-256 of the vendored DLLs:

```
74DDB8BC096B2AE08CE13B5A8670E8295847F2D3B43FC94B5023D826796AA553  libdave.dll
63B84A6EE3FDD11718785B4CED57A8261C99D83D23855AE79D44DDA2C355ABE6  libsodium.dll
A5A25F5FE777157AA5E43684EA64C968420C2E621D9170FC1171331D8652810B  opus.dll
```
