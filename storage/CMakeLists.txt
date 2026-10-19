add_library(pastes_storage STATIC
    database.cpp database.h
    storedentry.cpp storedentry.h
)
target_link_libraries(pastes_storage PUBLIC pastes_application PRIVATE Qt6::Sql)
