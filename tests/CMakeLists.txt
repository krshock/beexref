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

qt_add_executable(test_ui_scene test_ui_scene.cpp)
target_link_libraries(test_ui_scene PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_ui_scene)
add_test(NAME ui_scene COMMAND test_ui_scene)

qt_add_executable(test_util test_util.cpp)
target_link_libraries(test_util PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_util)
add_test(NAME util COMMAND test_util)

qt_add_executable(test_image_io test_image_io.cpp)
target_link_libraries(test_image_io PRIVATE beexref_core Qt6::Test)
target_compile_definitions(test_image_io PRIVATE
    ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
beexref_set_warnings(test_image_io)
add_test(NAME image_io COMMAND test_image_io)

qt_add_executable(test_drop test_drop.cpp)
target_link_libraries(test_drop PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_drop)
add_test(NAME drop COMMAND test_drop)

qt_add_executable(test_downloader test_downloader.cpp)
target_link_libraries(test_downloader PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_downloader)
add_test(NAME downloader COMMAND test_downloader)

qt_add_executable(test_input test_input.cpp)
target_link_libraries(test_input PRIVATE beexref_core Qt6::Test)
target_compile_definitions(test_input PRIVATE
    ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
beexref_set_warnings(test_input)
add_test(NAME input COMMAND test_input)

qt_add_executable(test_levels test_levels.cpp)
target_link_libraries(test_levels PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_levels)
add_test(NAME levels COMMAND test_levels)

qt_add_executable(test_lod_manager test_lod_manager.cpp)
target_link_libraries(test_lod_manager PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_lod_manager)
add_test(NAME lod_manager COMMAND test_lod_manager)

qt_add_executable(test_session_cache test_session_cache.cpp)
target_link_libraries(test_session_cache PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_session_cache)
add_test(NAME session_cache COMMAND test_session_cache)

qt_add_executable(test_layout_ops test_layout_ops.cpp)
target_link_libraries(test_layout_ops PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_layout_ops)
add_test(NAME layout_ops COMMAND test_layout_ops)

qt_add_executable(test_crop_tools test_crop_tools.cpp)
target_link_libraries(test_crop_tools PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_crop_tools)
add_test(NAME crop_tools COMMAND test_crop_tools)

qt_add_executable(test_selection_tools test_selection_tools.cpp)
target_link_libraries(test_selection_tools PRIVATE beexref_core Qt6::Test)
beexref_set_warnings(test_selection_tools)
add_test(NAME selection_tools COMMAND test_selection_tools)
