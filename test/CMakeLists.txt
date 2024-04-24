add_executable(regex_test
	main.c
)
mregex_setup_target_wall(regex_test)

target_link_libraries(regex_test
    PRIVATE
        regex
)

add_test(NAME regex_test
    COMMAND regex_test
)
