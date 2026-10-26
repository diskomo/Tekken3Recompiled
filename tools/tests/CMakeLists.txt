# Offline regressions: build, then run ctest in the build directory. None boots the
# game. Tests that need private local assets are added only when those assets
# are present. The Python suites need Python 3.11+ with
# tools/requirements-import.txt; pick one with -DTEKKEN3_TEST_PYTHON=<path>.
set(TEKKEN3_ROOT "${CMAKE_SOURCE_DIR}")
set(TEKKEN3_TEST_INCLUDES "${TEKKEN3_ROOT}/psxrecomp/runtime/include" "${TEKKEN3_ROOT}/src")

# NO_TEST builds a harness that other tests run with arguments.
function(tekken3_c_test name)
    cmake_parse_arguments(T "NO_TEST" "" "SOURCES;DEFINES;ARGS" ${ARGN})
    add_executable(${name} ${T_SOURCES})
    target_include_directories(${name} PRIVATE ${TEKKEN3_TEST_INCLUDES})
    target_compile_definitions(${name} PRIVATE ${T_DEFINES})
    # The checks are asserts: keep them in every build type.
    target_compile_options(${name} PRIVATE -UNDEBUG)
    if(NOT T_NO_TEST)
        add_test(NAME ${name} COMMAND ${name} ${T_ARGS})
    endif()
endfunction()

tekken3_c_test(ttt_ui_palette_test SOURCES ttt_ui_palette_test.c)
tekken3_c_test(ttt_ui_texture_test SOURCES ttt_ui_texture_test.c)
tekken3_c_test(ttt_voice_test SOURCES ttt_voice_test.c)
tekken3_c_test(jun_texture_layout_test SOURCES jun_texture_layout_test.c)
tekken3_c_test(fight_camera_policy_test SOURCES test_fight_camera_policy.c)
tekken3_c_test(main_menu_test SOURCES main_menu_test.c "${TEKKEN3_ROOT}/src/tekken3_main_menu.c")
tekken3_c_test(outfit_carousel_test
    SOURCES outfit_carousel_test.c "${TEKKEN3_ROOT}/psxrecomp/runtime/src/tekken3_outfits.c"
    DEFINES TEKKEN3_LAUNCHER=1)
tekken3_c_test(sound_options_test SOURCES sound_options_test.c
    ARGS "${CMAKE_CURRENT_BINARY_DIR}/sound-options-test.ini")
tekken3_c_test(disc_library_test SOURCES tekken3_disc_library_test.cpp
    ARGS "${CMAKE_CURRENT_BINARY_DIR}/disc-library-fixture")
target_compile_features(disc_library_test PRIVATE cxx_std_17)

# Each imported fighter's converted combat pack, when it has been imported.
set(TEKKEN3_PACK_BASES jun:8192)
foreach(entry IN LISTS TEKKEN3_PACK_BASES)
    string(REPLACE ":" ";" fields "${entry}")
    list(GET fields 0 key)
    list(GET fields 1 base)
    file(GLOB pack "${TEKKEN3_ROOT}/workspace/jun-import/${key}/*-TTT1-combat.jmv")
    if(pack)
        if(NOT TARGET ttt_pack_test)
            tekken3_c_test(ttt_pack_test NO_TEST SOURCES ttt_pack_test.c)
        endif()
        add_test(NAME ttt_pack_${key} COMMAND ttt_pack_test "${pack}" ${base})
    endif()
endforeach()

set(TEKKEN3_TEST_PYTHON "${Python3_EXECUTABLE}" CACHE FILEPATH "Python for the tools/tests suites")
file(GLOB TEKKEN3_PYTHON_TESTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/test_*.py")
foreach(script IN LISTS TEKKEN3_PYTHON_TESTS)
    get_filename_component(module "${script}" NAME_WE)
    add_test(NAME py_${module}
        COMMAND "${TEKKEN3_TEST_PYTHON}" -m unittest "tools.tests.${module}"
        WORKING_DIRECTORY "${TEKKEN3_ROOT}")
    # Suites that need Pillow report as skipped without it.
    set_tests_properties(py_${module} PROPERTIES SKIP_REGULAR_EXPRESSION "No module named 'PIL'")
endforeach()
