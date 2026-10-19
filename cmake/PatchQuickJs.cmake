# v0.17.0 的模块定义由上下文直接释放，但异步闭包仍可能持有模块值。
# 在构建目录生成修正版，保留原始依赖；只延迟结构体释放，不改变模块求值。
# nsq_qjs_source 保存待修补的源码文本；后续只写入构建目录副本。
file(READ "${quickjs_SOURCE_DIR}/quickjs.c" nsq_qjs_source)
# old/new_tag 为模块值最后一次引用释放的替换前后片段。
set(nsq_old_tag "    case JS_TAG_MODULE:\n        abort(); /* never freed here */\n        break;")
set(nsq_new_tag "    case JS_TAG_MODULE:\n        /* nsQuickJs：最后一个模块值释放后回收定义。 */\n        js_free_rt(rt, JS_VALUE_GET_PTR(v));\n        break;")
# old/new_free 为上下文清理模块成员后的结构体释放逻辑。
set(nsq_old_free "    list_del(&m->link);\n    js_free(ctx, m);\n}")
set(nsq_new_free "    list_del(&m->link);\n    /* nsQuickJs：成员清理后释放上下文持有的引用。 */\n    if (--JS_REF_COUNT(m) == 0)\n        js_free(ctx, m);\n}")
# 两个原始片段必须同时存在；上游变化时停止构建，要求人工重新核对补丁。
foreach(fragment IN ITEMS nsq_old_tag nsq_old_free)
  string(FIND "${nsq_qjs_source}" "${${fragment}}" nsq_position)
  if(nsq_position EQUAL -1)
    message(FATAL_ERROR "QuickJS 源码不匹配，须重新审查模块生命周期补丁")
  endif()
endforeach()
string(REPLACE "${nsq_old_tag}" "${nsq_new_tag}" nsq_qjs_source "${nsq_qjs_source}")
string(REPLACE "${nsq_old_free}" "${nsq_new_free}" nsq_qjs_source "${nsq_qjs_source}")
# 生成文件路径；file(CONFIGURE) 仅在内容变化时更新，避免无效重编译。
set(nsq_patched "${CMAKE_CURRENT_BINARY_DIR}/quickjs-patched/quickjs.c")
file(CONFIGURE OUTPUT "${nsq_patched}" CONTENT "${nsq_qjs_source}" @ONLY)
# 上游 qjs 源文件列表，以及替换 quickjs.c 后采用的绝对路径列表。
get_target_property(nsq_sources qjs SOURCES)
set(nsq_absolute_sources)
# 跨目录重设 SOURCES 时显式绝对化原路径，避免被解释为本项目内的路径。
foreach(source IN LISTS nsq_sources)
  if(source STREQUAL "quickjs.c")
    list(APPEND nsq_absolute_sources "${nsq_patched}")
  elseif(IS_ABSOLUTE "${source}")
    list(APPEND nsq_absolute_sources "${source}")
  else()
    list(APPEND nsq_absolute_sources "${quickjs_SOURCE_DIR}/${source}")
  endif()
endforeach()
set_property(TARGET qjs PROPERTY SOURCES "${nsq_absolute_sources}")
