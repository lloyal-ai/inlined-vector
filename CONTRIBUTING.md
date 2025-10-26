# Contributing to inlined-vector

Thank you for your interest in contributing! This document provides guidelines for contributing to the project.

## Ways to Contribute

- **Bug Reports**: File issues with reproduction steps
- **Feature Requests**: Propose new functionality with use cases
- **Code Contributions**: Submit pull requests with tests
- **Documentation**: Improve README, examples, or comments
- **Package Managers**: Help submit to vcpkg, Conan, etc.
- **Performance**: Benchmark improvements or optimizations

## Development Setup

### Prerequisites
- C++17 or C++20 compiler (GCC 9+, Clang 7+, AppleClang 13+, MSVC 2017+)
- CMake 3.14+
- (Optional) Google FuzzTest for fuzz testing
- (Optional) Google Benchmark for performance testing

### Building and Testing

```bash
# Clone the repository
git clone https://github.com/lloyal-ai/inlined-vector.git
cd inlined-vector

# Build and run tests
cmake -B build -DINLINED_VECTOR_BUILD_TESTS=ON
cmake --build build
./build/test_inlined_vector

# Run with sanitizers (recommended)
cmake -B build_sanitized \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
  -DINLINED_VECTOR_BUILD_TESTS=ON
cmake --build build_sanitized
./build_sanitized/test_inlined_vector

# Run fuzz tests (optional, requires FuzzTest)
cmake -B build_fuzz -DINLINED_VECTOR_BUILD_FUZZ_TESTS=ON
cmake --build build_fuzz
./build_fuzz/fuzz_inlined_vector

# Run benchmarks (optional, requires Google Benchmark)
cmake -B build_bench -DINLINED_VECTOR_BUILD_BENCHMARKS=ON
cmake --build build_bench
./build_bench/bench_inlined_vector
```

## Code Contributions

### Before You Start
1. **Check existing issues** for similar proposals
2. **Open an issue** to discuss major changes
3. **Keep PRs focused** - one feature/fix per PR

### Code Standards
- **C++17 minimum**: Avoid C++20-only features unless in `#ifdef __cpp_*` guards
- **Header-only**: All code in `include/inlined_vector.hpp`
- **No dependencies**: STL only
- **Follow existing style**: Match surrounding code formatting
- **Comment non-obvious code**: Especially complex template metaprogramming

### Testing Requirements
All PRs must include:
- ✅ **Unit tests pass** (15/15 tests)
- ✅ **Fuzz tests pass** (9/9 tests)
- ✅ **ASan/UBSan clean** (zero violations)
- ✅ **New tests** for new functionality
- ✅ **CI passes** on all platforms (Linux, macOS, Windows)

### Performance Considerations
- Benchmark changes that affect performance
- Document trade-offs in PR description
- Maintain competitive performance with `std::vector` and peers

### Pull Request Process
1. **Fork and branch**: `git checkout -b feature/your-feature-name`
2. **Make changes**: Follow code standards
3. **Add tests**: Cover new functionality
4. **Run locally**: Ensure all tests pass with sanitizers
5. **Commit**: Use clear, descriptive commit messages
6. **Push and PR**: Target `main` branch
7. **CI**: Wait for automated checks
8. **Review**: Address feedback

### Commit Message Format
```
type: Brief description (50 chars or less)

Detailed explanation of what and why (wrap at 72 chars).

Fixes #123
```

**Types**: `feat`, `fix`, `docs`, `perf`, `refactor`, `test`, `ci`, `style`

**Examples**:
```
feat: Add support for custom comparison operators
fix: Correct parent pointer retargeting in move assignment
perf: Optimize inline insert path for trivial types
docs: Update benchmark results for v5.7.0
```

## Bug Reports

When filing issues, include:
- **Minimal reproduction**: Code that demonstrates the bug
- **Expected behavior**: What should happen
- **Actual behavior**: What actually happens
- **Environment**: OS, compiler, compiler version
- **Context**: Use case that triggered the bug

**Example**:
```cpp
// Minimal reproduction
#include "inlined_vector.hpp"
lloyal::InlinedVector<int, 4> vec;
vec.insert(vec.begin(), 42);  // Crashes here

// Expected: Insert succeeds
// Actual: Segmentation fault
// Environment: Ubuntu 22.04, GCC 12.3, x86_64
```

## Feature Requests

Provide:
- **Use case**: Why is this needed?
- **API proposal**: How should it look?
- **Alternatives**: What did you consider?
- **Breaking changes**: Will it affect existing users?

## Package Manager Contributions

Help package `inlined-vector` for distribution:

### Release Checksums
```
SHA256: 81cd33f372db7fcde212902b830815b74c4c9147b884e439c205701cd4cdbd08
SHA512: 45cb97f18053fa3079b4014cdcd5f4ecb1c508ec30160baf04026934e43cf18e67688d70f5499147a208685fcdace587455555a3696ce3aeecf5bc99257d3fc7
```

### vcpkg Port
Repository: https://github.com/microsoft/vcpkg

Required files (see local package manager files for templates):
- `vcpkg.json` - Package metadata
- `portfile.cmake` - Build instructions
- `usage` - Usage documentation

Submit PR with title: `[inlined-vector] new port`

### Conan Recipe
Repository: https://github.com/conan-io/conan-center-index

Required files:
- `conanfile.py` - Package recipe
- `conandata.yml` - Source URLs and checksums
- `config.yml` - Version mapping

Submit PR with title: `(inlined-vector/5.7.0) new recipe`

### Other Package Managers
- **Meson WrapDB**: https://github.com/mesonbuild/wrapdb
- **Hunter**: User-managed overlays
- **Buckaroo**: Less actively maintained

## Documentation Contributions

Help improve:
- **README.md**: Usage examples, API documentation
- **Code comments**: Explain complex algorithms
- **Examples**: Real-world use cases
- **Wiki**: Tutorials, migration guides

## Performance Optimization

When proposing optimizations:
1. **Benchmark first**: Use `bench/bench_inlined_vector.cpp`
2. **Show results**: Before/after numbers
3. **Document trade-offs**: What's gained, what's lost
4. **Test thoroughly**: Ensure correctness preserved

## Code of Conduct

### Be Respectful
- Constructive feedback
- Assume good intentions
- Focus on the code, not the person

### Be Collaborative
- Help reviewers understand your changes
- Accept feedback gracefully
- Iterate based on review comments

### Be Patient
- Reviews take time
- Maintainers are volunteers
- Complex PRs require thorough review

## Questions?

- **Issues**: https://github.com/lloyal-ai/inlined-vector/issues
- **Discussions**: https://github.com/lloyal-ai/inlined-vector/discussions
- **Email**: noreply@lloyal.ai (for security issues)

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
