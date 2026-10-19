# -*- coding: utf-8 -*-
!ifndef NSQUICKJS_INCLUDED
!define NSQUICKJS_INCLUDED

; NSQUICKJS_PLUGIN_DIR 指向包含 x86-unicode 和 amd64-unicode 的目录。
!ifdef NSQUICKJS_PLUGIN_DIR
  !if ${NSIS_CHAR_SIZE} != 2
    !error "nsQuickJs 仅支持 Unicode 安装器"
  !endif
  !if ${NSIS_PTR_SIZE} == 8
    !addplugindir /amd64-unicode "${NSQUICKJS_PLUGIN_DIR}\amd64-unicode"
  !else
    !addplugindir /x86-unicode "${NSQUICKJS_PLUGIN_DIR}\x86-unicode"
  !endif
!endif

; 所有宏先弹出 STATUS（0 或 E_*），再弹出 RESULT（JSON 或错误说明）。
; /NOUNLOAD 保证多次调用共享同一个 DLL 和 JS 上下文。
; CONFIG 为含 rootDir 的 JSON 对象；成功后 RESULT 为空。
!macro NsQuickJs_Init CONFIG STATUS RESULT
  nsQuickJs::Init /NOUNLOAD `${CONFIG}`
  Pop ${STATUS}
  Pop ${RESULT}
!macroend
; 执行 SOURCE 全局脚本，返回最后表达式的 JSON；undefined 返回空串。
!macro NsQuickJs_Eval SOURCE STATUS RESULT
  nsQuickJs::Eval /NOUNLOAD `${SOURCE}`
  Pop ${STATUS}
  Pop ${RESULT}
!macroend
; 读取并执行 rootDir 内 PATH 指定的 UTF-8 脚本，路径可相对根目录。
!macro NsQuickJs_EvalFile PATH STATUS RESULT
  nsQuickJs::EvalFile /NOUNLOAD `${PATH}`
  Pop ${STATUS}
  Pop ${RESULT}
!macroend
; 将 PATH 模块绑定到唯一非空 ALIAS，处理顶层 await；成功时 RESULT 为空。
!macro NsQuickJs_LoadModule ALIAS PATH STATUS RESULT
  nsQuickJs::LoadModule /NOUNLOAD `${ALIAS}` `${PATH}`
  Pop ${STATUS}
  Pop ${RESULT}
!macroend
; 调用 ALIAS 模块的 FUNCTION；空 ALIAS 表示全局函数，ARGS 必须为 JSON 数组。
!macro NsQuickJs_Call ALIAS FUNCTION ARGS STATUS RESULT
  nsQuickJs::Call /NOUNLOAD `${ALIAS}` `${FUNCTION}` `${ARGS}`
  Pop ${STATUS}
  Pop ${RESULT}
!macroend
; ENABLED 必须为 true 或 false；仅改变 fs 权限，不影响 EvalFile 和模块加载。
!macro NsQuickJs_SetFileAccess ENABLED STATUS RESULT
  nsQuickJs::SetFileAccess /NOUNLOAD `${ENABLED}`
  Pop ${STATUS}
  Pop ${RESULT}
!macroend
; 幂等销毁上下文并释放资源，成功时 STATUS 为 0、RESULT 为空。
!macro NsQuickJs_Destroy STATUS RESULT
  nsQuickJs::Destroy /NOUNLOAD
  Pop ${STATUS}
  Pop ${RESULT}
!macroend
!endif
