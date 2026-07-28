function (qs_test name)
	add_executable(${name} ${ARGN})
	target_link_libraries(${name} PRIVATE Qt::Test)
	add_test(NAME ${name} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" COMMAND $<TARGET_FILE:${name}>)
endfunction()

# T-06 / F-04: partial-write resume for crash-handler frame dumps.
# Compiles write_all.cpp directly so the test does not pull CrashHandler's
# Qt/cpptrace/logcat link surface.
qs_test(write_all write_all.cpp ../write_all.cpp)
