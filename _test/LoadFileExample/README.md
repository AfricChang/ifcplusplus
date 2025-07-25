# IFC to glTF Converter

一个基于IFCPlusPlus库的简单IFC到glTF转换器，可以将IFC文件转换为glTF格式。

## 功能特性

- 支持IFC文件加载和解析
- 提取IFC几何数据
- 转换为标准glTF 2.0格式
- 支持命令行参数配置
- 包含进度显示和日志输出
- 支持材质信息导出

## 编译要求

- 可直接用build.bat编译(仅关注release即可)
- Visual Studio 2022
- IFCPlusPlus库 (位于 `E:\github\ifcplusplus\IfcPlusPlus\src`)
- cgltf库 (位于 `E:\github\acTorus\Third\cgltf-1.15`)
- C++17标准支持


## 编译方法

1. 确保已安装Visual Studio 2022
2. 确保IFCPlusPlus库已编译并生成了`IfcPlusPlus.lib`
3. 运行编译脚本：
   ```cmd
   build.bat
   ```

## 使用方法

### 基本用法
```cmd
LoadFileExample.exe -i input.ifc -o output.gltf
```

### 命令行参数

- `-i, --input FILE`    输入IFC文件路径
- `-o, --output FILE`   输出glTF文件路径
- `-s, --scale FLOAT`   缩放因子 (默认: 1.0)
- `-l, --log LEVEL`     日志级别 (0=静默, 1=普通, 2=详细)
- `--glb`               输出GLB格式 (暂未实现)
- `-h, --help`          显示帮助信息


### data目录下有测试数据
- 测试数据产生的结果数据也需要放到data目录下
- example.ifc
- IfcOpenHouse.ifc

### 使用示例

```cmd
# 基本转换
LoadFileExample.exe -i example.ifc -o output.gltf

# 带缩放的转换
LoadFileExample.exe -i model.ifc -o model.gltf -s 0.001

# 详细日志输出
LoadFileExample.exe -i model.ifc -o model.gltf -l 2
```

## 输出格式

转换器生成标准的glTF 2.0 JSON文件，包含：

- **Asset信息**: 版本和生成器信息
- **Scene**: 场景结构
- **Nodes**: 节点层次结构
- **Meshes**: 网格数据引用
- **Materials**: 基础PBR材质

注意：当前版本生成的glTF文件不包含实际的几何数据缓冲区，仅包含结构信息。

## 项目结构

```
LoadFileExample/
├── src/
│   └── simple_ifc2gltf.cpp    # 主要转换器代码
├── bin/
│   └── LoadFileExample.exe    # 编译后的可执行文件
├── build.bat                  # 编译脚本
├── LoadFileExample.vcxproj    # Visual Studio项目文件
└── README.md                  # 本文档
```

## 技术实现

### 核心组件

1. **IFC加载**: 使用IFCPlusPlus的ReaderSTEP类加载IFC文件
2. **几何转换**: 使用GeometryConverter将IFC几何转换为Carve网格
3. **数据提取**: 遍历ProductShapeData提取顶点、法线和索引数据
4. **glTF输出**: 生成符合glTF 2.0规范的JSON文件

### 关键类

- `SimpleIFC2GLTF`: 主转换器类
- `MessageHandler`: 消息和进度处理
- `GeometryData`: 几何数据结构
- `GltfNode`: glTF节点数据结构

## 已知限制

1. 当前版本不生成实际的几何数据缓冲区
2. 材质信息较为简化
3. 不支持纹理和复杂材质属性
4. GLB格式输出尚未实现

## 测试结果

使用提供的`example.ifc`文件测试：
- 成功加载100个IFC实体
- 提取了6个几何对象
- 生成了有效的glTF JSON文件

## 开发环境

- Windows 10/11
- Visual Studio 2022 Enterprise
- IFCPlusPlus (最新版本)
- cgltf-1.15

## 许可证

本项目基于IFCPlusPlus库开发，请遵循相应的开源许可证。