add_library(pastes_application STATIC
    historyrepository.h clipboardfeed.h syncservice.h
    historyservice.cpp historyservice.h
    clipboardcontroller.cpp clipboardcontroller.h
)
target_link_libraries(pastes_application PUBLIC pastes_core)
