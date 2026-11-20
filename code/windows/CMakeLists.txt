set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_sources_from_current_dir(utils
  utils/agent_mutex_name.c
  utils/agent_named_pipe_name.c
  utils/arm_arch_queries.c
  utils/aux_match_opt.c
  utils/blink_time.c
  utils/centre_window.c
  utils/cmdline_arg.c
  utils/cryptoapi.c
  utils/defaults.c
  utils/dll_hijacking_protection.c
  utils/dputs.c
  utils/escape_registry_key.c
  utils/exec_browser.c
  utils/filename.c
  utils/fontspec.c
  utils/getdlgitemtext_alloc.c
  utils/get_module_filename.c
  utils/get_system_dir.c
  utils/get_username.c
  utils/gui-timing.c
  utils/interprocess_mutex.c
  utils/is_console_handle.c
  utils/load_system32_dll.c
  utils/ltime.c
  utils/makedlgitemborderless.c
  utils/make_spr_sw_abort_winerror.c
  utils/message_box.c
  utils/minefield.c
  utils/open_for_write_would_lose_data.c
  utils/pgp_fingerprints_msgbox.c
  utils/platform_get_x_display.c
  utils/registry.c
  utils/request_file.c
  utils/screenshot.c
  utils/security.c
  utils/shinydialogbox.c
  utils/split_into_argv.c
  utils/split_into_argv_w.c
  utils/subprocess_waiter.c
  utils/version.c
  utils/win_strerror.c
  utils/app_conf.c
  utils/sleep_ticks.c
  winl10n.c
# winl10n must be used for CLI too. nol10n.c cannot query connection sharing with text translation.
  unicode.c)
if(NOT HAVE_STRTOUMAX)
  add_sources_from_current_dir(utils utils/strtoumax.c)
endif()
add_sources_from_current_dir(eventloop
  cliloop.c handle-wait.c)
add_sources_from_current_dir(console
  select-cli.c nohelp.c console.c)
add_sources_from_current_dir(settings
  ini.c
  storage.c)
add_sources_from_current_dir(network
  network.c handle-socket.c named-pipe-client.c named-pipe-server.c local-proxy.c x11.c)
add_sources_from_current_dir(sshcommon
  noise.c)
add_sources_from_current_dir(sshclient
  agent-client.c gss.c sharing.c)
add_sources_from_current_dir(sftpclient
  sftp.c)
add_sources_from_current_dir(otherbackends
  serial.c)
add_sources_from_current_dir(agent
  agent-client.c)
add_sources_from_current_dir(guiterminal
  wtrans.c
  dialog.c controls.c config.c printing.c jump-list.c sizetip.cpp)
add_dependencies(guiterminal generated_licence_h) # dialog.c uses licence.h

# This object awkwardly needs to live in the network library as well
# as the eventloop library, in case it didn't get pulled in from the
# latter before handle-socket.c needed it.
add_library(handle-io OBJECT
  handle-io.c)
target_sources(eventloop PRIVATE $<TARGET_OBJECTS:handle-io>)
target_sources(network PRIVATE $<TARGET_OBJECTS:handle-io>)

add_library(guimisc STATIC
  select-gui.c)

add_executable(pageant
  pageant.c
  help.c
  ini.c
  ${CMAKE_SOURCE_DIR}/stubs/no-iso2022.c
  pageant.rc)
add_dependencies(pageant generated_licence_h)
target_link_libraries(pageant
  guimisc eventloop agent network crypto utils
  iso2022
  ${platform_libraries})
set_target_properties(pageant PROPERTIES WIN32_EXECUTABLE ON)
installed_program(pageant)

add_sources_from_current_dir(plink no-jump-list.c nohelp.c plink.rc)
add_dependencies(plink generated_licence_h)

add_sources_from_current_dir(pscp no-jump-list.c nohelp.c pscp.rc)
add_dependencies(pscp generated_licence_h)

add_sources_from_current_dir(psftp no-jump-list.c nohelp.c psftp.rc)
add_dependencies(psftp generated_licence_h)

add_sources_from_current_dir(psocks nohelp.c)

add_library(tsf OBJECT
  tsf.cpp)
target_compile_features(tsf PRIVATE cxx_std_20)

add_library(fontzoom OBJECT
  fontzoom.cpp)
target_compile_features(fontzoom PRIVATE cxx_std_20)

add_executable(putty
  window.c
  d2ddw.cpp
  $<TARGET_OBJECTS:tsf>
  $<TARGET_OBJECTS:fontzoom>
  putty.c
  help.c
  ${CMAKE_SOURCE_DIR}/stubs/no-console.c
  putty.rc)
be_list(putty PuTTY SSH SERIAL OTHERBACKENDS)
add_dependencies(putty generated_licence_h)
target_link_libraries(putty
  guiterminal guimisc eventloop sshclient otherbackends settings network crypto
  utils
  iso2022
  ${platform_libraries})
set_target_properties(putty PROPERTIES WIN32_EXECUTABLE ON)
installed_program(putty)

target_compile_definitions(putty PRIVATE PUTTY_WITH_SSH)

add_executable(puttytel
  window.c
  d2ddw.cpp
  $<TARGET_OBJECTS:tsf>
  $<TARGET_OBJECTS:fontzoom>
  putty.c
  help.c
  ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
  ${CMAKE_SOURCE_DIR}/stubs/no-ca-config.c
  ${CMAKE_SOURCE_DIR}/stubs/no-console.c
  ${CMAKE_SOURCE_DIR}/stubs/no-rand.c
  ${CMAKE_SOURCE_DIR}/stubs/no-dit.c
  ${CMAKE_SOURCE_DIR}/proxy/nocproxy.c
  ${CMAKE_SOURCE_DIR}/proxy/nosshproxy.c
  puttytel.rc)
be_list(puttytel PuTTYtel SERIAL OTHERBACKENDS)
add_dependencies(puttytel generated_licence_h)
target_link_libraries(puttytel
  guiterminal guimisc eventloop otherbackends settings network utils
  iso2022
  ${platform_libraries})
set_target_properties(puttytel PROPERTIES WIN32_EXECUTABLE ON)
installed_program(puttytel)

add_executable(puttygen
  puttygen.c
  ${CMAKE_SOURCE_DIR}/stubs/no-timing.c
  noise.c
  no-jump-list.c
  ini.c
  storage.c
  help.c
  ${CMAKE_SOURCE_DIR}/sshpubk.c
  ${CMAKE_SOURCE_DIR}/sshrand.c
  controls.c
  ${CMAKE_SOURCE_DIR}/stubs/no-iso2022.c
  puttygen.rc)
add_dependencies(puttygen generated_licence_h)
target_link_libraries(puttygen
  keygen guimisc crypto utils
  iso2022
  ${platform_libraries})
set_target_properties(puttygen PROPERTIES WIN32_EXECUTABLE ON)
installed_program(puttygen)

if(HAVE_CONPTY)
  add_executable(pterm
    window.c
    d2ddw.cpp
    $<TARGET_OBJECTS:tsf>
    $<TARGET_OBJECTS:fontzoom>
    pterm.c
    help.c
    conpty.c
    ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
    ${CMAKE_SOURCE_DIR}/stubs/no-ca-config.c
    ${CMAKE_SOURCE_DIR}/stubs/no-console.c
    ${CMAKE_SOURCE_DIR}/stubs/no-rand.c
    ${CMAKE_SOURCE_DIR}/stubs/no-dit.c
    ${CMAKE_SOURCE_DIR}/proxy/nosshproxy.c
    pterm.rc)
  be_list(pterm pterm)
  add_dependencies(pterm generated_licence_h)
  target_link_libraries(pterm
    guiterminal guimisc eventloop settings network utils
    iso2022
    ${platform_libraries})
  set_target_properties(pterm PROPERTIES WIN32_EXECUTABLE ON)
  installed_program(pterm)
else()
  message("ConPTY not available; cannot build Windows pterm")
endif()

add_executable(test_split_into_argv
  ${CMAKE_SOURCE_DIR}/stubs/nol10n.c
  ${CMAKE_SOURCE_DIR}/utils/dupprintf.c
  test/test_split_into_argv.c)
target_compile_definitions(test_split_into_argv PRIVATE TEST)
target_link_libraries(test_split_into_argv utils ${platform_libraries})

add_executable(test_screenshot
  ${CMAKE_SOURCE_DIR}/stubs/no-ini.c
  ${CMAKE_SOURCE_DIR}/stubs/no-iso2022.c
  test/test_screenshot.c)
target_link_libraries(test_screenshot utils iso2022 ${platform_libraries})

add_executable(test_lineedit
  ${CMAKE_SOURCE_DIR}/test/test_lineedit.c
  ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
  ${CMAKE_SOURCE_DIR}/stubs/no-ini.c
  ${CMAKE_SOURCE_DIR}/stubs/no-logging.c
  ${CMAKE_SOURCE_DIR}/stubs/no-printing.c
  ${CMAKE_SOURCE_DIR}/stubs/no-storage.c
  ${CMAKE_SOURCE_DIR}/stubs/no-timing.c
  no-jump-list.c)
target_link_libraries(test_lineedit
  iso2022
  guiterminal settings eventloop utils ${platform_libraries})

add_executable(test_terminal
  ${CMAKE_SOURCE_DIR}/test/test_terminal.c
  ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
  ${CMAKE_SOURCE_DIR}/stubs/no-storage.c
  ${CMAKE_SOURCE_DIR}/stubs/no-timing.c
  no-jump-list.c)
target_link_libraries(test_terminal
  iso2022
  guiterminal settings eventloop utils ${platform_libraries})

add_sources_from_current_dir(test_conf
  ../stubs/no-iso2022.c
  no-jump-list.c handle-wait.c)

if(PUTTY_CAC)
target_link_libraries(puttytel crypto utils network sshclient)
target_link_libraries(pterm crypto utils network sshclient)
target_include_directories(crypto PUBLIC ../contrib)
target_include_directories(crypto PUBLIC ../crypto)
target_include_directories(crypto PUBLIC ../cert)
target_include_directories(sshcommon PUBLIC ../cert)
target_include_directories(settings PUBLIC ../cert)
target_include_directories(agent PUBLIC ../cert)
target_include_directories(guiterminal PUBLIC ../cert)
target_include_directories(sshclient PUBLIC ../cert)
target_include_directories(testcrypt PUBLIC ../cert)
target_include_directories(puttygen PUBLIC ../cert)
add_sources_from_current_dir(crypto
../import.c
../cert/cert_portable.c
../cert/cert_common.c
../cert/cert_capi.c
../cert/cert_capi_tools.cc
../cert/cert_fido.c
../cert/cert_pkcs.c)

add_executable(puttyimp
  puttyimp.c ../cert/cert_portable.c puttyimp.rc)
add_dependencies(puttyimp generated_licence_h)
set(fido_lib_arch "${CMAKE_C_COMPILER_ARCHITECTURE_ID}")
if(fido_lib_arch STREQUAL "X86")
  set(fido_lib_arch Win32)
endif()
set(fido_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/../contrib/libfido2/lib/${fido_lib_arch}")
target_include_directories(puttyimp PUBLIC ../contrib/libfido2/include)
target_include_directories(puttyimp PUBLIC ../cert)
target_link_libraries(puttyimp shlwapi.lib
  "${fido_lib_dir}/fido2.lib" "${fido_lib_dir}/cbor.lib" "${fido_lib_dir}/crypto.lib")
# The bundled libfido2 and OpenSSL archives ship without their compiler PDBs.
target_link_options(puttyimp PRIVATE $<$<CONFIG:Debug>:/IGNORE:4099>)
installed_program(puttyimp)

add_executable(puttytab
  tab/puttytab.cpp tab/theme.cpp ../cert/cert_portable.c tab/puttytab.rc)
add_dependencies(puttytab generated_licence_h)
target_compile_definitions(puttytab PRIVATE UNICODE _UNICODE)
target_compile_features(puttytab PRIVATE cxx_std_20)
target_include_directories(puttytab PRIVATE ../cert)
target_link_libraries(puttytab
  comctl32.lib
  dwmapi.lib
  ntdll.lib
  shlwapi.lib
  uxtheme.lib
  ${platform_libraries})
set_target_properties(puttytab PROPERTIES WIN32_EXECUTABLE ON)
installed_program(puttytab)
endif()
