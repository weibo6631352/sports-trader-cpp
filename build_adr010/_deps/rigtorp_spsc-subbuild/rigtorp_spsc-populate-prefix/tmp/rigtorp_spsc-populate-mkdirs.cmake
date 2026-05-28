# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/wangweibo/code/sports-trader-cpp/build_adr010/_deps/rigtorp_spsc-src")
  file(MAKE_DIRECTORY "/Users/wangweibo/code/sports-trader-cpp/build_adr010/_deps/rigtorp_spsc-src")
endif()
file(MAKE_DIRECTORY
  "/Users/wangweibo/code/sports-trader-cpp/build_adr010/_deps/rigtorp_spsc-build"
  "/Users/wangweibo/code/sports-trader-cpp/build_adr010/_deps/rigtorp_spsc-subbuild/rigtorp_spsc-populate-prefix"
  "/Users/wangweibo/code/sports-trader-cpp/build_adr010/_deps/rigtorp_spsc-subbuild/rigtorp_spsc-populate-prefix/tmp"
  "/Users/wangweibo/code/sports-trader-cpp/build_adr010/_deps/rigtorp_spsc-subbuild/rigtorp_spsc-populate-prefix/src/rigtorp_spsc-populate-stamp"
  "/Users/wangweibo/code/sports-trader-cpp/build_adr010/_deps/rigtorp_spsc-subbuild/rigtorp_spsc-populate-prefix/src"
  "/Users/wangweibo/code/sports-trader-cpp/build_adr010/_deps/rigtorp_spsc-subbuild/rigtorp_spsc-populate-prefix/src/rigtorp_spsc-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/wangweibo/code/sports-trader-cpp/build_adr010/_deps/rigtorp_spsc-subbuild/rigtorp_spsc-populate-prefix/src/rigtorp_spsc-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/wangweibo/code/sports-trader-cpp/build_adr010/_deps/rigtorp_spsc-subbuild/rigtorp_spsc-populate-prefix/src/rigtorp_spsc-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
