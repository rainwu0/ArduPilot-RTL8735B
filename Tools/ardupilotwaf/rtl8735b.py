# encoding: utf-8
# SPDX-License-Identifier: GPL-3.0-or-later

# flake8: noqa

"""
RTL8735B 的 waf 工具：ArduPilot 由 waf 編成靜態庫，再交給
libraries/AP_HAL_RTL8735B/targets/cmake 的 cmake 專案與 Realtek SDK 連結、產生映像。
SDK 位置由環境變數 AMEBAPRO2_SDK 指定，預設是原始碼樹旁的 ../ambpro2_sdk。
"""

from waflib import Logs, Task
from waflib.TaskGen import after_method, feature
from collections import OrderedDict

import os
import signal
import sys
import traceback

import hal_common
import rtl8735b_upload

sys.path.append(os.path.join(os.path.dirname(os.path.realpath(__file__)), '../../libraries/AP_HAL_RTL8735B/hwdef/scripts'))
import rtl8735b_hwdef
import rtl8735b_wifi_config


def configure(cfg):
    bldnode = cfg.bldnode.make_node(cfg.variant)
    def srcpath(path):
        return cfg.srcnode.make_node(path).abspath()
    def bldpath(path):
        return bldnode.make_node(path).abspath()

    cfg.load('cmake')

    env = cfg.env
    env.AMEBAPRO2_SDK = os.environ.get(
        'AMEBAPRO2_SDK',
        os.path.normpath(os.path.join(cfg.srcnode.abspath(), '..', 'ambpro2_sdk')))
    if not os.path.isdir(os.path.join(env.AMEBAPRO2_SDK, 'project', 'realtek_amebapro2_v0_example')):
        cfg.fatal("RTL8735B SDK not found at %s (set AMEBAPRO2_SDK)" % env.AMEBAPRO2_SDK)
    cfg.msg('Using RTL8735B SDK', env.AMEBAPRO2_SDK)

    env.AP_PROGRAM_FEATURES += ['rtl8735b_ap_program']
    env.BUILDROOT = bldpath('')
    env.SRCROOT = srcpath('')

    try:
        hwdef_obj = generate_hwdef_h(env)
    except Exception:
        traceback.print_exc()
        cfg.fatal("Failed to process hwdef.dat")
    hal_common.process_hwdef_results(cfg, hwdef_obj)


def generate_hwdef_h(env):
    '''run rtl8735b_hwdef.py'''
    hwdef_dir = os.path.join(env.SRCROOT, 'libraries/AP_HAL_RTL8735B/hwdef')

    if len(env.HWDEF) == 0:
        env.HWDEF = os.path.join(hwdef_dir, env.BOARD, 'hwdef.dat')
    hwdef_out = env.BUILDROOT
    if not os.path.exists(hwdef_out):
        os.mkdir(hwdef_out)
    hwdef = [env.HWDEF]
    if env.HWDEF_EXTRA:
        hwdef.append(env.HWDEF_EXTRA)

    hwdef_obj = rtl8735b_hwdef.RTL8735BHWDef(
        outdir=hwdef_out,
        hwdef=hwdef,
        quiet=False,
    )
    hwdef_obj.run()

    return hwdef_obj


def generate_wifi_credentials(bld):
    '''Wi-Fi STA 帳密在建置時（不是 configure 時）由環境變數 RTL8735B_WIFI_CONFIG 指向的本機檔案產生，
    不進 waf 的設定快取與編譯命令列；未設定或檔案不存在時產生停用版本，建置照常成功'''
    config = os.environ.get('RTL8735B_WIFI_CONFIG') or None
    out = bld.bldnode.make_node('rtl8735b/wifi_credentials.c').abspath()
    try:
        status = rtl8735b_wifi_config.generate(config, out)
    except rtl8735b_wifi_config.WifiConfigError as e:
        bld.fatal('Wi-Fi STA 設定錯誤（%s）：%s' % (config, e))
    if status == 'configured':
        print('Wi-Fi STA: 帳密取自 %s' % config)
    else:
        print('Wi-Fi STA: 未設定（RTL8735B_WIFI_CONFIG=%s），映像中的 Wi-Fi 不啟用' % (config or '未指定'))
    return out


def save_build_state_on_hangup():
    '''waf 只在建置結束或收到 Ctrl-C（KeyboardInterrupt）時把各工作的簽章寫進 .wafpickle；
    被其他訊號直接終止時，已完成的編譯都不記錄，下一次從頭整份重編，SDK 端的 cmake configure
    也會重跑。背景工作逾時或終端關閉時 WSL 送的是 SIGHUP，因此把 SIGHUP、
    SIGTERM 與 Ctrl-C 同樣處理：waf 先保存已完成的部分再結束'''
    for name in ('SIGHUP', 'SIGTERM'):
        sig = getattr(signal, name, None)
        if sig is not None:
            signal.signal(sig, signal.default_int_handler)


def pre_build(self):
    """設定 SDK 端的 cmake 專案；先產生 includes.list 與 defines.list，給 waf 編譯 ArduPilot 用"""
    save_build_state_on_hangup()
    lib_vars = OrderedDict()
    lib_vars['RTL8735B_WIFI_CREDENTIALS_SOURCE'] = generate_wifi_credentials(self)
    lib_vars['ARDUPILOT_CMD'] = self.cmd
    lib_vars['WAF_BUILD_TARGET'] = self.targets
    lib_vars['ARDUPILOT_LIB'] = self.bldnode.find_or_declare('lib/').abspath()
    lib_vars['ARDUPILOT_BIN'] = self.bldnode.find_or_declare('lib/bin').abspath()
    lib_vars['AMEBAPRO2_SDK'] = self.env.AMEBAPRO2_SDK
    lib_vars['CMAKE_TOOLCHAIN_FILE'] = os.path.join(
        self.env.AMEBAPRO2_SDK, 'project/realtek_amebapro2_v0_example/GCC-RELEASE/toolchain.cmake')
    sdk = self.cmake(
        name='rtl8735b-sdk',
        cmake_vars=lib_vars,
        cmake_src='libraries/AP_HAL_RTL8735B/targets/cmake',
        cmake_bld='sdk_build',
    )

    sdk_showinc = sdk.build('showinc', target='sdk_build/includes.list')
    sdk_showinc.post()

    def unique(items):
        '''去重並保留順序。cmake 每次 configure 產生的清單重複次數不一定相同
        （toolchain.cmake 的 add_definitions 依語言啟用次數重複），不去重會讓旗標改變、整份重編'''
        seen = set()
        out = []
        for i in items:
            if i not in seen:
                seen.add(i)
                out.append(i)
        return out

    class load_generated_lists(Task.Task):
        """把 cmake 專案吐出的 include 目錄與巨集加進 waf 的環境"""
        always_run = True
        def run(tsk):
            bld = tsk.generator.bld
            includes = unique(bld.bldnode.find_or_declare('sdk_build/includes.list').read().split())
            bld.env.prepend_value('INCLUDES', includes)
            defines = unique(bld.bldnode.find_or_declare('sdk_build/defines.list').read().split())
            flags = ['-D' + d for d in defines]
            bld.env.append_value('CFLAGS', flags)
            bld.env.append_value('CXXFLAGS', flags)

    tsk = load_generated_lists(env=self.env)
    tsk.set_inputs(self.path.find_resource('sdk_build/includes.list'))
    self.add_to_group(tsk)


class rtl8735b_upload_fw(Task.Task):
    '''--upload：建置完成後以 uartfwburn 燒錄 flash_ntz.bin（見 rtl8735b_upload.py）'''
    color = 'BLUE'
    always_run = True

    def run(self):
        bld = self.generator.bld
        try:
            rtl8735b_upload.upload(
                image=self.inputs[0].abspath(),
                port=bld.options.upload_port,
                sdk=bld.env.AMEBAPRO2_SDK,
                workdir=bld.bldnode.make_node('pg_tool').abspath(),
                override=os.environ.get('AP_OVERRIDE_UPLOAD_CMD'))
        except rtl8735b_upload.UploadError as e:
            Logs.error(str(e))
            return 1
        return 0


@feature('rtl8735b_ap_program')
@after_method('process_source', 'apply_link')
def rtl8735b_firmware(self):
    self.link_task.always_run = True
    sdk = self.bld.cmake('rtl8735b-sdk')

    build = sdk.build('flash', target='sdk_build/flash_ntz.bin')
    build.post()

    build.cmake_build_task.set_run_after(self.link_task)

    # 所有程式共用同一份 SDK 映像，只燒一次；缺燒錄埠等錯誤在建置開始前就停下
    if self.bld.options.upload and not getattr(self.bld, 'rtl8735b_upload_posted', False):
        try:
            rtl8735b_upload.check_static(self.bld.options.upload_port, os.environ.get('AP_OVERRIDE_UPLOAD_CMD'))
        except rtl8735b_upload.UploadError as e:
            self.bld.fatal(str(e))
        self.bld.rtl8735b_upload_posted = True
        upload = self.create_task('rtl8735b_upload_fw', src=build.cmake_build_task.outputs[0])
        upload.set_run_after(build.cmake_build_task)
