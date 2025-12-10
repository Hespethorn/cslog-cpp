add_executable(cslog_example_basic
    basic/main.cpp
)

target_link_libraries(cslog_example_basic
    PRIVATE
        cslog
)
