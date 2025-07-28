#!/bin/bash
echo "=== Starting compilation for x86_64 in MinSizeRel mode ==="

echo
echo "=== x86_64 Configuration (MinSizeRel) ==="
cmake -B build-x86_64 -DCMAKE_BUILD_TYPE=MinSizeRel .
if [ $? -ne 0 ]; then
    echo "Error during x86_64 configuration"
    exit 1
fi

echo
echo "=== x86_64 Compilation (MinSizeRel) ==="
cmake --build build-x86_64 --config MinSizeRel
if [ $? -ne 0 ]; then
    echo "Error during x86_64 compilation"
    exit 1
fi

# Create the destination directory if it doesn't exist
mkdir -p ../src/commonMain/resources/linux-x86-64

# Copy the compiled library to the resources directory
cp build-x86_64/libtray.so ../src/commonMain/resources/linux-x86-64/libtray.so
if [ $? -ne 0 ]; then
    echo "Error copying library to resources directory"
    exit 1
fi

echo
echo "=== Compilation completed successfully in MinSizeRel mode ==="
echo
echo "x86_64 library: ../src/commonMain/resources/linux-x86-64/libtray.so"