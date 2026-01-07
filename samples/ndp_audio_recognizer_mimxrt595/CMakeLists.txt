cmake_minimum_required(VERSION 3.20.0)

# Allow building this sample from this repository without requiring a west
# workspace manifest entry for the NDP module.
get_filename_component(NDP_MODULE_DIR
	${CMAKE_CURRENT_LIST_DIR}/../../modules/ndp
	ABSOLUTE
)
list(APPEND ZEPHYR_EXTRA_MODULES ${NDP_MODULE_DIR})

# This sample is meant to be built inside a Zephyr workspace.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ndp_audio_recognizer_mimxrt595)

target_sources(app PRIVATE src/main.c)
