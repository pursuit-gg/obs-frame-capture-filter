cmake_minimum_required(VERSION 3.0.0)
PROJECT(frame-capture-filter)

SET(PATH_OBS_STUDIO "" CACHE PATH "OBS Studio Source Code Directory")
SET(obsPath "${PATH_OBS_STUDIO}")
INCLUDE("${PATH_OBS_STUDIO}/cmake/external/Findlibobs.cmake")

FIND_LIBRARY(LIBJPEG_LIBRARY
  NAMES jpeg
  HINTS
    ${PROJECT_SOURCE_DIR}
  PATH_SUFFIXES
    libjpeg/x64/Release
)

SET(frame-capture-filter_SOURCES
  frame-capture-filter.c)

INCLUDE_DIRECTORIES(
  "${PROJECT_SOURCE_DIR}"
  "${LIBOBS_INCLUDE_DIRS}"
)

ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)

ADD_LIBRARY(frame-capture-filter MODULE
  ${frame-capture-filter_SOURCES}
)

TARGET_LINK_LIBRARIES(frame-capture-filter
  ${LIBOBS_LIBRARIES}
  ${LIBJPEG_LIBRARY}
)
