# pch breaks clang-tidy..... somehow
if (NOT NO_PCH)
	# pchstub.cpp must be regenerable by the build tool: file(GENERATE) only
	# writes at cmake configure time, so `ninja -t clean` (or a deleted file)
	# left the pchset autogen steps without their source and ninja errored with
	# "missing and no known rule to make it". A custom command gives ninja a
	# real rule to recreate the stub before any pchset library is built.
	if (NOT TAHOE_PCHSTUB_RULE)
		set(TAHOE_PCHSTUB_RULE ON)
		add_custom_command(
			OUTPUT ${CMAKE_BINARY_DIR}/pchstub.cpp
			COMMAND ${CMAKE_COMMAND} -E echo "// intentionally empty" >
			        ${CMAKE_BINARY_DIR}/pchstub.cpp
			VERBATIM
			COMMENT "Generating pchstub.cpp"
		)
	endif()
endif()

function (qs_pch target)
	if (NO_PCH)
		return()
	endif()

	cmake_parse_arguments(PARSE_ARGV 1 arg "" "SET" "")

	if ("${arg_SET}" STREQUAL "")
		set(arg_SET "common")
	endif()

	target_precompile_headers(${target} REUSE_FROM "qs-pchset-${arg_SET}")
endfunction()

function (qs_module_pch target)
	qs_pch(${target} ${ARGN})
	qs_pch("${target}plugin" SET plugin)
	qs_pch("${target}plugin_init" SET plugin)
endfunction()

function (qs_add_pchset SETNAME)
	if (NO_PCH)
		return()
	endif()

	cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "HEADERS;DEPENDENCIES")

	set(LIBNAME "qs-pchset-${SETNAME}")

	add_library(${LIBNAME} ${CMAKE_BINARY_DIR}/pchstub.cpp)
	target_link_libraries(${LIBNAME} ${arg_DEPENDENCIES})
	target_precompile_headers(${LIBNAME} PUBLIC ${arg_HEADERS})
endfunction()

set(COMMON_PCH_SET
	<chrono>
	<memory>
	<vector>
	<qdebug.h>
	<qobject.h>
	<qmetatype.h>
	<qstring.h>
	<qchar.h>
	<qlist.h>
	<qabstractitemmodel.h>
)

qs_add_pchset(common
	DEPENDENCIES Qt::Quick
	HEADERS ${COMMON_PCH_SET}
)

qs_add_pchset(large
	DEPENDENCIES Qt::Quick
	HEADERS
		${COMMON_PCH_SET}
		<qiodevice.h>
		<qevent.h>
		<qcoreapplication.h>
		<qqmlengine.h>
		<qquickitem.h>
		<qquickwindow.h>
		<qcolor.h>
		<qdir.h>
		<qtimer.h>
		<qabstractitemmodel.h>
)


# including qplugin.h directly will cause required symbols to disappear
qs_add_pchset(plugin
	DEPENDENCIES Qt::Qml
	HEADERS
		<qobject.h>
		<qjsonobject.h>
		<qpointer.h>
)
