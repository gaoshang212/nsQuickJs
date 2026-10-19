# -*- coding: utf-8 -*-
Unicode true
Target ${TEST_TARGET}
SetCompressor zlib
Name "nsQuickJs 自动测试"
OutFile "${TEST_OUTPUT}"
RequestExecutionLevel user
SilentInstall silent
!define NSQUICKJS_PLUGIN_DIR "${TEST_PLUGINS}"
!include "${TEST_SOURCE}\include\nsQuickJs.nsh"
!include StrFunc.nsh
${StrRep}

; 比较插件的 $0 状态和 $1 内容，失败跳转到统一诊断出口。
!macro Check STATUS RESULT
  StrCmp $0 `${STATUS}` +2
    Goto failed
  StrCmp $1 `${RESULT}` +2
    Goto failed
!macroend
; 只比较状态，适用于错误消息含堆栈或其他不稳定内容的场景。
!macro Status STATUS
  StrCmp $0 `${STATUS}` +2
    Goto failed
!macroend

; 在真实安装器内验证宏、Unicode、跨调用状态、错误恢复和卸载清理。
; $0/$1 为返回值，$2 为脚本路径，$3 为复用配置，$4 为失败日志句柄。
Section
  InitPluginsDir
  SetOutPath "$PLUGINSDIR\中文脚本"
  File /r "${TEST_SOURCE}\tests\fixtures\*"
  ${StrRep} $2 "$OUTDIR" "\" "/"
  StrCpy $3 '{"rootDir":"$2","timeoutMs":100,"memoryLimitMiB":8}'
  !insertmacro NsQuickJs_Init '$3' $0 $1
  !insertmacro Check '0' ''
  ; 验证默认禁用、宿主动态授权、中文文件读写和撤销后的引用检查。
  !insertmacro NsQuickJs_Eval 'try { fs.exists("basic.js"); } catch(e) { e.code; }' $0 $1
  !insertmacro Check '0' '"E_PERMISSION"'
  !insertmacro NsQuickJs_SetFileAccess 'true' $0 $1
  !insertmacro Check '0' ''
  !insertmacro NsQuickJs_Eval 'fs.writeText("文件.txt", "你好😀"); fs.appendText("文件.txt", "!"); fs.readText("文件.txt")' $0 $1
  !insertmacro Check '0' '"你好😀!"'
  !insertmacro NsQuickJs_Eval 'fs.remove("文件.txt")' $0 $1
  !insertmacro Check '0' 'true'
  !insertmacro NsQuickJs_Eval 'globalThis.savedExists = fs.exists; undefined' $0 $1
  !insertmacro Check '0' ''
  !insertmacro NsQuickJs_SetFileAccess 'false' $0 $1
  !insertmacro Check '0' ''
  !insertmacro NsQuickJs_Eval 'try { savedExists("basic.js"); } catch(e) { e.code; }' $0 $1
  !insertmacro Check '0' '"E_PERMISSION"'
  !insertmacro NsQuickJs_Eval 'globalThis.counter = 10; counter' $0 $1
  !insertmacro Check '0' '10'
  !insertmacro NsQuickJs_Eval '++counter' $0 $1
  !insertmacro Check '0' '11'
  !insertmacro NsQuickJs_Eval '"你好😀"' $0 $1
  !insertmacro Check '0' '"你好😀"'
  !insertmacro NsQuickJs_EvalFile 'basic.js' $0 $1
  !insertmacro Check '0' '12'
  !insertmacro NsQuickJs_Call '' 'add' '[7,8]' $0 $1
  !insertmacro Check '0' '15'
  !insertmacro NsQuickJs_LoadModule 'entry' 'entry.mjs' $0 $1
  !insertmacro Check '0' ''
  !insertmacro NsQuickJs_Call 'entry' 'compute' '[3,4]' $0 $1
  !insertmacro Check '0' '7'
  !insertmacro NsQuickJs_Call 'entry' 'getReady' '[]' $0 $1
  !insertmacro Check '0' '42'
  !insertmacro NsQuickJs_Eval '"x".repeat(2000)' $0 $1
  !insertmacro Status 'E_RESULT_TOO_LONG'
  !insertmacro NsQuickJs_Eval 'throw new Error("测试异常")' $0 $1
  !insertmacro Status 'E_JS'
  !insertmacro NsQuickJs_Eval 'counter' $0 $1
  !insertmacro Check '0' '12'
  !insertmacro NsQuickJs_Eval 'while(true){}' $0 $1
  !insertmacro Status 'E_TIMEOUT'
  !insertmacro NsQuickJs_Eval '1' $0 $1
  !insertmacro Status 'E_STATE'
  !insertmacro NsQuickJs_Destroy $0 $1
  !insertmacro Check '0' ''
  !insertmacro NsQuickJs_Init '$3' $0 $1
  !insertmacro Check '0' ''
  !insertmacro NsQuickJs_Eval 'typeof counter' $0 $1
  !insertmacro Check '0' '"undefined"'
  !insertmacro NsQuickJs_LoadModule 'pending' 'pending-held.mjs' $0 $1
  !insertmacro Status 'E_PENDING'
  ; 保留上下文退出，验证 NSIS 的卸载回调。
  SetErrorLevel 0
  Quit
; 将诊断写为 UTF-16LE，便于外部 PowerShell 测试脚本读取并报告失败。
failed:
  FileOpen $4 "${TEST_OUTPUT}.failure.txt" w
  FileWriteUTF16LE $4 "$0: $1"
  FileClose $4
  SetErrorLevel 1
SectionEnd
