qt_add_library(quickshell-wayland-tahoe-glass STATIC
	manager.cpp
	surface.cpp
	qml.cpp
)

qt_add_qml_module(quickshell-wayland-tahoe-glass
	URI Quickshell.Wayland._TahoeGlass
	VERSION 0.1
	DEPENDENCIES QtQml
)

install_qml_module(quickshell-wayland-tahoe-glass)

wl_proto(wlp-tahoe-glass tahoe-glass-v1 "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(quickshell-wayland-tahoe-glass PRIVATE
	Qt::Quick Qt::WaylandClient Qt::WaylandClientPrivate wayland-client
	quickshell-wayland-attached-surface-lifecycle
	quickshell-wayland-background-effect
	wlp-tahoe-glass
	wlp-background-effect
)

qs_module_pch(quickshell-wayland-tahoe-glass)

target_link_libraries(quickshell PRIVATE quickshell-wayland-tahoe-glassplugin)
