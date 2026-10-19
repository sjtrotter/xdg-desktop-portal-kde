ecm_add_tests(
    xdgshortcuttest.cpp

    LINK_LIBRARIES Qt::Test Qt::Gui
)
target_sources(xdgshortcuttest PRIVATE ${CMAKE_SOURCE_DIR}/src/xdgshortcut.cpp ${CMAKE_SOURCE_DIR}/src/xdgshortcut.h)
target_link_libraries(xdgshortcuttest Qt::GuiPrivate)

add_test(
    NAME colorschemetest
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/colorschemetest.py
)
set_tests_properties(colorschemetest PROPERTIES ENVIRONMENT "CMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

ecm_add_test(inputcapturebarriertest.cpp ${CMAKE_SOURCE_DIR}/src/inputcapturebarrier.cpp TEST_NAME inputcapturebarriertest LINK_LIBRARIES Qt::Test Qt::Gui)

if(BUILD_CERTIFICATE_PORTAL)
    ecm_add_test(certificateconfigtest.cpp ${CMAKE_SOURCE_DIR}/src/certificateconfig.cpp
        TEST_NAME certificateconfigtest LINK_LIBRARIES Qt::Test KF6::ConfigCore PkgConfig::XCC)
    target_include_directories(certificateconfigtest PRIVATE ${CMAKE_SOURCE_DIR}/src)
    set_tests_properties(certificateconfigtest PROPERTIES TIMEOUT 60)
    ecm_add_test(certificatedispatchertest.cpp
        ${CMAKE_SOURCE_DIR}/src/certificatedispatcher.cpp
        TEST_NAME certificatedispatchertest LINK_LIBRARIES Qt::Test PkgConfig::XCC)
    target_include_directories(certificatedispatchertest PRIVATE ${CMAKE_SOURCE_DIR}/src)
    ecm_add_test(certificatechoosermodeltest.cpp
        ${CMAKE_SOURCE_DIR}/src/certificatechoosermodel.cpp
        TEST_NAME certificatechoosermodeltest LINK_LIBRARIES Qt::Test KF6::I18n PkgConfig::XCC)
    target_include_directories(certificatechoosermodeltest PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(certificatechoosermodeltest PRIVATE TRANSLATION_DOMAIN="xdg-desktop-portal-kde")
    add_executable(certificateadaptortest certificateadaptortest.cpp
        ${CMAKE_SOURCE_DIR}/src/quickdialog.cpp certificateutilsstub.cpp)
    target_link_libraries(certificateadaptortest PRIVATE certificateportal Qt::Test Qt::Quick Qt::Widgets KF6::I18n KF6::I18nQml KF6::WindowSystem)
    target_compile_definitions(certificateadaptortest PRIVATE TRANSLATION_DOMAIN="xdg-desktop-portal-kde"
        CERTIFICATE_TEST_BUS_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/certificate-test-bus.conf")
    target_link_options(certificateadaptortest PRIVATE
        "-Wl,--wrap=xcc_context_new" "-Wl,--wrap=xcc_candidate_info")
    add_executable(certificatepinfieldtest certificatepinfieldtest.cpp ${CMAKE_SOURCE_DIR}/src/certificatepinfield.cpp)
    target_link_libraries(certificatepinfieldtest PRIVATE Qt::Test Qt::Quick Qt::DBus PkgConfig::XCC)
    target_include_directories(certificatepinfieldtest PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_executable(certificatepinbridgetest certificatepinbridgetest.cpp
        ${CMAKE_SOURCE_DIR}/src/quickdialog.cpp certificateutilsstub.cpp)
    target_link_libraries(certificatepinbridgetest PRIVATE certificateportal Qt::Test Qt::Quick Qt::Widgets KF6::I18n KF6::I18nQml KF6::WindowSystem)
    foreach(ui_test certificateadaptortest certificatepinfieldtest certificatepinbridgetest)
        ecm_mark_as_test(${ui_test})
        ecm_mark_nongui_executable(${ui_test})
        target_compile_definitions(${ui_test} PRIVATE QT_FORCE_ASSERTS)
        # The wrapper creates the bus before QApplication can probe portals.
        add_test(NAME ${ui_test} COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/certificate-test-session.sh $<TARGET_FILE:${ui_test}>)
        set_tests_properties(${ui_test} PROPERTIES TIMEOUT 60)
    endforeach()
    # Mirror the service's QML module without generating two plugins with the
    # same URI/output directory. The field is registered by CertificateUi.
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/CertificatePinDialog.qml
        PROPERTIES QT_RESOURCE_ALIAS CertificatePinDialog.qml)
    set_source_files_properties(certificate-test-qmldir PROPERTIES QT_RESOURCE_ALIAS qmldir)
    foreach(ui_test certificateadaptortest certificatepinbridgetest)
        qt_add_resources(${ui_test} ${ui_test}_certificate_test_ui
            PREFIX "/qt/qml/org/kde/xdgdesktopportal"
            FILES ${CMAKE_SOURCE_DIR}/src/CertificatePinDialog.qml certificate-test-qmldir)
    endforeach()
    foreach(pin_test certificatepinfieldtest certificatepinbridgetest)
        target_compile_definitions(${pin_test} PRIVATE TRANSLATION_DOMAIN="xdg-desktop-portal-kde")
        target_link_options(${pin_test} PRIVATE
            "-Wl,--wrap=xcc_secret_new" "-Wl,--wrap=xcc_secret_free"
            "-Wl,--wrap=xcc_pin_ready" "-Wl,--wrap=xcc_pin_submit"
            "-Wl,--wrap=xcc_warning_reply" "-Wl,--wrap=xcc_ui_cancel" "-Wl,--wrap=xcc_ui_failed")
    endforeach()
    set_tests_properties(certificatedispatchertest certificatechoosermodeltest
        PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_NO_GLIB=1")
    set_tests_properties(certificateadaptortest PROPERTIES TIMEOUT 180)
    add_test(NAME certificateacquisitiontest
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/certificate-test-session.sh
            bash ${CMAKE_CURRENT_SOURCE_DIR}/certificate-softhsm-fixture.sh
            $<TARGET_FILE:certificateadaptortest> successfulAcquisitionAndGrantedMalformedSign)
    set_tests_properties(certificateacquisitiontest PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77)
    add_test(NAME certificatepinsigntest
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/certificate-test-session.sh
            bash ${CMAKE_CURRENT_SOURCE_DIR}/certificate-softhsm-fixture.sh
            $<TARGET_FILE:certificateadaptortest> grantedSignWithNativePin)
    set_tests_properties(certificatepinsigntest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
        ENVIRONMENT "CERTIFICATE_TEST_REQUIRE_REFERENCE=0")
    add_test(NAME certificatepinalwaysauthtest
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/certificate-test-session.sh
            bash ${CMAKE_CURRENT_SOURCE_DIR}/certificate-softhsm-fixture.sh
            $<TARGET_FILE:certificateadaptortest> grantedSignWithNativePin)
    set_tests_properties(certificatepinalwaysauthtest PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
        ENVIRONMENT "CERTIFICATE_TEST_REQUIRE_REFERENCE=0;CERTIFICATE_TEST_ALWAYS_AUTH=1")
endif()
