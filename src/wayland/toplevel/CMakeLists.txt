qt_add_library(quickshell-wayland-toplevel-management STATIC
	wlr_toplevel.cpp
	ext_toplevel.cpp
	identifier_pairing.cpp
	qml.cpp
)

qt_add_qml_module(quickshell-wayland-toplevel-management
	URI Quickshell.Wayland._ToplevelManagement
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-wayland-toplevel-management
	Quickshell Quickshell.Wayland
)

install_qml_module(quickshell-wayland-toplevel-management)

wl_proto(wlp-foreign-toplevel wlr-foreign-toplevel-management-unstable-v1 "${CMAKE_CURRENT_SOURCE_DIR}")
# Shared name with screencopy/image_copy_capture (same protocol XML); toplevel
# is configured first so capture reuses this target.
wl_proto(wlp-ext-foreign-toplevel ext-foreign-toplevel-list-v1 "${WAYLAND_PROTOCOLS}/staging/ext-foreign-toplevel-list")

target_link_libraries(quickshell-wayland-toplevel-management PRIVATE
	Qt::Quick Qt::WaylandClient Qt::WaylandClientPrivate wayland-client
	wlp-foreign-toplevel
	wlp-ext-foreign-toplevel
)

qs_module_pch(quickshell-wayland-toplevel-management SET large)

target_link_libraries(quickshell PRIVATE quickshell-wayland-toplevel-managementplugin)
