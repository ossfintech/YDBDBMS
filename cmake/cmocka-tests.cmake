macro(ADD_UNIT_TEST_WITH_OPTIONS TEST_NAME TEST_FILE WRAP_FUNCTION)
  set(test_link_flags "")
  if(NOT "${WRAP_FUNCTION}" STREQUAL "")
    set(test_link_flags "--wrap,${WRAP_FUNCTION}")
  endif()
  add_executable(${TEST_NAME} ${PROJECT_SOURCE_DIR}/${TEST_FILE}.c $<TARGET_OBJECTS:liboctod>)

  target_link_libraries(${TEST_NAME} ${CMOCKA_LIBRARIES} ${Readline_LIBRARY} ${YOTTADB_LIBRARIES}  ${test_link_flags})
  add_test(${TEST_NAME} ${TEST_NAME})
endmacro(ADD_UNIT_TEST_WITH_OPTIONS)

#ADD_UNIT_TEST_WITH_OPTIONS(test_emit_create_table "")
#ADD_UNIT_TEST_WITH_OPTIONS(test_parser_negatives "")
#ADD_UNIT_TEST_WITH_OPTIONS(test_emit_select_statement "")
#ADD_UNIT_TEST_WITH_OPTIONS(test_generate_cursor "")

ADD_UNIT_TEST_WITH_OPTIONS(test_read_bind src/octod/test_read_bind "")
ADD_UNIT_TEST_WITH_OPTIONS(test_make_error_response src/octod/test_make_error_response "")
