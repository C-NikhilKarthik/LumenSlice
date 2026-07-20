# Release-only variant of the built-in x64-windows triplet.
#
# The stock triplet builds every dependency (DCMTK is the expensive one) in BOTH
# debug and release configurations. LumenSlice CI only ships a Release build, so
# VCPKG_BUILD_TYPE=release skips the debug half and roughly halves the cold build
# time. Everything else matches x64-windows (dynamic CRT + dynamic libraries).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
