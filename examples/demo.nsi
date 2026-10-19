# -*- coding: utf-8 -*-
Unicode true
!ifndef DEMO_TARGET
  !define DEMO_TARGET x86-unicode
!endif
Target ${DEMO_TARGET}
SetCompressor zlib
Name "nsQuickJs 示例"
OutFile "demo-${DEMO_TARGET}.exe"
RequestExecutionLevel user
ShowInstDetails show
Page instfiles

!define NSQUICKJS_PLUGIN_DIR "..\Plugins"
!include "..\Include\nsQuickJs.nsh"
!include StrFunc.nsh
${StrRep}

; 检查 $0 状态码；失败时显示 $1 消息，并以非零退出码终止安装段。
!macro RequireSuccess
  StrCmp $0 '0' +4
    DetailPrint "$0: $1"
    SetErrorLevel 1
    Abort
!macroend

; 在临时目录演示求值、模块状态复用、Promise 返回及异常文本。
; $0/$1 为插件状态和结果，$2 为用于 JSON 配置的正斜杠脚本路径。
Section
  InitPluginsDir
  SetOutPath "$PLUGINSDIR\scripts"
  File /r "scripts\*"
  ; Windows 反斜杠须先转换，才能直接放入 JSON 字符串。
  ${StrRep} $2 "$OUTDIR" "\" "/"
  !insertmacro NsQuickJs_Init '{"rootDir":"$2"}' $0 $1
  !insertmacro RequireSuccess
  !insertmacro NsQuickJs_Eval '1 + 2' $0 $1
  !insertmacro RequireSuccess
  DetailPrint "表达式结果：$1"
  ; 只在文件示例执行期间授予权限；普通脚本文件使用 EvalFile 加载。
  !insertmacro NsQuickJs_SetFileAccess 'true' $0 $1
  !insertmacro RequireSuccess
  !insertmacro NsQuickJs_EvalFile 'files.js' $0 $1
  !insertmacro RequireSuccess
  DetailPrint "文件读写结果：$1"
  !insertmacro NsQuickJs_SetFileAccess 'false' $0 $1
  !insertmacro RequireSuccess
  !insertmacro NsQuickJs_LoadModule 'main' 'main.mjs' $0 $1
  !insertmacro RequireSuccess
  !insertmacro NsQuickJs_Call 'main' 'calculate' '[20,22]' $0 $1
  !insertmacro RequireSuccess
  DetailPrint "第一次调用：$1"
  !insertmacro NsQuickJs_Call 'main' 'calculate' '[1,2]' $0 $1
  !insertmacro RequireSuccess
  DetailPrint "第二次调用：$1"
  !insertmacro NsQuickJs_Call 'main' 'fail' '[]' $0 $1
  DetailPrint "异常信息：$0 / $1"
  !insertmacro NsQuickJs_Destroy $0 $1
  !insertmacro RequireSuccess
  SetErrorLevel 0
SectionEnd
