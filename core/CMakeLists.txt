add_library(pastes_core STATIC
    itemdata.cpp itemdata.h
    clipboarddata.cpp clipboarddata.h
    clipboardcontent.cpp clipboardcontent.h
    historypolicy.h sourceicon.h eventstate.h
    synccontent.cpp synccontent.h
)
# Core has no Qt meta-object declarations; avoid an empty moc archive member.
set_target_properties(pastes_core PROPERTIES AUTOMOC OFF)
target_include_directories(pastes_core PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(pastes_core PUBLIC Qt6::Core Qt6::Gui)
