PROJECT("Application")

INCLUDE_DIRECTORIES("${CMAKE_CURRENT_BINARY_DIR}")

ADD_SUBDIRECTORY(ui)
INCLUDE_DIRECTORIES("${toplevel_ui_sources_BINARY_DIR}")

ADD_SUBDIRECTORY(dewarping)
ADD_SUBDIRECTORY(foundation)
ADD_SUBDIRECTORY(math)
ADD_SUBDIRECTORY(imageproc)
ADD_SUBDIRECTORY(interaction)
ADD_SUBDIRECTORY(zones)
ADD_SUBDIRECTORY(acceleration)
IF(NOT STE_NO_TESTS STREQUAL "ON")
    ADD_SUBDIRECTORY(tests)
ENDIF()

ADD_SUBDIRECTORY(stages)

SET(resource_files resources/resources.qrc resources/icons/COPYING)
IF(WIN32)
    LIST(APPEND resource_files resources/win32/resources.rc)
ENDIF()
SOURCE_GROUP("Resources" FILES ${resource_files})

FILE(WRITE "${CMAKE_BINARY_DIR}/src/resources/license.qrc" "<RCC><qresource prefix=\"/\"><file>license.html</file></qresource></RCC>")
IF(QT_DEFAULT_MAJOR_VERSION EQUAL 5)
    QT5_ADD_RESOURCES(resource_sources resources/resources.qrc "${CMAKE_BINARY_DIR}/src/resources/license.qrc")
ELSE()
    QT6_ADD_RESOURCES(resource_sources resources/resources.qrc "${CMAKE_BINARY_DIR}/src/resources/license.qrc")
ENDIF()
SOURCE_GROUP("Generated" FILES ${resource_sources})

SET(
    common_sources
    BackgroundExecutor.cpp BackgroundExecutor.h
    OpenGLSupport.cpp OpenGLSupport.h
    PixmapRenderer.cpp PixmapRenderer.h
    BubbleAnimation.cpp BubbleAnimation.h
    ProcessingIndicationWidget.cpp ProcessingIndicationWidget.h
    NonOwningWidget.cpp NonOwningWidget.h
    ContentBox.cpp ContentBox.h
    UnifiedContentReview.cpp UnifiedContentReview.h
    UnifiedAiReview.cpp UnifiedAiReview.h UnifiedAiReviewDialog.h
    UnifiedGeometryReview.cpp UnifiedGeometryReview.h UnifiedGeometryReviewDialog.h UnifiedReviewStore.h
    SmartFilenameOrdering.cpp SmartFilenameOrdering.h
    AbstractRelinker.h
    RelinkablePath.cpp RelinkablePath.h
    ImageInfo.cpp ImageInfo.h
    ImageFileInfo.h
    ImageMetadata.cpp ImageMetadata.h
    RecentProjects.cpp RecentProjects.h
    OutOfMemoryHandler.cpp OutOfMemoryHandler.h
    CommandLine.cpp CommandLine.h
    PageSelectionAccessor.cpp PageSelectionAccessor.h
    PageSelectionProvider.h
    ContentSpanFinder.cpp ContentSpanFinder.h
    ImagePixmapUnion.h
    ImageViewBase.cpp ImageViewBase.h
    BasicImageView.cpp BasicImageView.h
    DebugImageView.cpp DebugImageView.h
    VectorFieldImageView.cpp VectorFieldImageView.h
    TabbedDebugImages.cpp TabbedDebugImages.h
    ThumbnailLoadResult.h
    ThumbnailPixmapCache.cpp ThumbnailPixmapCache.h
    ThumbnailBase.cpp ThumbnailBase.h
    ThumbnailFactory.cpp ThumbnailFactory.h
    IncompleteThumbnail.cpp IncompleteThumbnail.h
    ContentBoxPropagator.cpp ContentBoxPropagator.h
    PageOrientationPropagator.cpp PageOrientationPropagator.h
    DebugImagesImpl.cpp DebugImagesImpl.h
    DebugViewFactory.h
    ImageId.cpp ImageId.h
    PageId.cpp PageId.h
    PageInfo.cpp PageInfo.h
    BackgroundTask.cpp BackgroundTask.h
    ProcessingTaskQueue.cpp ProcessingTaskQueue.h
    PageSequence.cpp PageSequence.h
    StageSequence.cpp StageSequence.h
    ProjectPages.cpp ProjectPages.h
    ImageMetadataLoader.cpp ImageMetadataLoader.h
    TiffReader.cpp TiffReader.h
    TiffWriter.cpp TiffWriter.h
    TiffMetadataLoader.cpp TiffMetadataLoader.h
    fastimage.c fastimage.h
    FastImageMetadataLoader.cpp FastImageMetadataLoader.h
    ImageLoader.cpp ImageLoader.h
    ErrorWidget.cpp ErrorWidget.h
    OrthogonalRotation.cpp OrthogonalRotation.h
    WorkerThreadPool.cpp WorkerThreadPool.h
    LoadFileTask.cpp LoadFileTask.h
    FilterOptionsWidget.cpp FilterOptionsWidget.h
    TaskStatus.h FilterUiInterface.h
    ProjectReader.cpp ProjectReader.h
    ProjectWriter.cpp ProjectWriter.h
    PaletteReader.cpp PaletteReader.h
    AtomicFileOverwriter.cpp AtomicFileOverwriter.h
    EstimateBackground.cpp EstimateBackground.h
    Despeckle.cpp Despeckle.h
    FileNameDisambiguator.cpp FileNameDisambiguator.h
    OutputFileNameGenerator.cpp OutputFileNameGenerator.h
    PageRange.cpp PageRange.h
    SelectedPage.cpp SelectedPage.h
    Utils.cpp Utils.h
    PageView.h
    AutoManualMode.h
    AbstractCommand.h
    AbstractFilter.h
    BeforeOrAfter.h
    FilterResult.h
    CompositeCacheDrivenTask.h
    ChangedStateItemDelegate.h
    PageOrderProvider.h
    PageOrderOption.h
    PayloadEvent.h
    filter_dc/AbstractFilterDataCollector.h
    filter_dc/ThumbnailCollector.h
    filter_dc/ContentBoxCollector.h
    filter_dc/PageOrientationCollector.h
    version.h
    config.h.in
    ${common_ui_files}
)

SET(
    gui_only_sources
    Application.cpp Application.h
    SkinnedButton.cpp SkinnedButton.h
    StageListView.cpp StageListView.h
    RelinkablePathVisualization.cpp RelinkablePathVisualization.h
    RelinkingModel.cpp RelinkingModel.h
    RelinkingSortingModel.cpp RelinkingSortingModel.h
    RelinkingListView.cpp RelinkingListView.h
    RelinkingDialog.cpp RelinkingDialog.h
    SettingsDialog.cpp SettingsDialog.h
    LoadFilesStatusDialog.cpp LoadFilesStatusDialog.h
    ProjectCreationContext.cpp ProjectCreationContext.h
    ProjectOpeningContext.cpp ProjectOpeningContext.h
    OutOfMemoryDialog.cpp OutOfMemoryDialog.h
    ThumbnailSequence.cpp ThumbnailSequence.h
    ProjectFilesDialog.cpp ProjectFilesDialog.h
    NewOpenProjectPanel.cpp NewOpenProjectPanel.h
    SystemLoadWidget.cpp SystemLoadWidget.h
    UnifiedPanel.cpp UnifiedPanel.h UnifiedResources.cpp UnifiedResources.h
    UnifiedQueuePreview.cpp UnifiedQueuePreview.h UnifiedQueueSelection.h
    UnifiedQueueThumbnails.cpp UnifiedQueueThumbnails.h
    MainWindow.cpp MainWindow.h MainWindowWorkflow.cpp UnifiedWorkflow.h UnifiedWorkflowHelp.h UnifiedProjectCache.h
    main.cpp
)

SET(
    cli_only_sources
    ConsoleBatch.cpp ConsoleBatch.h
    main-cli.cpp
)

SOURCE_GROUP("Sources" FILES ${common_sources} ${gui_only_sources} ${cli_only_sources})
SOURCE_GROUP("Special Headers" FILES version.h config.h.in)

IF(CMAKE_COMPILER_IS_GNUCXX)
    # There seems to be a bug in either gcc or boost that makes page sorting either crash
    # or go into infinite loop.  It's reproducible on a very simple test case on at least
    # gcc 4.4.3 and gcc 4.4.4.  It's not reproducible on gcc 4.5.1.
    # Adding -fno-strict-aliasing seems to fix it.
    SET_SOURCE_FILES_PROPERTIES(
        ThumbnailSequence.cpp PROPERTIES
        COMPILE_FLAGS "-fno-strict-aliasing"
    )
ENDIF()

SET(win32_resource_file "")
IF(WIN32)
    SET(rc_file "${CMAKE_CURRENT_SOURCE_DIR}/resources/win32/resources.rc")
    FILE(GLOB win32_resources resources/win32/*.ico)
    SET_SOURCE_FILES_PROPERTIES(
        "${rc_file}" PROPERTIES
        OBJECT_DEPENDS ${win32_resources}
    )
    SET(win32_resource_file "${rc_file}")
ENDIF(WIN32)

ADD_LIBRARY(stcore STATIC ${common_sources} ${common_ui_sources})

IF(APPLE)
    IF(QT_DEFAULT_MAJOR_VERSION EQUAL 5)
        TARGET_LINK_LIBRARIES(stcore Qt5::Widgets Qt5::Xml Qt5::PrintSupport ${MAYBE_QT_OPENGL_MODULE})
    ELSE()
        TARGET_LINK_LIBRARIES(stcore Qt6::Widgets Qt6::Xml Qt6::PrintSupport ${MAYBE_QT_OPENGL_MODULE})
    ENDIF()
ELSE()
    IF(QT_DEFAULT_MAJOR_VERSION EQUAL 5)
        TARGET_LINK_LIBRARIES(stcore Qt5::Widgets Qt5::Xml ${MAYBE_QT_OPENGL_MODULE})
    ELSE()
        TARGET_LINK_LIBRARIES(stcore Qt6::Widgets Qt6::Xml ${MAYBE_QT_OPENGL_MODULE})
    ENDIF()
ENDIF()

ADD_DEPENDENCIES(stcore toplevel_ui_sources)

IF(APPLE)
    SET(ICON_NAME "ScanTailor-Experimental.icns")
    SET(ICON_PATH "${CMAKE_SOURCE_DIR}/packaging/osx/${ICON_NAME}")
    SET_SOURCE_FILES_PROPERTIES(${ICON_PATH} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    ADD_EXECUTABLE(scantailor-experimental MACOSX_BUNDLE ${gui_only_sources} ${resource_sources} ${ICON_PATH})
    SET_TARGET_PROPERTIES(scantailor-experimental PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/osx/Info.plist.in
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
ELSEIF(WIN32)
    ADD_EXECUTABLE(scantailor-experimental WIN32 ${gui_only_sources} ${resource_sources} ${win32_resource_file})
ELSE()
    ADD_EXECUTABLE(scantailor-experimental ${gui_only_sources} ${resource_sources})
ENDIF()

TARGET_LINK_LIBRARIES(
    scantailor-experimental
    $<$<PLATFORM_ID:Windows>:psapi>
    acceleration page_layout output
    fix_orientation page_split deskew select_content stcore
    dewarping zones interaction imageproc math foundation
    ${EXTRA_LIBS}
)
SET(STOU_GUI_OUTPUT_NAME "scantailor-ocr-unified-0.5.0" CACHE STRING "GUI executable name")
SET_TARGET_PROPERTIES(
    scantailor-experimental PROPERTIES
    OUTPUT_NAME "${STOU_GUI_OUTPUT_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
TARGET_INCLUDE_DIRECTORIES(scantailor-experimental PRIVATE "${CMAKE_BINARY_DIR}/src/stages/output" "${CMAKE_BINARY_DIR}/src/stages/page_layout" "${CMAKE_BINARY_DIR}/src/stages/deskew" "${CMAKE_BINARY_DIR}/src/stages/select_content")
IF(EXISTS "${CMAKE_SOURCE_DIR}/../../tests/native_i18n.cpp")
    SET(i18n_test_sources ${gui_only_sources})
    LIST(REMOVE_ITEM i18n_test_sources main.cpp)
    ADD_EXECUTABLE(unified-i18n-test EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/../../tests/native_i18n.cpp" ${i18n_test_sources} ${resource_sources})
    TARGET_INCLUDE_DIRECTORIES(unified-i18n-test PRIVATE "${CMAKE_BINARY_DIR}/src/stages/output" "${CMAKE_BINARY_DIR}/src/stages/page_layout" "${CMAKE_BINARY_DIR}/src/stages/deskew" "${CMAKE_BINARY_DIR}/src/stages/select_content")
    TARGET_LINK_LIBRARIES(unified-i18n-test $<$<PLATFORM_ID:Windows>:psapi> acceleration page_layout output fix_orientation page_split deskew select_content stcore dewarping zones interaction imageproc math foundation ${EXTRA_LIBS})
    SET_TARGET_PROPERTIES(unified-i18n-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
ENDIF()
IF(EXISTS "${CMAKE_SOURCE_DIR}/../../tests/native_window043.cpp")
    SET(window_test_sources ${gui_only_sources})
    LIST(REMOVE_ITEM window_test_sources main.cpp)
    ADD_EXECUTABLE(unified-window-test EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/../../tests/native_window043.cpp" ${window_test_sources} ${resource_sources})
    TARGET_INCLUDE_DIRECTORIES(unified-window-test PRIVATE "${CMAKE_BINARY_DIR}/src/stages/output" "${CMAKE_BINARY_DIR}/src/stages/page_layout" "${CMAKE_BINARY_DIR}/src/stages/deskew" "${CMAKE_BINARY_DIR}/src/stages/select_content")
    TARGET_LINK_LIBRARIES(unified-window-test $<$<PLATFORM_ID:Windows>:psapi> acceleration page_layout output fix_orientation page_split deskew select_content stcore dewarping zones interaction imageproc math foundation ${EXTRA_LIBS})
    SET_TARGET_PROPERTIES(unified-window-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
ENDIF()

# Local regression test: real queue widgets and asynchronous preview selection.
# Excluded from normal builds and distribution.
IF(EXISTS "${CMAKE_SOURCE_DIR}/../../tests/native_stage_tools.cpp")
    ADD_EXECUTABLE(unified-stage-tools-test EXCLUDE_FROM_ALL
        "${CMAKE_SOURCE_DIR}/../../tests/native_stage_tools.cpp"
        UnifiedPanel.cpp UnifiedPanel.h UnifiedResources.cpp UnifiedResources.h)
    TARGET_INCLUDE_DIRECTORIES(unified-stage-tools-test PRIVATE
        "${CMAKE_BINARY_DIR}/src/stages/output" "${CMAKE_BINARY_DIR}/src/stages/select_content" "${CMAKE_BINARY_DIR}/src/stages/page_layout" "${CMAKE_BINARY_DIR}/src/stages/deskew")
    TARGET_LINK_LIBRARIES(unified-stage-tools-test
        $<$<PLATFORM_ID:Windows>:psapi>
        "$<LINK_GROUP:RESCAN,acceleration,page_layout,output,fix_orientation,page_split,deskew,select_content,stcore,dewarping,zones,interaction,imageproc,math,foundation>" ${EXTRA_LIBS})
    SET_TARGET_PROPERTIES(unified-stage-tools-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
ENDIF()
IF(EXISTS "${CMAKE_SOURCE_DIR}/../../tests/native_review_state.cpp")
    ADD_EXECUTABLE(unified-review-state-test EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/../../tests/native_review_state.cpp")
    TARGET_LINK_LIBRARIES(unified-review-state-test
        "$<LINK_GROUP:RESCAN,acceleration,page_layout,output,fix_orientation,page_split,deskew,select_content,stcore,dewarping,zones,interaction,imageproc,math,foundation>" ${EXTRA_LIBS})
    SET_TARGET_PROPERTIES(unified-review-state-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
ENDIF()
IF(EXISTS "${CMAKE_SOURCE_DIR}/../../tests/native_soft_preview.cpp")
    ADD_EXECUTABLE(soft-preview-test EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/../../tests/native_soft_preview.cpp")
    TARGET_LINK_LIBRARIES(soft-preview-test foundation ${EXTRA_LIBS})
    SET_TARGET_PROPERTIES(soft-preview-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
ENDIF()
IF(EXISTS "${CMAKE_SOURCE_DIR}/../../tests/native_queue_selection.cpp")
    ADD_EXECUTABLE(unified-selection-test EXCLUDE_FROM_ALL
        "${CMAKE_SOURCE_DIR}/../../tests/native_queue_selection.cpp"
        UnifiedPanel.cpp UnifiedPanel.h UnifiedResources.cpp UnifiedResources.h
        UnifiedQueuePreview.cpp UnifiedQueuePreview.h
        UnifiedQueueThumbnails.cpp UnifiedQueueThumbnails.h)
    TARGET_LINK_LIBRARIES(unified-selection-test
        $<$<PLATFORM_ID:Windows>:psapi>
        acceleration page_layout output fix_orientation page_split deskew select_content stcore
        dewarping zones interaction imageproc math foundation ${EXTRA_LIBS})
    SET_TARGET_PROPERTIES(unified-selection-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
ENDIF()

ADD_EXECUTABLE(scantailor-experimental-cli ${cli_only_sources} ${common_ui_sources})
SET(STOU_CLI_OUTPUT_NAME "scantailor-experimental-cli" CACHE STRING "CLI filename for isolated performance builds")
IF(EXISTS "${CMAKE_SOURCE_DIR}/../../tests/native_geometry.cpp")
    ADD_EXECUTABLE(unified-geometry-test EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/../../tests/native_geometry.cpp")
    TARGET_LINK_LIBRARIES(unified-geometry-test foundation ${EXTRA_LIBS})
    SET_TARGET_PROPERTIES(unified-geometry-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
ENDIF()
IF(EXISTS "${CMAKE_SOURCE_DIR}/../../tests/native_content_review.cpp")
    ADD_EXECUTABLE(unified-ai-test EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/../../tests/native_ai_review.cpp")
    ADD_EXECUTABLE(unified-geometry-review-test EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/../../tests/native_geometry_review.cpp")
    TARGET_LINK_LIBRARIES(unified-geometry-review-test "$<LINK_GROUP:RESCAN,acceleration,page_layout,output,fix_orientation,page_split,deskew,select_content,stcore,dewarping,zones,interaction,imageproc,math,foundation>" ${EXTRA_LIBS})
    SET_TARGET_PROPERTIES(unified-geometry-review-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    TARGET_LINK_LIBRARIES(unified-ai-test
        "$<LINK_GROUP:RESCAN,acceleration,page_layout,output,fix_orientation,page_split,deskew,select_content,stcore,dewarping,zones,interaction,imageproc,math,foundation>" ${EXTRA_LIBS})
    SET_TARGET_PROPERTIES(unified-ai-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    ADD_EXECUTABLE(unified-auto-geometry-test EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/../../tests/native_auto_geometry.cpp")
    TARGET_LINK_LIBRARIES(unified-auto-geometry-test
        "$<LINK_GROUP:RESCAN,acceleration,page_layout,output,fix_orientation,page_split,deskew,select_content,stcore,dewarping,zones,interaction,imageproc,math,foundation>" ${EXTRA_LIBS})
    SET_TARGET_PROPERTIES(unified-auto-geometry-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    ADD_EXECUTABLE(unified-manual-test EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/../../tests/native_manual_geometry.cpp")
    TARGET_INCLUDE_DIRECTORIES(unified-manual-test PRIVATE "${CMAKE_BINARY_DIR}/src/stages/deskew")
    TARGET_LINK_LIBRARIES(unified-manual-test
        "$<LINK_GROUP:RESCAN,acceleration,page_layout,output,fix_orientation,page_split,deskew,select_content,stcore,dewarping,zones,interaction,imageproc,math,foundation>" ${EXTRA_LIBS})
    SET_TARGET_PROPERTIES(unified-manual-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    ADD_EXECUTABLE(unified-content-test EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/../../tests/native_content_review.cpp")
    TARGET_LINK_LIBRARIES(unified-content-test
        "$<LINK_GROUP:RESCAN,acceleration,page_layout,output,fix_orientation,page_split,deskew,select_content,stcore,dewarping,zones,interaction,imageproc,math,foundation>" ${EXTRA_LIBS})
    SET_TARGET_PROPERTIES(unified-content-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    ADD_EXECUTABLE(unified-edit-audit EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/../../tests/native_edit_audit.cpp")
    TARGET_LINK_LIBRARIES(unified-edit-audit
        "$<LINK_GROUP:RESCAN,acceleration,page_layout,output,fix_orientation,page_split,deskew,select_content,stcore,dewarping,zones,interaction,imageproc,math,foundation>" ${EXTRA_LIBS})
    SET_TARGET_PROPERTIES(unified-edit-audit PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
ENDIF()
SET_TARGET_PROPERTIES(scantailor-experimental-cli PROPERTIES OUTPUT_NAME "${STOU_CLI_OUTPUT_NAME}")

IF(APPLE)
    SET_TARGET_PROPERTIES(scantailor-experimental-cli PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
ENDIF()

TARGET_LINK_LIBRARIES(
    scantailor-experimental-cli
    acceleration page_layout output
    fix_orientation page_split deskew select_content stcore
    dewarping zones interaction imageproc math foundation
    ${EXTRA_LIBS}
)
SET_TARGET_PROPERTIES(
    scantailor-experimental-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

