# NDP Zephyr module
zephyr_library()
zephyr_library_include_directories(include)

zephyr_library_sources_ifdef(CONFIG_NDP
  src/ndp_graph.c
  src/ndp_stage.c
  src/ndp_sched.c
  src/ndp_channel.c
  src/ndp_buffer.c
  src/ndp_metrics.c
)

zephyr_library_sources_ifdef(CONFIG_NDP_BACKEND_AMP
  src/ndp_amp_shm.c
)
