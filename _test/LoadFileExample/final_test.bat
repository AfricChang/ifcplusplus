@echo off
chcp 65001
echo ========================================
echo IFC到glTF转换器 - 最终功能测试
echo ========================================

echo.
echo 测试1: 基本转换（带材质）
bin\LoadFileExample.exe -i data\example.ifc -o data\final_basic.gltf -l 1

echo.
echo 测试2: GLB格式输出（带材质）
bin\LoadFileExample.exe -i data\example.ifc -o data\final_glb.glb --glb -l 1

echo.
echo 测试3: 缩放转换
bin\LoadFileExample.exe -i data\example.ifc -o data\final_scaled.gltf -s 0.001 -l 1

echo.
echo ========================================
echo 测试完成！生成的文件：
echo ========================================
dir final_*.* /b

echo.
echo 材质验证 - 查看材质数量：
findstr "materials" final_basic.gltf
findstr "baseColorFactor" final_basic.gltf | find /c "baseColorFactor"

echo.
echo ========================================
echo 所有功能测试完成！
echo ========================================
pause