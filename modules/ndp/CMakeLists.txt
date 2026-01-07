cmake_minimum_required(VERSION 3.20.0)

if (NOT DEFINED ZEPHYR_BASE)
  message(FATAL_ERROR "This project is a Zephyr module and must be built via Zephyr")
endif()

include(${ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(ndp_module)

include(modules.cmake)
