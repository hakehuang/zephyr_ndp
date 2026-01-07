cmake_minimum_required(VERSION 3.20.0)

get_filename_component(NDP_MODULE_DIR
	${CMAKE_CURRENT_LIST_DIR}/../../../modules/ndp
	ABSOLUTE
)
list(APPEND ZEPHYR_EXTRA_MODULES ${NDP_MODULE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ndp_audio_amp_cm33)

target_sources(app PRIVATE src/main.c)
