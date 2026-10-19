add_executable(webdav_test webdav_test.cpp)
target_link_libraries(webdav_test PRIVATE pastes_sync pastes_platform Qt6::Network)
pastes_add_contract(webdav_contract headless webdav_test)
set_tests_properties(webdav_contract PROPERTIES TIMEOUT 60)

add_executable(syncordering_test syncordering_test.cpp)
target_link_libraries(syncordering_test PRIVATE
    pastes_sync pastes_storage pastes_platform Qt6::Network Qt6::Sql)
pastes_add_contract(syncordering_contract headless syncordering_test)
set_tests_properties(syncordering_contract PROPERTIES TIMEOUT 60)
