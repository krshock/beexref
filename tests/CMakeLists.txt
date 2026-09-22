qt_add_executable(test_settings test_settings.cpp)
target_link_libraries(test_settings PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_settings)
add_test(NAME settings COMMAND test_settings)

qt_add_executable(test_logging test_logging.cpp)
target_link_libraries(test_logging PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_logging)
add_test(NAME logging COMMAND test_logging)

qt_add_executable(test_sqlite test_sqlite.cpp)
target_link_libraries(test_sqlite PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_sqlite)
add_test(NAME sqlite COMMAND test_sqlite)

qt_add_executable(test_schema test_schema.cpp)
target_link_libraries(test_schema PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_schema)
add_test(NAME schema COMMAND test_schema)
