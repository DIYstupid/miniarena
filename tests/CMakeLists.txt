# ============================================================
# GoogleTest Setup
# ============================================================

# Build gtest from system source (Ubuntu libgtest-dev pattern)
add_library(gtest STATIC
    /usr/src/googletest/googletest/src/gtest-all.cc
)
target_include_directories(gtest PUBLIC
    /usr/src/googletest/googletest/include
    /usr/src/googletest/googletest
)
find_package(Threads REQUIRED)
target_link_libraries(gtest PUBLIC Threads::Threads)

add_library(gtest_main STATIC
    /usr/src/googletest/googletest/src/gtest_main.cc
)
target_link_libraries(gtest_main PUBLIC gtest)

# ============================================================
# Test Executables
# ============================================================

# Proto test
add_executable(test_proto
    test_proto.cpp
)
target_link_libraries(test_proto PRIVATE
    miniarena_proto
    gtest_main
)
add_test(NAME Proto COMMAND test_proto)
