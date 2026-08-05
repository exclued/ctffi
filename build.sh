#!/bin/bash
# Build script for CTF-FFI Bridge demonstration

set -e

echo "=== CTF-FFI Bridge Build Script ==="
echo ""

# Check for required tools
check_tool() {
    if ! command -v $1 &> /dev/null; then
        echo "ERROR: $1 is required but not installed."
        exit 1
    fi
}

echo "Checking dependencies..."
check_tool gcc
check_tool pkg-config

# Check GCC version (CTF support requires GCC 10+ with binutils 2.37+)
GCC_VERSION=$(gcc --version | head -n1 | grep -oP '\d+\.\d+' | head -n1)
echo "GCC version: $GCC_VERSION"

# Try to detect libctf
echo ""
echo "Checking for libctf..."
if pkg-config --exists libctf 2>/dev/null; then
    LIBCTF_CFLAGS=$(pkg-config --cflags libctf)
    LIBCTF_LIBS=$(pkg-config --libs libctf)
    echo "libctf found via pkg-config"
    echo "  CFLAGS: $LIBCTF_CFLAGS"
    echo "  LIBS: $LIBCTF_LIBS"
    HAVE_LIBCTF=1
else
    # Try to find libctf manually
    if ldconfig -p | grep -q libctf; then
        LIBCTF_CFLAGS=""
        LIBCTF_LIBS="-lctf"
        echo "libctf found in system libraries"
        HAVE_LIBCTF=1
    else
        echo "WARNING: libctf not found. Building without CTF support."
        HAVE_LIBCTF=0
    fi
fi

# Check for libffi
echo ""
echo "Checking for libffi..."
if pkg-config --exists libffi; then
    LIBFFI_CFLAGS=$(pkg-config --cflags libffi)
    LIBFFI_LIBS=$(pkg-config --libs libffi)
    echo "libffi found"
    echo "  CFLAGS: $LIBFFI_CFLAGS"
    echo "  LIBS: $LIBFFI_LIBS"
else
    LIBFFI_CFLAGS=""
    LIBFFI_LIBS="-lffi"
    echo "Using default libffi flags"
fi

# Create build directory
BUILD_DIR="build"
mkdir -p $BUILD_DIR

echo ""
echo "=== Building Test Library ==="

# Compile test library with CTF debug info
# Note: CTF is generated automatically with -g in newer GCC versions
if [ $HAVE_LIBCTF -eq 1 ]; then
    echo "Compiling with CTF debug information..."
    gcc -shared -fPIC -g -o $BUILD_DIR/libtest_ctf.so test_library.c
else
    echo "Compiling without CTF (libctf not available)..."
    gcc -shared -fPIC -g -o $BUILD_DIR/libtest_ctf.so test_library.c
fi

echo "Test library built: $BUILD_DIR/libtest_ctf.so"

# Check if CTF section exists
echo ""
echo "Checking for CTF section in library..."
if readelf -S $BUILD_DIR/libtest_ctf.so 2>/dev/null | grep -q ctf; then
    echo "✓ CTF section found!"
    readelf -S $BUILD_DIR/libtest_ctf.so | grep ctf
else
    echo "✗ No CTF section found (this may be due to GCC/binutils version)"
    echo "  CTF support requires GCC 10+ and binutils 2.37+"
fi

echo ""
echo "=== Building CTF-FFI Bridge ==="

if [ $HAVE_LIBCTF -eq 1 ]; then
    # Build the bridge library
    gcc -c -fPIC -g $LIBCTF_CFLAGS $LIBFFI_CFLAGS \
        -DHAVE_LIBCTF -DDEBUG \
        -o $BUILD_DIR/ctf_ffi_bridge.o ctf_ffi_bridge.c
    
    # Build standalone test program
    gcc -DSTANDALONE_TEST -g $LIBCTF_CFLAGS $LIBFFI_CFLAGS \
        -o $BUILD_DIR/ctf_ffi_test ctf_ffi_bridge.c \
        $LIBCTF_LIBS $LIBFFI_LIBS -ldl
    
    echo "✓ CTF-FFI bridge built successfully"
    echo "  Test program: $BUILD_DIR/ctf_ffi_test"
else
    echo "Skipping bridge build (libctf not available)"
fi

echo ""
echo "=== Testing ==="

# Run the test library directly
echo "Running test library directly:"
LD_LIBRARY_PATH=$BUILD_DIR:$LD_LIBRARY_PATH \
    gcc -DBUILD_TEST_MAIN -o $BUILD_DIR/test_lib_exe test_library.c
$BUILD_DIR/test_lib_exe

echo ""
echo "=== Summary ==="
echo "Build artifacts in: $BUILD_DIR/"
ls -lh $BUILD_DIR/

echo ""
if [ $HAVE_LIBCTF -eq 1 ]; then
    echo "To test the CTF-FFI bridge, run:"
    echo "  LD_LIBRARY_PATH=./build ./build/ctf_ffi_test ./build/libtest_ctf.so add_numbers"
else
    echo "Install libctf to enable full CTF-FFI functionality:"
    echo "  Debian/Ubuntu: apt-get install binutils-dev"
    echo "  Fedora/RHEL: dnf install binutils-devel"
fi

echo ""
echo "Build complete!"
