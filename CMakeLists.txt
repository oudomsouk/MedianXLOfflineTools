cmake_minimum_required(VERSION 3.18)

project(MedianXLOfflineTools
	VERSION 0.6.6
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)

# dependencies

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core)
if(QT_FOUND)
	find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core REQUIRED)
	add_compile_definitions(IS_QT5=1)
else()
	find_package(Qt4 REQUIRED QtCore)
	add_library("Qt4::Core" ALIAS "Qt4::QtCore")
	set(QT_VERSION_MAJOR 4)
endif()

# target: CLI tool (view/respec-stats/respec-skills/backup-on-save). Links against QtCore only - no GUI.

add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x060000)
if(WIN32)
	add_compile_definitions(NOMINMAX)
	if(QT_VERSION_MAJOR LESS 6)
		add_compile_definitions(UNICODE _UNICODE)
	endif()
endif()

add_executable(mxl
	src/characterfile.cpp
	src/characterfile.h
	src/characterinfo.hpp
	src/enums.cpp
	src/enums.h
	src/helpers.h
	src/itemdatabase.cpp
	src/itemdatabase.h
	src/languagemanager.hpp
	src/main_cli.cpp
	src/resourcepathmanager.hpp
	src/reversebitreader.cpp
	src/reversebitreader.h
	src/structs.h
)
target_link_libraries(mxl PRIVATE "Qt${QT_VERSION_MAJOR}::Core")
target_compile_definitions(mxl PRIVATE
	"NVER1=${CMAKE_PROJECT_VERSION_MAJOR}"
	"NVER2=${CMAKE_PROJECT_VERSION_MINOR}"
	"NVER3=${CMAKE_PROJECT_VERSION_PATCH}"
	"NVER_STRING=\"${CMAKE_PROJECT_VERSION}\""
	"$<$<CONFIG:Debug>:DATA_PATH=\"${CMAKE_SOURCE_DIR}/resources\">"
)

# install

install(TARGETS mxl RUNTIME DESTINATION ".")
install(DIRECTORY "resources/data" DESTINATION ".")

# CPack

if(WIN32)
	set(CPACK_GENERATOR "7Z")
else()
	set(CPACK_GENERATOR "TXZ")
endif()
include(CPack)
