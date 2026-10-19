add_library(pastes_sync STATIC
    webdavworker.cpp webdavworker.h
    webdavsync.cpp webdavsync.h
)
target_link_libraries(pastes_sync PUBLIC pastes_application PRIVATE Qt6::Network)
