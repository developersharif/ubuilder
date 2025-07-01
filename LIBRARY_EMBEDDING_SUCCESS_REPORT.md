# UBuilder Library Embedding Requirements

**Date:** July 1, 2025  
**Purpose:** Documentation of required shared libraries for each runtime to ensure portability

## Overview

This document specifies which shared libraries must be bundled with each runtime to ensure UBuilder-generated executables work in minimal environments (Docker containers, Alpine Linux, etc.) where system libraries may be missing or incompatible.

## Required Libraries by Runtime

### Python Runtime

**Required Libraries:**

- `libz.so.1` - Compression library (zlib)
- `libexpat.so.1` - XML parsing library

**Library Details:**

```
libz.so.1        -> /lib/x86_64-linux-gnu/libz.so.1.3.1      (121,272 bytes / ~118 KB)
libexpat.so.1    -> /lib/x86_64-linux-gnu/libexpat.so.1.10.2 (186,704 bytes / ~182 KB)
```

**Total Size:** ~300 KB

**Analysis Command:**

```bash
ldd /usr/bin/python3 | grep -E "(libz|libexpat)"
```

**Why These Libraries:**

- `libz.so.1`: Required for Python's built-in compression modules (gzip, zlib)
- `libexpat.so.1`: Required for Python's XML parsing capabilities (xml.parsers.expat)

---

### Node.js Runtime

**Required Libraries:**

- `libdl.so.2` - Dynamic linking interface
- `libstdc++.so.6` - GNU Standard C++ Library
- `libgcc_s.so.1` - GCC runtime library

**Library Details:**

```
libdl.so.2       -> /lib/x86_64-linux-gnu/libdl.so.2         (14,488 bytes / ~14 KB)
libstdc++.so.6   -> /lib/x86_64-linux-gnu/libstdc++.so.6.0.34 (2,653,696 bytes / ~2.5 MB)
libgcc_s.so.1    -> /lib/x86_64-linux-gnu/libgcc_s.so.1      (178,928 bytes / ~175 KB)
```

**Total Size:** ~2.8 MB

**Analysis Command:**

```bash
ldd $(which node) | grep -E "(libdl|libstdc|libgcc)"
```

**Why These Libraries:**

- `libdl.so.2`: Required for dynamic loading of native modules
- `libstdc++.so.6`: Node.js is built with C++, requires standard library
- `libgcc_s.so.1`: Runtime support for GCC-compiled code

---

### PHP Runtime

**Required Libraries:**

- `libxml2.so.2` - XML C parser and toolkit
- `libz.so.1` - Compression library (zlib)
- `libssl.so.3` - Secure Sockets Layer toolkit
- `libcrypto.so.3` - Cryptographic library
- `libpcre2-8.so.0` - Perl Compatible Regular Expressions v2
- `libsodium.so.23` - Modern cryptographic library
- `libargon2.so.1` - Password hashing library

**Library Details:**

```
libxml2.so.2     -> /lib/x86_64-linux-gnu/libxml2.so.2.9.14   (2,012,552 bytes / ~1.9 MB)
libz.so.1        -> /lib/x86_64-linux-gnu/libz.so.1.3.1       (121,272 bytes / ~118 KB)
libssl.so.3      -> /lib/x86_64-linux-gnu/libssl.so.3         (1,093,800 bytes / ~1.0 MB)
libcrypto.so.3   -> /lib/x86_64-linux-gnu/libcrypto.so.3      (6,111,160 bytes / ~5.8 MB)
libpcre2-8.so.0  -> /lib/x86_64-linux-gnu/libpcre2-8.so.0.14.0 (776,880 bytes / ~759 KB)
libsodium.so.23  -> /lib/x86_64-linux-gnu/libsodium.so.23.3.0  (355,040 bytes / ~347 KB)
libargon2.so.1   -> /lib/x86_64-linux-gnu/libargon2.so.1       (34,808 bytes / ~34 KB)
```

**Total Size:** ~10.3 MB

**Analysis Command:**

```bash
ldd /usr/bin/php | grep -E "(libxml2|libz|libssl|libcrypto|libpcre2|libsodium|libargon2)"
```

**Why These Libraries:**

- `libxml2.so.2`: Core XML functionality, DOM, SimpleXML, XMLReader/Writer
- `libz.so.1`: Compression support (gzip, deflate)
- `libssl.so.3` + `libcrypto.so.3`: HTTPS, SSL/TLS, cryptographic functions
- `libpcre2-8.so.0`: Regular expression engine
- `libsodium.so.23`: Modern crypto functions (password_hash, etc.)
- `libargon2.so.1`: Password hashing algorithm (PASSWORD_ARGON2ID)

---

## Library Detection Strategy

### Standard Search Paths (Linux)

```c
const char* search_paths[] = {
    "/lib/x86_64-linux-gnu/",     // Debian/Ubuntu primary
    "/usr/lib/x86_64-linux-gnu/", // Debian/Ubuntu secondary
    "/lib64/",                    // RHEL/CentOS/Fedora
    "/usr/lib64/",                // RHEL/CentOS/Fedora secondary
    "/usr/lib/",                  // Generic Unix
    "/lib/",                      // Generic Unix
    NULL
};
```

### Symlink Resolution

Most libraries are accessed via symlinks (e.g., `libz.so.1` → `libz.so.1.3.1`). The embedding system must resolve symlinks to the actual library files.

### Size Optimization Opportunities

**Python:** Already minimal (300KB total)

- Only 2 libraries needed
- Both are core dependencies

**Node.js:** Moderate size (2.8MB total)

- `libstdc++.so.6` is the largest (2.5MB)
- Could explore static linking for smaller footprint

**PHP:** Largest footprint (10.3MB total)

- `libcrypto.so.3` dominates (5.8MB)
- Could make SSL support optional
- Could reduce to core libraries only: libxml2 + libz (~2MB)

## Implementation Priority

### Phase 1: Critical Libraries (Minimal Functionality)

- **Python:** `libz.so.1`, `libexpat.so.1`
- **Node.js:** `libdl.so.2`, `libgcc_s.so.1`
- **PHP:** `libxml2.so.2`, `libz.so.1`

### Phase 2: Enhanced Libraries (Full Functionality)

- **Python:** No additional libraries needed
- **Node.js:** `libstdc++.so.6`
- **PHP:** `libpcre2-8.so.0`

### Phase 3: Security Libraries (Production Use)

- **Python:** No additional libraries needed
- **Node.js:** No additional libraries needed
- **PHP:** `libssl.so.3`, `libcrypto.so.3`, `libsodium.so.23`, `libargon2.so.1`

## Compatibility Matrix

| Runtime | Minimal Env | Docker | Alpine | BusyBox | Ubuntu |
| ------- | ----------- | ------ | ------ | ------- | ------ |
| Python  | ✅          | ✅     | ⚠️     | ⚠️      | ✅     |
| Node.js | ✅          | ✅     | ⚠️     | ⚠️      | ✅     |
| PHP     | ✅          | ✅     | ⚠️     | ⚠️      | ✅     |

**Legend:**

- ✅ Works with embedded libraries
- ⚠️ GLIBC compatibility issues (expected)
- ❌ Not compatible

## Testing Commands

### Verify Dependencies

```bash
# Check what libraries a runtime needs
ldd /usr/bin/python3
ldd $(which node)
ldd /usr/bin/php

# Check if library exists
ls -la /lib/x86_64-linux-gnu/libz.so.1*

# Test in minimal environment
docker run --rm -v $(pwd):/test alpine:latest /test/executable
```

### Debug Missing Libraries

```bash
# See what's missing when execution fails
strace -e trace=openat ./executable 2>&1 | grep "\.so"
```

---

## Notes

1. **Architecture Specific:** These paths are for x86_64 Linux. ARM64 would use `/lib/aarch64-linux-gnu/`.

2. **Version Dependencies:** Library versions may vary between distributions. The embedding system should handle version differences gracefully.

3. **Static vs Dynamic:** This document focuses on dynamic library embedding. Static linking would eliminate these dependencies but requires different build infrastructure.

4. **Minimal Sets:** For ultra-minimal deployments, consider implementing configurable library sets to reduce executable size.

5. **Security Considerations:** Always embed the same library versions that were used during runtime testing to avoid compatibility issues.
