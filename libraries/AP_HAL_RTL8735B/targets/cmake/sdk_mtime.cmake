# SDK 的 config.cmake 每次 configure 都以 sed -i 重寫幾個 SDK 檔案（platform_conf.h、
# partition_rtl8735b.h、連結腳本）。預設值（ASIC、B-cut、DDR 128M）下內容不變，但檔案時間被更新，
# make 因此把所有依賴它們的 SDK 物件判定過期而整份重編。SDK 由所有 worktree 共用，任何一處
# configure 都會讓其他 worktree 的 SDK 端也整份重編。
#
# rtl8735b_sdk_mtime_save() 在 include config.cmake 前保存這些檔案（cp -p，連同時間）；
# rtl8735b_sdk_mtime_restore() 在 include 之後比對內容：內容未變就把時間還原成 configure 前的值，
# 內容有變（例如改了 CUTVER、DDR）則保留新時間，照常重編。只動檔案時間，不改 SDK 內容。
# 需要 cp、touch（GNU coreutils）；config.cmake 本身已經要求 bash 與 sed。
#
# TODO(rtl8735b): 這是繞過 SDK config.cmake 的副作用；SDK 換版或 config.cmake 改寫的檔案清單變了，
# 要重新核對下面的清單（GCC-RELEASE/config.cmake:207–254）。
# config.cmake 另外每次以 configure_file 重寫 inc/build_info.h（內含 configure 的時間，內容每次不同），
# 只有 isp_osd_lite.c 引用它，維持 SDK 原行為不處理。

set(rtl8735b_sdk_sed_targets
    ${AMEBAPRO2_SDK}/component/soc/8735b/cmsis/rtl8735b/include/platform_conf.h
    ${AMEBAPRO2_SDK}/component/soc/8735b/cmsis/rtl8735b/include/partition_rtl8735b.h
    ${AMEBAPRO2_SDK}/project/realtek_amebapro2_v0_example/GCC-RELEASE/application/rtl8735b_ram.ld
    ${AMEBAPRO2_SDK}/project/realtek_amebapro2_v0_example/GCC-RELEASE/application/rtl8735b_ram_ns.ld
    ${AMEBAPRO2_SDK}/project/realtek_amebapro2_v0_example/GCC-RELEASE/application/rtl8735b_ram_s.ld
    ${AMEBAPRO2_SDK}/project/realtek_amebapro2_v0_example/GCC-RELEASE/bootloader/rtl8735b_boot_mp.ld
)

function(rtl8735b_sdk_mtime_save saved_dir)
    file(REMOVE_RECURSE ${saved_dir})
    file(MAKE_DIRECTORY ${saved_dir})
    foreach(f IN LISTS rtl8735b_sdk_sed_targets)
        # 清單中有些檔案在 SDK 裡不存在（config.cmake 的 sed 對它們只會報錯），略過
        if(EXISTS ${f})
            string(MD5 key "${f}")
            execute_process(COMMAND cp -p ${f} ${saved_dir}/${key} RESULT_VARIABLE rc)
            if(NOT rc EQUAL 0)
                message(FATAL_ERROR "cannot save ${f} before config.cmake (cp -p rc=${rc})")
            endif()
        endif()
    endforeach()
endfunction()

function(rtl8735b_sdk_mtime_restore saved_dir)
    foreach(f IN LISTS rtl8735b_sdk_sed_targets)
        string(MD5 key "${f}")
        set(saved ${saved_dir}/${key})
        if(EXISTS ${saved} AND EXISTS ${f})
            file(SHA256 ${f} after)
            file(SHA256 ${saved} before)
            if(after STREQUAL before)
                execute_process(COMMAND touch -c -m -r ${saved} ${f} RESULT_VARIABLE rc)
                if(NOT rc EQUAL 0)
                    message(WARNING "cannot restore timestamp of ${f} (touch rc=${rc}); SDK objects may rebuild")
                endif()
            else()
                message(STATUS "config.cmake changed ${f}; keeping the new timestamp")
            endif()
        endif()
    endforeach()
    file(REMOVE_RECURSE ${saved_dir})
endfunction()
