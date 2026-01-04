#source files
file(GLOB_RECURSE MMS_BASE_LIB_SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/*.h" 
                                        "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp" 
                                        "${CMAKE_CURRENT_SOURCE_DIR}/*.c" 
                                        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" 
                                        "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")

# add_compile_options(-Wno-error)
add_library(mms-base STATIC ${MMS_BASE_LIB_SOURCES})
add_dependencies(mms-base 
    libboost
    libjsoncpp
    libboringssl
    libspdlog
)


target_include_directories(mms-base 
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../)

target_link_libraries(mms-base
    boost_system.a
    boost_serialization.a
    boost_date_time.a
    boost_regex.a
    boost_coroutine.a
    boost_thread.a
    boost_context.a
    boost_program_options.a
    ssl.a 
    crypto.a
    libjsoncpp.a
    libz.a
    libyaml-cpp.a
    dl
    pthread
)

install(TARGETS mms-base ARCHIVE DESTINATION lib)