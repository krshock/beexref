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

qt_add_executable(test_board test_board.cpp)
target_link_libraries(test_board PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_board)
add_test(NAME board COMMAND test_board)

qt_add_executable(test_board_save test_board_save.cpp)
target_link_libraries(test_board_save PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_board_save)
add_test(NAME board_save COMMAND test_board_save)

qt_add_executable(test_boardcheck test_boardcheck.cpp)
target_link_libraries(test_boardcheck PRIVATE beexref_core Qt6::Test)
target_compile_definitions(test_boardcheck PRIVATE
    BOARDCHECK_PATH="$<TARGET_FILE:beexref-boardcheck>")
beexref_set_warnings(test_boardcheck)
add_test(NAME boardcheck COMMAND test_boardcheck)

qt_add_executable(test_doc_item test_doc_item.cpp)
target_link_libraries(test_doc_item PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_doc_item)
add_test(NAME doc_item COMMAND test_doc_item)

qt_add_executable(test_doc_document test_doc_document.cpp)
target_link_libraries(test_doc_document PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_doc_document)
add_test(NAME doc_document COMMAND test_doc_document)

qt_add_executable(test_doc_undo test_doc_undo.cpp)
target_link_libraries(test_doc_undo PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_doc_undo)
add_test(NAME doc_undo COMMAND test_doc_undo)
