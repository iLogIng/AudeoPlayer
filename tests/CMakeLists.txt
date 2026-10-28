# 每个基础组件测试都是独立可执行文件 便于 CTest 单独报告与设置超时
function(add_component_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE avplayer_core)
    target_compile_options(${name} PRIVATE -Wall -Wextra)

    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endfunction()

add_component_test(test_pcm_ring_buffer)
add_component_test(test_packet_queue)
add_component_test(test_video_frame_queue)
add_component_test(test_clocks)
add_component_test(test_media_timeline)
add_component_test(test_app_event)
