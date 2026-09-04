set(ZYNQ_SCOPE_CORE_SOURCES
    core/compilercompat.h
    acquisition/datablock.h
    acquisition/idatasource.h
    acquisition/idatasource.cpp
    acquisition/simulationdatasource.h
    acquisition/simulationdatasource.cpp
    acquisition/filereplaydatasource.h
    acquisition/filereplaydatasource.cpp
    acquisition/dmacapturedatasource.h
    acquisition/dmacapturedatasource.cpp
    acquisition/serialdatasource.h
    acquisition/serialdatasource.cpp
    acquisition/tcpdatasource.h
    acquisition/tcpdatasource.cpp
    protocol/scopelinkprotocol.h
    protocol/scopelinkprotocol.cpp
    plotting/sampleringbuffer.h
    plotting/sampleringbuffer.cpp
    plotting/scopeaxistransform.h
    plotting/scopeaxistransform.cpp
    plotting/waveformwidget.h
    plotting/waveformwidget.cpp
    plotting/spectrumwidget.h
    plotting/spectrumwidget.cpp
    processing/triggerengine.h
    processing/triggerengine.cpp
    processing/measurementengine.h
    processing/measurementengine.cpp
    processing/mathengine.h
    processing/mathengine.cpp
    processing/fftengine.h
    processing/fftengine.cpp
    processing/fftworker.h
    processing/fftworker.cpp
    processing/dacfunctionexpression.h
    processing/dacfunctionexpression.cpp
    storage/waveformfilecodec.h
    storage/waveformfilecodec.cpp
    storage/fileworker.h
    storage/fileworker.cpp
    ui/mainwindow.h
    ui/mainwindow.cpp
    ui/boardlinkwidget.h
    ui/boardlinkwidget.cpp
    ui/dacfunctionpreview.h
    ui/dacfunctionpreview.cpp
)

add_library(zynq_scope_core STATIC ${ZYNQ_SCOPE_CORE_SOURCES})
target_include_directories(zynq_scope_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(zynq_scope_core PUBLIC
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Gui
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Widgets
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Network
)
if(MSVC)
    target_compile_options(zynq_scope_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(zynq_scope_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(ac880_zynq_scope WIN32 main.cpp)

target_include_directories(ac880_zynq_scope PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ac880_zynq_scope PRIVATE
    zynq_scope_core
)

if(ZYNQ_SCOPE_QT_MAJOR_VERSION EQUAL 6)
    qt_finalize_executable(ac880_zynq_scope)
endif()

install(TARGETS ac880_zynq_scope
    RUNTIME DESTINATION bin
)
