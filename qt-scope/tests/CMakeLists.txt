find_package(Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION} REQUIRED COMPONENTS Test)

add_executable(test_scopelink_protocol
    test_scopelinkprotocol.cpp
    ../src/protocol/scopelinkprotocol.cpp
    ../src/protocol/scopelinkprotocol.h
)
target_include_directories(test_scopelink_protocol PRIVATE ../src)
target_link_libraries(test_scopelink_protocol PRIVATE
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Test
)

add_executable(test_ring_buffer
    test_ringbuffer.cpp
    ../src/plotting/sampleringbuffer.cpp
    ../src/plotting/sampleringbuffer.h
    ../src/acquisition/datablock.h
)

target_include_directories(test_ring_buffer PRIVATE ../src)
target_link_libraries(test_ring_buffer PRIVATE
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Test
)

add_executable(test_scope_axis_transform
    test_scopeaxistransform.cpp
    ../src/plotting/scopeaxistransform.cpp
    ../src/plotting/scopeaxistransform.h
)
target_include_directories(test_scope_axis_transform PRIVATE ../src)
target_link_libraries(test_scope_axis_transform PRIVATE
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Test
)


add_executable(test_trigger_engine
    test_triggerengine.cpp
    ../src/processing/triggerengine.cpp
    ../src/processing/triggerengine.h
    ../src/acquisition/datablock.h
)
target_include_directories(test_trigger_engine PRIVATE ../src)
target_link_libraries(test_trigger_engine PRIVATE
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Test
)

add_executable(test_measurement_engine
    test_measurementengine.cpp
    ../src/processing/measurementengine.cpp
    ../src/processing/measurementengine.h
    ../src/acquisition/datablock.h
)
target_include_directories(test_measurement_engine PRIVATE ../src)
target_link_libraries(test_measurement_engine PRIVATE
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Test
)

add_executable(test_math_fft
    test_mathfft.cpp
    ../src/processing/mathengine.cpp
    ../src/processing/mathengine.h
    ../src/processing/fftengine.cpp
    ../src/processing/fftengine.h
    ../src/acquisition/datablock.h
)
target_include_directories(test_math_fft PRIVATE ../src)
target_link_libraries(test_math_fft PRIVATE
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Test
)

add_executable(test_waveform_file_codec
    test_waveformfilecodec.cpp
    ../src/storage/waveformfilecodec.cpp
    ../src/storage/waveformfilecodec.h
    ../src/acquisition/datablock.h
)
target_include_directories(test_waveform_file_codec PRIVATE ../src)
target_link_libraries(test_waveform_file_codec PRIVATE
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Test
)

add_executable(test_file_replay
    test_filereplaydatasource.cpp
    ../src/acquisition/idatasource.cpp
    ../src/acquisition/idatasource.h
    ../src/acquisition/filereplaydatasource.cpp
    ../src/acquisition/filereplaydatasource.h
    ../src/acquisition/datablock.h
)
target_include_directories(test_file_replay PRIVATE ../src)
target_link_libraries(test_file_replay PRIVATE
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Test
)

add_executable(test_dma_capture_data_source
    test_dmacapturedatasource.cpp
    ../src/acquisition/idatasource.cpp
    ../src/acquisition/idatasource.h
    ../src/acquisition/dmacapturedatasource.cpp
    ../src/acquisition/dmacapturedatasource.h
    ../src/acquisition/datablock.h
)
target_include_directories(test_dma_capture_data_source PRIVATE ../src)
target_link_libraries(test_dma_capture_data_source PRIVATE
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Test
)

add_executable(test_ui_smoke test_uismoke.cpp)
target_link_libraries(test_ui_smoke PRIVATE
    zynq_scope_core
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Gui
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Widgets
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Test
)

add_executable(test_dac_function
    test_dacfunctionexpression.cpp
    ../src/processing/dacfunctionexpression.cpp
    ../src/processing/dacfunctionexpression.h
)
target_include_directories(test_dac_function PRIVATE ../src)
target_link_libraries(test_dac_function PRIVATE
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core
    Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Test
)

if(WIN32)
    add_executable(test_hs_hardware test_hs_hardware.cpp)
    target_link_libraries(test_hs_hardware PRIVATE
        zynq_scope_core
        Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core
        Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Test
    )
endif()

if(WIN32)
    get_target_property(QT_CORE_LOCATION Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT_CORE_LOCATION)
        get_target_property(QT_CORE_LOCATION Qt${ZYNQ_SCOPE_QT_MAJOR_VERSION}::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT_RUNTIME_DIR "${QT_CORE_LOCATION}" DIRECTORY)
    get_filename_component(COMPILER_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(TEST_RUNTIME_PATH "${QT_RUNTIME_DIR};${COMPILER_RUNTIME_DIR};$ENV{Path}")
    add_test(NAME ring_buffer
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${TEST_RUNTIME_PATH}"
                $<TARGET_FILE:test_ring_buffer>)
    add_test(NAME scopelink_protocol
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${TEST_RUNTIME_PATH}"
                $<TARGET_FILE:test_scopelink_protocol>)
    add_test(NAME scope_axis_transform
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${TEST_RUNTIME_PATH}"
                $<TARGET_FILE:test_scope_axis_transform>)
    add_test(NAME trigger_engine
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${TEST_RUNTIME_PATH}"
                $<TARGET_FILE:test_trigger_engine>)
    add_test(NAME measurement_engine
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${TEST_RUNTIME_PATH}"
                $<TARGET_FILE:test_measurement_engine>)
    add_test(NAME math_fft
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${TEST_RUNTIME_PATH}"
                $<TARGET_FILE:test_math_fft>)
    add_test(NAME waveform_file_codec
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${TEST_RUNTIME_PATH}"
                $<TARGET_FILE:test_waveform_file_codec>)
    add_test(NAME file_replay
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${TEST_RUNTIME_PATH}"
                $<TARGET_FILE:test_file_replay>)
    add_test(NAME dma_capture_data_source
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${TEST_RUNTIME_PATH}"
                $<TARGET_FILE:test_dma_capture_data_source>)
    add_test(NAME ui_smoke
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${TEST_RUNTIME_PATH}"
                "QT_QPA_PLATFORM=offscreen"
                $<TARGET_FILE:test_ui_smoke>)
    add_test(NAME dac_function
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${TEST_RUNTIME_PATH}"
                $<TARGET_FILE:test_dac_function>)
    add_test(NAME hs_hardware
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${TEST_RUNTIME_PATH}"
                $<TARGET_FILE:test_hs_hardware>)
else()
    add_test(NAME scopelink_protocol COMMAND test_scopelink_protocol)
    add_test(NAME ring_buffer COMMAND test_ring_buffer)
    add_test(NAME scope_axis_transform COMMAND test_scope_axis_transform)
    add_test(NAME trigger_engine COMMAND test_trigger_engine)
    add_test(NAME measurement_engine COMMAND test_measurement_engine)
    add_test(NAME math_fft COMMAND test_math_fft)
    add_test(NAME waveform_file_codec COMMAND test_waveform_file_codec)
    add_test(NAME file_replay COMMAND test_file_replay)
    add_test(NAME dma_capture_data_source COMMAND test_dma_capture_data_source)
    add_test(NAME ui_smoke COMMAND test_ui_smoke)
    add_test(NAME dac_function COMMAND test_dac_function)
endif()
