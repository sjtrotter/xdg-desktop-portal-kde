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
    ecm_add_test(certificatedispatchertest.cpp
        ${CMAKE_SOURCE_DIR}/src/certificatedispatcher.cpp
        TEST_NAME certificatedispatchertest LINK_LIBRARIES Qt::Test PkgConfig::XCC)
    target_include_directories(certificatedispatchertest PRIVATE ${CMAKE_SOURCE_DIR}/src)
    ecm_add_test(certificatechoosermodeltest.cpp
        ${CMAKE_SOURCE_DIR}/src/certificatechoosermodel.cpp
        TEST_NAME certificatechoosermodeltest LINK_LIBRARIES Qt::Test KF6::I18n PkgConfig::XCC)
    target_include_directories(certificatechoosermodeltest PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(certificatechoosermodeltest PRIVATE TRANSLATION_DOMAIN="xdg-desktop-portal-kde")
    ecm_add_test(certificateadaptortest.cpp
        ${CMAKE_SOURCE_DIR}/src/quickdialog.cpp certificateutilsstub.cpp
        TEST_NAME certificateadaptortest LINK_LIBRARIES certificateportal Qt::Test Qt::Quick Qt::Widgets KF6::I18n KF6::I18nQml KF6::WindowSystem)
    target_compile_definitions(certificateadaptortest PRIVATE TRANSLATION_DOMAIN="xdg-desktop-portal-kde")
    target_link_options(certificateadaptortest PRIVATE
        "-Wl,--wrap=xcc_context_new" "-Wl,--wrap=xcc_candidate_info")
    set_tests_properties(certificatedispatchertest certificatechoosermodeltest certificateadaptortest
        PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_NO_GLIB=1")
    set_tests_properties(certificateadaptortest PROPERTIES TIMEOUT 180)
    add_test(NAME certificateacquisitiontest
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/certificate-softhsm-fixture.sh
            $<TARGET_FILE:certificateadaptortest> successfulAcquisitionAndGrantedMalformedSign)
    set_tests_properties(certificateacquisitiontest PROPERTIES
        TIMEOUT 180 SKIP_RETURN_CODE 77 ENVIRONMENT "QT_NO_GLIB=1")
endif()
