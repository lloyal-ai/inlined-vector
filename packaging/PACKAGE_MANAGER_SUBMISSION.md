# Package Manager Submission Guide

This document provides step-by-step instructions for submitting `inlined-vector` to major C++ package managers.

## Prerequisites

Before submitting, ensure:

1. ✅ **GitHub Release Created**: Tag `v5.7.0` must exist with release notes
2. ✅ **CI Passing**: All tests must be green on main branch
3. ✅ **License File**: `LICENSE` file exists in repository root
4. ✅ **CMake Package Config**: `inlined-vectorConfig.cmake` is generated correctly

---

## 1. vcpkg Submission

### Overview
vcpkg is Microsoft's C++ package manager with 2000+ packages.

### Files Created
- `vcpkg.json` - Package metadata
- `portfile.cmake` - Build instructions for vcpkg
- `usage` - Usage documentation for consumers

### Submission Steps

#### Step 1: Create GitHub Release
```bash
# Tag the release
git tag -a v5.7.0 -m "Release v5.7.0 - Allocator-Aware with Zero Overhead"
git push origin v5.7.0

# Create release on GitHub with release notes
```

#### Step 2: Calculate SHA512
```bash
# Download the release tarball
wget https://github.com/lloyal-ai/inlined-vector/archive/refs/tags/v5.7.0.tar.gz

# Calculate SHA512
sha512sum v5.7.0.tar.gz

# Update portfile.cmake with the actual SHA512
```

#### Step 3: Fork vcpkg Repository
```bash
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
git checkout -b add-inlined-vector
```

#### Step 4: Create Port Directory
```bash
mkdir -p ports/inlined-vector
cp /path/to/inlined-vector/vcpkg.json ports/inlined-vector/
cp /path/to/inlined-vector/portfile.cmake ports/inlined-vector/
cp /path/to/inlined-vector/usage ports/inlined-vector/
```

#### Step 5: Test Locally
```bash
# Bootstrap vcpkg (if not already done)
./bootstrap-vcpkg.sh

# Test the port
./vcpkg install inlined-vector --overlay-ports=./ports/inlined-vector

# Test usage
./vcpkg integrate install
```

#### Step 6: Submit Pull Request
```bash
git add ports/inlined-vector
git commit -m "[inlined-vector] new port"
git push origin add-inlined-vector
```

Then create a PR at: https://github.com/microsoft/vcpkg/pulls

**PR Title**: `[inlined-vector] new port`

**PR Description**:
```markdown
### Description
Adds inlined-vector v5.7.0 - A C++17/20 header-only vector with Small Buffer Optimization.

### Details
- **Type**: Header-only library
- **License**: MIT
- **Dependencies**: None
- **Tested on**: [list platforms tested]

### Features
- Zero external dependencies
- 13.2× faster than std::vector for inline operations
- Full allocator support (std::pmr compatible)
- Supports non-assignable types

### Checklist
- [x] License file included
- [x] Usage file provided
- [x] Tested locally on [platform]
- [x] All tests pass in CI
- [x] No dependencies required
```

**Expected Timeline**: 1-2 weeks for review

---

## 2. Conan Center Submission

### Overview
Conan is a decentralized C++ package manager with 1500+ packages.

### Files Created
- `conanfile.py` - Conan recipe
- `conandata.yml` - Source URLs and checksums

### Submission Steps

#### Step 1: Calculate SHA256
```bash
# Download the release tarball
wget https://github.com/lloyal-ai/inlined-vector/archive/refs/tags/v5.7.0.tar.gz

# Calculate SHA256
sha256sum v5.7.0.tar.gz

# Update conandata.yml with the actual SHA256
```

#### Step 2: Fork Conan Center Index
```bash
git clone https://github.com/conan-io/conan-center-index.git
cd conan-center-index
git checkout -b inlined-vector/5.7.0
```

#### Step 3: Create Recipe Directory
```bash
mkdir -p recipes/inlined-vector/all
cp /path/to/inlined-vector/conanfile.py recipes/inlined-vector/all/
cp /path/to/inlined-vector/conandata.yml recipes/inlined-vector/all/
```

#### Step 4: Create config.yml
```bash
cat > recipes/inlined-vector/config.yml << 'EOF'
versions:
  "5.7.0":
    folder: all
EOF
```

#### Step 5: Test Locally
```bash
# Install Conan (if not already installed)
pip install conan

# Test the recipe
conan create recipes/inlined-vector/all --version 5.7.0

# Test usage
conan install --requires=inlined-vector/5.7.0@ --build=missing
```

#### Step 6: Submit Pull Request
```bash
git add recipes/inlined-vector
git commit -m "inlined-vector/5.7.0: new recipe"
git push origin inlined-vector/5.7.0
```

Then create a PR at: https://github.com/conan-io/conan-center-index/pulls

**PR Title**: `(inlined-vector/5.7.0) new recipe`

**PR Description**:
```markdown
### Description
Adds inlined-vector 5.7.0 - A high-performance header-only vector with SBO.

### Recipe Details
- **Type**: Header-only library
- **License**: MIT
- **Homepage**: https://github.com/lloyal-ai/inlined-vector
- **Dependencies**: None (C++17 STL only)

### Key Features
- Zero external dependencies
- Production-ready (15/15 tests + 9/9 fuzz tests passing)
- ASan/UBSan clean
- 13.2× performance improvement over std::vector for small collections

### Testing
- [x] Tested on Linux
- [x] Tested on macOS
- [x] All CI checks passing
- [x] Documentation complete
```

**Expected Timeline**: 2-4 weeks for review (includes automated CI checks)

---

## 3. Other Package Managers

### Meson WrapDB
**Status**: Optional (for Meson build system users)

**Steps**:
1. Fork: https://github.com/mesonbuild/wrapdb
2. Create wrap file: `subprojects/inlined-vector.wrap`
3. Submit PR with wrap file

**Wrap File Template**:
```ini
[wrap-file]
directory = inlined-vector-5.7.0
source_url = https://github.com/lloyal-ai/inlined-vector/archive/refs/tags/v5.7.0.tar.gz
source_filename = inlined-vector-5.7.0.tar.gz
source_hash = <sha256>

[provide]
inlined-vector = inlined_vector_dep
```

### Hunter
**Status**: Optional (for CMake users)

Hunter packages are typically added by users via overlay, not central submission.

### Buckaroo
**Status**: Optional (less popular)

Buckaroo is less actively maintained. Consider skipping unless requested by users.

---

## 4. Post-Submission Tasks

### Monitor Pull Requests
- **vcpkg**: https://github.com/microsoft/vcpkg/pulls?q=inlined-vector
- **Conan**: https://github.com/conan-io/conan-center-index/pulls?q=inlined-vector

### Respond to Review Comments
Both package managers have automated CI and human reviewers. Common feedback:
- **vcpkg**: Portfile formatting, SHA512 verification, usage examples
- **Conan**: Recipe structure, package_info correctness, build testing

### Update Documentation
Once approved, update README.md with installation instructions:

```markdown
## Installation

### vcpkg
\`\`\`bash
vcpkg install inlined-vector
\`\`\`

### Conan
\`\`\`bash
conan install --requires=inlined-vector/5.7.0@
\`\`\`

### CMake FetchContent
\`\`\`cmake
FetchContent_Declare(
    inlined_vector
    GIT_REPOSITORY https://github.com/lloyal-ai/inlined-vector.git
    GIT_TAG v5.7.0
)
FetchContent_MakeAvailable(inlined_vector)
target_link_libraries(your_target PRIVATE inlined-vector::inlined-vector)
\`\`\`
```

---

## 5. Troubleshooting

### vcpkg Issues

**Problem**: SHA512 mismatch
```bash
# Recalculate SHA512
sha512sum v5.7.0.tar.gz
# Update portfile.cmake
```

**Problem**: Build fails
```bash
# Test locally with verbose output
./vcpkg install inlined-vector --overlay-ports=./ports/inlined-vector --debug
```

### Conan Issues

**Problem**: SHA256 mismatch
```bash
# Update conandata.yml with correct hash
sha256sum v5.7.0.tar.gz
```

**Problem**: Recipe validation fails
```bash
# Run Conan linter
conan export recipes/inlined-vector/all --version 5.7.0
```

---

## 6. Timeline Summary

| Package Manager | Submission Time | Review Time | Total |
|-----------------|-----------------|-------------|-------|
| **vcpkg** | 30 min | 1-2 weeks | ~2 weeks |
| **Conan Center** | 45 min | 2-4 weeks | ~4 weeks |
| **Meson WrapDB** | 20 min | 1 week | ~1 week |

**Priority Order**: vcpkg → Conan → Others

---

## 7. Success Criteria

✅ **vcpkg**: Port accepted, searchable via `vcpkg search inlined-vector`
✅ **Conan**: Recipe accepted, installable via `conan install inlined-vector/5.7.0@`
✅ **Documentation**: README updated with installation instructions
✅ **Badges**: Add package manager badges to README

**Badges to Add**:
```markdown
[![vcpkg](https://img.shields.io/vcpkg/v/inlined-vector)](https://vcpkg.link/ports/inlined-vector)
[![Conan Center](https://shields.io/conan/v/inlined-vector)](https://conan.io/center/recipes/inlined-vector)
```

---

## Need Help?

- **vcpkg**: https://github.com/microsoft/vcpkg/discussions
- **Conan**: https://github.com/conan-io/conan-center-index/discussions
- **General**: Create issue on https://github.com/lloyal-ai/inlined-vector/issues
