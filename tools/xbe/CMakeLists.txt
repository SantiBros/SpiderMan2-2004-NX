# The PC-side tools the build needs, for the computer that builds the .nro (Linux, or Windows with WSL):
#   xbe_recomp    the x86 to C++ recompiler (sdk/tools/recomp): writes generated/game from your default.xbe
#   xbe_extract   copies default.xbe out of a disc image (.iso .xiso .cci .cso, whole or split)
#   xbe_symbols   the XDK signature scanner that wrote config/game.toml (only for porting work)
# build.sh at the top of the repository configures and builds this; by hand:
#   cmake -S tools/xbe -B build-tools -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-tools
cmake_minimum_required(VERSION 3.20)
project(spiderman2_nx_tools CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(SDK ${CMAKE_CURRENT_LIST_DIR}/../../sdk ABSOLUTE)

include(FetchContent)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zydis GIT_REPOSITORY https://github.com/zyantific/zydis.git GIT_TAG v4.1.1 GIT_SHALLOW TRUE)
FetchContent_Declare(tomlplusplus GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git GIT_TAG v3.4.0 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(zydis tomlplusplus)

file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS ${SDK}/tools/recomp/*.cpp)
add_executable(xbe_recomp ${RECOMP_SOURCES})
target_include_directories(xbe_recomp PRIVATE ${SDK}/runtime ${SDK}/tools/recomp)
target_link_libraries(xbe_recomp PRIVATE Zydis tomlplusplus::tomlplusplus)

add_executable(xbe_symbols ${SDK}/tools/symbols/main.cpp)
target_include_directories(xbe_symbols PRIVATE ${SDK}/runtime)
target_link_libraries(xbe_symbols PRIVATE Zydis tomlplusplus::tomlplusplus)

# the port's own disc image reader, as the Switch build uses it
add_executable(xbe_extract xbe_extract.cpp ${SDK}/runtime/kernel/disc.cpp ${SDK}/runtime/kernel/image.cpp)
target_include_directories(xbe_extract PRIVATE ${SDK}/runtime)
find_package(Threads REQUIRED)
target_link_libraries(xbe_extract PRIVATE Threads::Threads)
