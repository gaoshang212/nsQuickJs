// 故意导入不存在的文件，验证文件读取错误经模块加载器转为 JS 异常。
import './missing-file.mjs';
