# Changes

## 0.1.1 — 07/10/2026

- Remove residual provider-specific references from the common native source
  and active integration documentation. The 0.1.0 common module already had no
  frame-generation engines or controls; those belonged to the AC7 consumer.
- Use the common VERSION in both CMake entry points and add a Windows DLL
  version resource. Host package is 0.1.1 and manifest implementation is 2.
- Add an operational-source and delivery audit, and an allowlisted consumer
  kit with per-file SHA-256 hashes. Keep the surface ABI and OpenXR behavior.
- Document consumer migration separately. Carlos accepted the AC7 0.8.5-r2
  integration and this module's close on 2026-10-07. Its package contains the
  exact tested 0.1.1 DLL. This does not certify other consumers or headsets.
- Release sources and the documentation-updated kit only in the private
  repository under v0.1.1. The native DLL and operational sources are unchanged
  from the tested local kit; the original kit identity remains in its receipt.

## 0.1.0 — 05/10/2026

Initial common-module extraction from AC7 0.8.3-r2. Historical source hashes
and the accepted origin DLL are retained in `provenance.json` and the v0.1.0 tag.
