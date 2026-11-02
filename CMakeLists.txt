# Local integration modifications (banderson777, 2026); see LOCAL_CHANGES.md.
project(CFS_BPNODE C)

set(APP_SRC_FILES
  # App files
  fsw/src/bpnode_app.c
  fsw/src/bpnode_task.c
  fsw/src/bpnode_adu_out.c
  fsw/src/bpnode_adu_in.c
  fsw/src/bpnode_cla_in.c
  fsw/src/bpnode_cla_out.c
  fsw/src/bpnode_video_sink.c
  fsw/src/bpnode_gen_worker.c
  fsw/src/bpnode_notif.c
  fsw/src/bpnode_maint.c

  # FWP files
  fsw/fwp/fwp_timep.c
  fsw/fwp/fwp_tablep.c
  fsw/fwp/fwp_evp.c
  fsw/fwp/fwp_adup.c
  fsw/fwp/fwp_perflogp.c
  fsw/fwp/fwp_dp.c
  fsw/fwp/fwp_tlmp.c
  fsw/fwp/fwp_utils.c
  fsw/fwp/fwp_clap.c
)

set(APP_TABLE_FILES
  fsw/tables/bpnode_adup.c
  fsw/tables/bpnode_channel.c
  fsw/tables/bpnode_contacts.c
  fsw/tables/bpnode_crs.c
  fsw/tables/bpnode_custodia.c
  fsw/tables/bpnode_custody.c
  fsw/tables/bpnode_mib_pn.c
  fsw/tables/bpnode_mib_ps.c
  fsw/tables/bpnode_reportto.c
  fsw/tables/bpnode_src_auth.c
  fsw/tables/bpnode_latency.c
  fsw/tables/bpnode_storage.c
)

# Create the app module
add_cfe_app(bpnode ${APP_SRC_FILES})
target_sources(bpnode PRIVATE
  ../integration/bplib_tcp_cla.c
  ../integration/bplib_tcp_cla_bplib.c)

target_include_directories(bpnode PUBLIC fsw/inc)
target_include_directories(bpnode PUBLIC fsw/fwp)
target_include_directories(bpnode PUBLIC fsw/src)
target_include_directories(bpnode PUBLIC ../integration)

# Include bplib
add_cfe_app_dependency(bpnode bplib)

# Add table
add_cfe_tables(bpnode ${APP_TABLE_FILES})

# If UT is enabled, then add the tests from the subdirectory
# Note that this is an app, and therefore does not provide
# stub functions, as other entities would not typically make
# direct function calls into this application.
if (ENABLE_UNIT_TESTS)
  add_subdirectory(unit-test)
endif (ENABLE_UNIT_TESTS)

# The lab build explicitly opts into autonomous five-node configuration.
if (BPNODE_ENABLE_FIVE_NODE_DEMO)
  target_sources(bpnode PRIVATE fsw/src/bpnode_demo.c)
  target_include_directories(bpnode PRIVATE ../bplib/bpa/ct/src)
  target_compile_definitions(bpnode PRIVATE BPNODE_ENABLE_FIVE_NODE_DEMO)
  target_link_libraries(bpnode ${BPNODE_DEMO_SECURITY_LIBRARY})
endif()
