configure_file(org.freedesktop.impl.portal.desktop.kde.desktop.in org.freedesktop.impl.portal.desktop.kde.desktop @ONLY)
configure_file(org.freedesktop.impl.portal.desktop.kde.cmake.in org.freedesktop.impl.portal.desktop.kde.service @ONLY)

if(BUILD_CERTIFICATE_PORTAL)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/kde.portal")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/kde.portal" certificate_portal_descriptor)
    string(REPLACE "org.freedesktop.impl.portal.Usb" "org.freedesktop.impl.portal.Usb;org.freedesktop.impl.portal.experimental.Certificate1"
        certificate_portal_descriptor "${certificate_portal_descriptor}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/kde.portal" "${certificate_portal_descriptor}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/kde.portal" DESTINATION ${KDE_INSTALL_DATADIR}/xdg-desktop-portal/portals)
else()
    install(FILES kde.portal DESTINATION ${KDE_INSTALL_DATADIR}/xdg-desktop-portal/portals)
endif()
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.kde.service DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.kde.desktop DESTINATION ${KDE_INSTALL_APPDIR})

ecm_install_configured_files(INPUT plasma-xdg-desktop-portal-kde.service.in @ONLY
            DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
