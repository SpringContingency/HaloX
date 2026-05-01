#pragma once

// Per-instance kernel-object namespace isolation.
//
// Two halox processes loading the same haloreach.dll on one machine collide
// on every named mutex / event / semaphore / file-mapping the engine uses
// for singleton coordination. The second process either fails to acquire
// the resource (silent hang) or wins it from the first (state corruption).
//
// instance_isolation hooks the Win32 named-object APIs and prefixes every
// non-null name with "halox<INSTANCE>_" so each halox process operates in
// its own private namespace. INSTANCE comes from the HALOX_INSTANCE env
// var (set by launch_A.bat / launch_B.bat). When unset, the suffix is the
// process ID (so single-instance dev runs still get an isolated namespace
// but never collide with other halox runs).
//
// Install at startup BEFORE haloreach.dll is loaded — once Reach has
// already CreateMutex'd, renaming on subsequent calls is too late.

namespace halox::network {

// Idempotent. Safe to call multiple times.
void instance_isolation_install();

} // namespace halox::network
