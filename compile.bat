@echo off
echo ===== Start building... =====

set VCPKG_ROOT=D:\vcpkg

set BUILD_DIR=build

mkdir %BUILD_DIR%
cd %BUILD_DIR%

echo ===== Configuration =====
cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DCMAKE_BUILD_TYPE=Release

echo ===== Compilation =====
cmake --build . --config Release

cd ..
echo ===== Build completed! =====
pause