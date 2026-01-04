#source files
file(GLOB_RECURSE MMS_CODEC_LIB_SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/*.h" 
                                         "${CMAKE_CURRENT_SOURCE_DIR}/*.c" 
                                         "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" 
                                         "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")

add_library(mms-codec STATIC ${MMS_CODEC_LIB_SOURCES})
add_dependencies(mms-codec 
    libboost
    libjsoncpp
    libboringssl
    libopus
    libfaad2
    libaac
    mms-base
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(mms-codec 
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../)
target_include_directories(mms-codec 
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})


target_link_libraries(mms-codec
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
    libopus.a
    libyaml-cpp.a
    dl
    pthread
    mms-base
)

install(TARGETS mms-codec ARCHIVE DESTINATION lib)