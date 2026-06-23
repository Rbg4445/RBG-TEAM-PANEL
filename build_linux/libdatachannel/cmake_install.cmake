# Install script for directory: /workspace/deps/libdatachannel

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/x86_64-linux-gnu" TYPE STATIC_LIBRARY FILES "/workspace/build_linux/libdatachannel/deps/usrsctp/usrsctplib/libusrsctp.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/x86_64-linux-gnu" TYPE STATIC_LIBRARY FILES "/workspace/build_linux/libdatachannel/deps/libsrtp/libsrtp2.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/x86_64-linux-gnu" TYPE STATIC_LIBRARY FILES "/workspace/build_linux/libdatachannel/deps/libjuice/libjuice.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/x86_64-linux-gnu" TYPE STATIC_LIBRARY FILES "/workspace/build_linux/libdatachannel/libdatachannel.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/rtc" TYPE FILE FILES
    "/workspace/deps/libdatachannel/include/rtc/candidate.hpp"
    "/workspace/deps/libdatachannel/include/rtc/channel.hpp"
    "/workspace/deps/libdatachannel/include/rtc/configuration.hpp"
    "/workspace/deps/libdatachannel/include/rtc/datachannel.hpp"
    "/workspace/deps/libdatachannel/include/rtc/dependencydescriptor.hpp"
    "/workspace/deps/libdatachannel/include/rtc/description.hpp"
    "/workspace/deps/libdatachannel/include/rtc/iceudpmuxlistener.hpp"
    "/workspace/deps/libdatachannel/include/rtc/mediahandler.hpp"
    "/workspace/deps/libdatachannel/include/rtc/rtcpreceivingsession.hpp"
    "/workspace/deps/libdatachannel/include/rtc/common.hpp"
    "/workspace/deps/libdatachannel/include/rtc/global.hpp"
    "/workspace/deps/libdatachannel/include/rtc/message.hpp"
    "/workspace/deps/libdatachannel/include/rtc/frameinfo.hpp"
    "/workspace/deps/libdatachannel/include/rtc/peerconnection.hpp"
    "/workspace/deps/libdatachannel/include/rtc/reliability.hpp"
    "/workspace/deps/libdatachannel/include/rtc/rtc.h"
    "/workspace/deps/libdatachannel/include/rtc/rtc.hpp"
    "/workspace/deps/libdatachannel/include/rtc/rtp.hpp"
    "/workspace/deps/libdatachannel/include/rtc/track.hpp"
    "/workspace/deps/libdatachannel/include/rtc/websocket.hpp"
    "/workspace/deps/libdatachannel/include/rtc/websocketserver.hpp"
    "/workspace/deps/libdatachannel/include/rtc/rtppacketizationconfig.hpp"
    "/workspace/deps/libdatachannel/include/rtc/video_layers_allocation.hpp"
    "/workspace/deps/libdatachannel/include/rtc/rtcpsrreporter.hpp"
    "/workspace/deps/libdatachannel/include/rtc/rtppacketizer.hpp"
    "/workspace/deps/libdatachannel/include/rtc/rtpdepacketizer.hpp"
    "/workspace/deps/libdatachannel/include/rtc/h264rtppacketizer.hpp"
    "/workspace/deps/libdatachannel/include/rtc/h264rtpdepacketizer.hpp"
    "/workspace/deps/libdatachannel/include/rtc/nalunit.hpp"
    "/workspace/deps/libdatachannel/include/rtc/h265rtppacketizer.hpp"
    "/workspace/deps/libdatachannel/include/rtc/h265rtpdepacketizer.hpp"
    "/workspace/deps/libdatachannel/include/rtc/h265nalunit.hpp"
    "/workspace/deps/libdatachannel/include/rtc/av1rtppacketizer.hpp"
    "/workspace/deps/libdatachannel/include/rtc/vp8rtppacketizer.hpp"
    "/workspace/deps/libdatachannel/include/rtc/vp8rtpdepacketizer.hpp"
    "/workspace/deps/libdatachannel/include/rtc/vp9rtppacketizer.hpp"
    "/workspace/deps/libdatachannel/include/rtc/vp9rtpdepacketizer.hpp"
    "/workspace/deps/libdatachannel/include/rtc/rtcpnackresponder.hpp"
    "/workspace/deps/libdatachannel/include/rtc/utils.hpp"
    "/workspace/deps/libdatachannel/include/rtc/plihandler.hpp"
    "/workspace/deps/libdatachannel/include/rtc/pacinghandler.hpp"
    "/workspace/deps/libdatachannel/include/rtc/rembhandler.hpp"
    "/workspace/deps/libdatachannel/include/rtc/rtcpapphandler.hpp"
    "/workspace/deps/libdatachannel/include/rtc/version.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/x86_64-linux-gnu/cmake/LibDataChannel/LibDataChannelTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/x86_64-linux-gnu/cmake/LibDataChannel/LibDataChannelTargets.cmake"
         "/workspace/build_linux/libdatachannel/CMakeFiles/Export/b1aeee85b204851073eb9780073c3bd8/LibDataChannelTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/x86_64-linux-gnu/cmake/LibDataChannel/LibDataChannelTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/x86_64-linux-gnu/cmake/LibDataChannel/LibDataChannelTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/x86_64-linux-gnu/cmake/LibDataChannel" TYPE FILE FILES "/workspace/build_linux/libdatachannel/CMakeFiles/Export/b1aeee85b204851073eb9780073c3bd8/LibDataChannelTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/x86_64-linux-gnu/cmake/LibDataChannel" TYPE FILE FILES "/workspace/build_linux/libdatachannel/CMakeFiles/Export/b1aeee85b204851073eb9780073c3bd8/LibDataChannelTargets-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/x86_64-linux-gnu/cmake/LibDataChannel" TYPE FILE FILES
    "/workspace/build_linux/LibDataChannelConfig.cmake"
    "/workspace/build_linux/LibDataChannelConfigVersion.cmake"
    )
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.

endif()

