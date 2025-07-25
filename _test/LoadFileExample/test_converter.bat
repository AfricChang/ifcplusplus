@echo off
echo 测试IFC到glTF转换器
echo ==================

echo.
echo 测试1: 转换example.ifc到glTF格式
bin\LoadFileExample.exe -i data\example.ifc -o data\example_output.gltf -l 1

echo.
echo 测试2: 转换example.ifc到GLB格式
bin\LoadFileExample.exe -i data\example.ifc -o data\example_output.glb --glb -l 1

echo.
echo 测试3: 使用缩放因子转换
bin\LoadFileExample.exe -i data\example.ifc -o data\example_scaled.gltf -s 0.001 -l 1

echo.
echo 测试4: 转换IfcOpenHouse.ifc
bin\LoadFileExample.exe -i data\IfcOpenHouse.ifc -o data\house_output.gltf -l 1

echo.
echo 测试5: 转换IfcOpenHouse.ifc
bin\LoadFileExample.exe -i data\IfcOpenHouse.ifc -o data\house_output.glb --glb -l 1

echo.
echo 测试完成！检查生成的文件：
dir *.gltf *.glb *.bin

pause