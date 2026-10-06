# SPDX-License-Identifier: GPL-3.0-or-later
# 建置時從 SDK 的 cmake 檔取出原始檔清單與打包步驟並執行，本庫不重列這些內容。
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(_rtl8735b_sdk_extract ${CMAKE_CURRENT_LIST_DIR}/sdk_extract.py)

# 以 sdk_extract.py 取出 file 中的片段放進 out_var；其餘參數照傳給 sdk_extract.py。
# 呼叫端以 cmake_language(EVAL CODE "${片段}") 在自己的範圍執行。
function(rtl8735b_sdk_extract out_var file)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${_rtl8735b_sdk_extract} ${file} ${ARGN}
        OUTPUT_VARIABLE code
        ERROR_VARIABLE err
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "無法從 SDK 取得建置片段：${err}")
    endif()
    # SDK 的 cmake 檔或擷取工具改了，下次建置要重新 configure
    if(NOT CMAKE_SCRIPT_MODE_FILE)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${file} ${_rtl8735b_sdk_extract})
    endif()
    set(${out_var} "${code}" PARENT_SCOPE)
endfunction()

# SDK 範例的原始檔清單（out_sources 與 app_sources），去掉範例本身：main.c、RTSP、NN 模型與工具。
function(rtl8735b_sdk_sources out_var)
    set(out_sources)
    set(app_sources)
    rtl8735b_sdk_extract(code ${prj_root}/GCC-RELEASE/application/application.cmake
        --region "#MBED" "if(DEFINED EXAMPLE AND EXAMPLE)")
    cmake_language(EVAL CODE "${code}")
    set(sources ${out_sources} ${app_sources})
    list(REMOVE_DUPLICATES sources)
    list(FILTER sources EXCLUDE REGEX
        "/src/main\\.c$|/src/test_model/|/mmfv2/module_rtsp2\\.c$|/mmfv2/module_vipnn\\.c$")
    # 本 HAL 直接依賴的檔案；SDK 換版後清單結構改變而少了它們時，在 configure 就停下
    foreach(required tasks.c port.c sockets.c libc_wrap.c flash_api.c)
        set(found FALSE)
        foreach(s IN LISTS sources)
            get_filename_component(name ${s} NAME)
            if(name STREQUAL required)
                set(found TRUE)
                break()
            endif()
        endforeach()
        if(NOT found)
            message(FATAL_ERROR "SDK 原始檔清單缺少 ${required}")
        endif()
    endforeach()
    set(${out_var} ${sources} PARENT_SCOPE)
endfunction()
