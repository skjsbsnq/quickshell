function (qs_test name)
	add_executable(${name} ${ARGN})
	target_link_libraries(${name} PRIVATE
		Qt::Quick
		Qt::QuickPrivate
		Qt::Test
		Qt::WaylandClient
		Qt::WaylandClientPrivate
		quickshell-wayland-tahoe-glass
		quickshell-window
		quickshell-core
		quickshell-ui
		quickshell-io
		wlp-tahoe-glass
	)
	target_include_directories(${name} PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/..
		${CMAKE_CURRENT_BINARY_DIR}/..
	)
	add_test(NAME ${name} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" COMMAND $<TARGET_FILE:${name}>)
	set_tests_properties(${name} PROPERTIES LABELS tahoe-glass)
endfunction()

# Unique shared harness for Task 06/13/17/20/08 Tahoe glass behavioral tests.
# Single executable; each task only adds named sources (no second harness).
qs_test(tahoe-glass-tests main.cpp region_diff.cpp transform_lifecycle.cpp fallback_alpha.cpp commit_atomicity.cpp mapping_lifecycle.cpp)
