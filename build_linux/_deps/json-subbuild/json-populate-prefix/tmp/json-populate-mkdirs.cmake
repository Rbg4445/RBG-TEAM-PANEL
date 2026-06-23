# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/workspace/build_linux/_deps/json-src"
  "/workspace/build_linux/_deps/json-build"
  "/workspace/build_linux/_deps/json-subbuild/json-populate-prefix"
  "/workspace/build_linux/_deps/json-subbuild/json-populate-prefix/tmp"
  "/workspace/build_linux/_deps/json-subbuild/json-populate-prefix/src/json-populate-stamp"
  "/workspace/build_linux/_deps/json-subbuild/json-populate-prefix/src"
  "/workspace/build_linux/_deps/json-subbuild/json-populate-prefix/src/json-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/workspace/build_linux/_deps/json-subbuild/json-populate-prefix/src/json-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/workspace/build_linux/_deps/json-subbuild/json-populate-prefix/src/json-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
