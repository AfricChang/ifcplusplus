/*
 * simple_ifc2gltf.cpp - 完整版IFC到glTF/GLB转换器
 * 
 * 支持输出glTF和GLB格式，包含完整的几何数据
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <unordered_set>
#include <mutex>

// IFCPlusPlus 核心头文件
#include <ifcpp/model/BuildingModel.h>
#include <ifcpp/reader/ReaderSTEP.h>
#include <ifcpp/geometry/GeometryConverter.h>
#include <ifcpp/geometry/GeomUtils.h>
#include <ifcpp/IFC4X3/include/IfcProject.h>
#include <ifcpp/IFC4X3/include/IfcSite.h>
#include <ifcpp/IFC4X3/include/IfcBuilding.h>
#include <ifcpp/IFC4X3/include/IfcBuildingStorey.h>
#include <ifcpp/IFC4X3/include/IfcProduct.h>
#include <ifcpp/IFC4X3/include/IfcObjectDefinition.h>
#include <ifcpp/IFC4X3/include/IfcRoot.h>
#include <ifcpp/IFC4X3/include/IfcSpatialStructureElement.h>
#include <ifcpp/IFC4X3/include/IfcRelAggregates.h>
#include <ifcpp/IFC4X3/include/IfcRelContainedInSpatialStructure.h>
#include <ifcpp/IFC4X3/include/IfcGloballyUniqueId.h>
#include <ifcpp/IFC4X3/include/IfcLabel.h>
#include <ifcpp/IFC4X3/include/IfcText.h>

// Carve 几何库
#include <carve/mesh.hpp>
#include <carve/geom.hpp>

using namespace IFC4X3;

/**
 * 命令行参数结构体
 */
struct CommandLineArgs
{
    std::string inputFile;          // IFC输入文件路径
    std::string outputFile;         // glTF输出文件路径
    float scale = 1.0f;            // 缩放因子
    int logLevel = 1;              // 日志级别 (0=静默, 1=普通, 2=详细)
    bool outputGlb = false;        // 是否输出GLB格式
};

/**
 * 几何数据结构
 */
struct GeometryData
{
    std::vector<float> vertices;    // 顶点坐标 (x,y,z)
    std::vector<float> normals;     // 法线向量 (x,y,z)
    std::vector<float> texCoords;   // 纹理坐标 (u,v)
    std::vector<uint32_t> indices;  // 三角形索引
    std::string materialId;         // 材质ID
    
    // 边界框
    float minBounds[3] = {0.0f, 0.0f, 0.0f};
    float maxBounds[3] = {0.0f, 0.0f, 0.0f};
    
    void CalculateBounds()
    {
        if (vertices.empty()) return;
        
        minBounds[0] = maxBounds[0] = vertices[0];
        minBounds[1] = maxBounds[1] = vertices[1];
        minBounds[2] = maxBounds[2] = vertices[2];
        
        for (size_t i = 3; i < vertices.size(); i += 3)
        {
            for (int j = 0; j < 3; ++j)
            {
                minBounds[j] = std::min(minBounds[j], vertices[i + j]);
                maxBounds[j] = std::max(maxBounds[j], vertices[i + j]);
            }
        }
    }
};

/**
 * 材质数据结构
 */
struct MaterialData
{
    std::string name;              // 材质名称
    float baseColorFactor[4] = { 0.8f, 0.8f, 0.8f, 1.0f }; // 基础颜色RGBA
    float metallicFactor = 0.0f;   // 金属度
    float roughnessFactor = 0.9f;  // 粗糙度
};

/**
 * glTF节点数据结构
 */
struct GltfNode
{
    std::string name;              // 节点名称
    std::string type;              // IFC类型
    std::vector<std::shared_ptr<GltfNode>> children; // 子节点
    std::vector<GeometryData> geometries; // 几何数据
    float transform[16] = {        // 变换矩阵 (4x4)
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

/**
 * 消息处理器
 */
class MessageHandler
{
public:
    MessageHandler() {}

    void slotMessageWrapper(shared_ptr<StatusCallback::Message> m)
    {
        std::lock_guard<std::mutex> lock(m_mutex_messages);
        
        StatusCallback::MessageType mType = m->m_message_type;
        if (mType == StatusCallback::MESSAGE_TYPE_PROGRESS_VALUE)
        {
            std::string progressType = m->m_progress_type;
            int progressPercent = int(m->m_progress_value * 100);
            std::cout << "\rProgress: " << progressPercent << "%" << std::flush;
        }
        else if (mType == StatusCallback::MESSAGE_TYPE_ERROR)
        {
            std::cerr << "Error: " << m->m_message_text << std::endl;
        }
        else if (mType == StatusCallback::MESSAGE_TYPE_WARNING)
        {
            std::cout << "Warning: " << m->m_message_text << std::endl;
        }
    }

    std::mutex m_mutex_messages;
};

/**
 * IFC到glTF转换器主类
 */
class SimpleIFC2GLTF
{
private:
    CommandLineArgs m_args;                                    // 命令行参数
    std::shared_ptr<BuildingModel> m_ifcModel;                // IFC模型
    std::shared_ptr<GeometryConverter> m_geometryConverter;   // 几何转换器
    std::map<std::string, MaterialData> m_materials;         // 材质映射
    std::shared_ptr<GltfNode> m_rootNode;                    // glTF根节点
    MessageHandler m_messageHandler;                          // 消息处理器

public:
    /**
     * 构造函数
     */
    SimpleIFC2GLTF(const CommandLineArgs& args) : m_args(args)
    {
        m_ifcModel = std::make_shared<BuildingModel>();
        
        // 创建几何设置
        shared_ptr<GeometrySettings> geom_settings(new GeometrySettings());
        m_geometryConverter = std::make_shared<GeometryConverter>(m_ifcModel, geom_settings);
        
        m_rootNode = std::make_shared<GltfNode>();
        m_rootNode->name = "Scene";
        m_rootNode->type = "Scene";
    }

    /**
     * 执行转换流程
     */
    bool Convert()
    {
        try
        {
            LogInfo("Starting IFC to " + std::string(m_args.outputGlb ? "GLB" : "glTF") + " conversion...");

            // 步骤1: 加载IFC文件
            if (!LoadIFCFile())
            {
                LogError("Failed to load IFC file: " + m_args.inputFile);
                return false;
            }

            // 步骤2: 转换几何数据
            if (!ConvertGeometry())
            {
                LogError("Failed to convert geometry");
                return false;
            }

            // 步骤3: 提取几何数据
            if (!ExtractGeometryData())
            {
                LogError("Failed to extract geometry data");
                return false;
            }

            // 步骤4: 输出文件
            if (!WriteOutputFile())
            {
                LogError("Failed to write output file: " + m_args.outputFile);
                return false;
            }

            LogInfo("Conversion completed successfully!");
            return true;
        }
        catch (const std::exception& e)
        {
            LogError("Exception during conversion: " + std::string(e.what()));
            return false;
        }
    }

private:
    /**
     * 加载IFC文件
     */
    bool LoadIFCFile()
    {
        LogInfo("Loading IFC file: " + m_args.inputFile);

        try
        {
            // 创建STEP读取器
            shared_ptr<ReaderSTEP> step_reader(new ReaderSTEP());
            step_reader->setMessageCallBack(std::bind(&MessageHandler::slotMessageWrapper, 
                &m_messageHandler, std::placeholders::_1));

            // 加载模型
            step_reader->loadModelFromFile(m_args.inputFile, m_ifcModel);

            // 检查模型是否有效
            if (!m_ifcModel || m_ifcModel->getMapIfcEntities().empty())
            {
                LogError("IFC model is empty or invalid");
                return false;
            }

            LogInfo("Successfully loaded " + std::to_string(m_ifcModel->getMapIfcEntities().size()) + " IFC entities");
            return true;
        }
        catch (const std::exception& e)
        {
            LogError("Error loading IFC file: " + std::string(e.what()));
            return false;
        }
    }

    /**
     * 转换几何数据
     */
    bool ConvertGeometry()
    {
        LogInfo("Converting IFC geometry...");

        try
        {
            // 设置消息回调
            m_geometryConverter->setMessageCallBack(std::bind(&MessageHandler::slotMessageWrapper, 
                &m_messageHandler, std::placeholders::_1));

            // 设置几何参数
            shared_ptr<GeometrySettings> geom_settings = m_geometryConverter->getGeomSettings();
            int numVerticesPerCircle = geom_settings->getNumVerticesPerCircle();
            LogInfo("Number of vertices per circle: " + std::to_string(numVerticesPerCircle));

            // 设置CSG精度
            m_geometryConverter->setCsgEps(1.5e-9);

            // 转换几何
            m_geometryConverter->convertGeometry();

            LogInfo("Geometry conversion completed");
            return true;
        }
        catch (const std::exception& e)
        {
            LogError("Error converting geometry: " + std::string(e.what()));
            return false;
        }
    }

    /**
     * 提取几何数据
     */
    bool ExtractGeometryData()
    {
        LogInfo("Extracting geometry data...");

        try
        {
            // 获取所有带几何的实体
            const std::unordered_map<std::string, shared_ptr<ProductShapeData>>& map_entities = 
                m_geometryConverter->getShapeInputData();

            LogInfo("Found " + std::to_string(map_entities.size()) + " entities with geometry");

            int geometryCount = 0;
            int totalTriangles = 0;
            int totalVertices = 0;
            
            for (auto it : map_entities)
            {
                shared_ptr<ProductShapeData> shapeData = it.second;
                if (!shapeData || shapeData->m_ifc_object_definition.expired())
                {
                    continue;
                }

                shared_ptr<IfcObjectDefinition> ifcObject = 
                    shared_ptr<IfcObjectDefinition>(shapeData->m_ifc_object_definition);

                // 创建节点
                auto node = std::make_shared<GltfNode>();
                node->name = GetEntityName(ifcObject);
                node->type = GetEntityType(ifcObject);

                // 提取几何数据
                ExtractShapeData(shapeData, node);

                if (!node->geometries.empty())
                {
                    m_rootNode->children.push_back(node);
                    geometryCount++;
                    
                    // 统计几何数据
                    for (auto& geom : node->geometries)
                    {
                        totalVertices += geom.vertices.size() / 3;
                        totalTriangles += geom.indices.size() / 3;
                    }
                }
            }

            LogInfo("Extracted geometry from " + std::to_string(geometryCount) + " objects");
            LogInfo("Total vertices: " + std::to_string(totalVertices) + ", triangles: " + std::to_string(totalTriangles));
            return true;
        }
        catch (const std::exception& e)
        {
            LogError("Error extracting geometry data: " + std::string(e.what()));
            return false;
        }
    }

    /**
     * 提取形状数据
     */
    void ExtractShapeData(shared_ptr<ProductShapeData>& shapeData, std::shared_ptr<GltfNode> node)
    {
        try
        {
            carve::math::Matrix localTransform = shapeData->getTransform();

            // 遍历几何项目
            for (auto geometricItem : shapeData->getGeometricItems())
            {
                ExtractGeometricItems(geometricItem, localTransform, node);
            }

            // 递归处理子元素
            for (auto child_object : shapeData->getChildElements())
            {
                ExtractShapeData(child_object, node);
            }
        }
        catch (const std::exception& e)
        {
            LogWarning("Failed to extract shape data for " + node->name + ": " + e.what());
        }
    }

    /**
     * 提取几何项目
     */
    void ExtractGeometricItems(shared_ptr<ItemShapeData>& geometricItem, 
        carve::math::Matrix& localTransform, std::shared_ptr<GltfNode> node)
    {
        GeometryData geomData;
        
        // 提取材质信息
        geomData.materialId = ExtractMaterialFromItem(geometricItem, node);

        // 处理封闭网格
        for (auto meshset : geometricItem->m_meshsets)
        {
            ExtractMeshSet(meshset, localTransform, geomData);
        }

        // 处理开放网格
        for (auto meshset : geometricItem->m_meshsets_open)
        {
            ExtractMeshSet(meshset, localTransform, geomData);
        }

        // 如果有几何数据，添加到节点
        if (!geomData.vertices.empty())
        {
            // 应用缩放
            ApplyScale(geomData, m_args.scale);
            
            // 计算边界框
            geomData.CalculateBounds();
            
            node->geometries.push_back(geomData);
            
            LogDebug("Added geometry with " + std::to_string(geomData.vertices.size() / 3) + 
                    " vertices and " + std::to_string(geomData.indices.size() / 3) + " triangles, material: " + geomData.materialId);
        }

        // 递归处理子项目
        for (auto childItem : geometricItem->m_child_items)
        {
            ExtractGeometricItems(childItem, localTransform, node);
        }
    }

    /**
     * 提取网格集合
     */
    void ExtractMeshSet(shared_ptr<carve::mesh::MeshSet<3>> meshset, 
        carve::math::Matrix& localTransform, GeometryData& geomData)
    {
        if (!meshset) return;

        for (auto mesh : meshset->meshes)
        {
            if (!mesh) continue;

            for (auto face : mesh->faces)
            {
                if (!face || face->n_edges < 3) continue;

                // 收集面的顶点
                std::vector<carve::mesh::Vertex<3>*> faceVertices;
                carve::mesh::Edge<3>* edge = face->edge;
                do
                {
                    faceVertices.push_back(edge->vert);
                    edge = edge->next;
                } while (edge != face->edge);

                // 三角化面
                for (size_t i = 1; i < faceVertices.size() - 1; ++i)
                {
                    auto v0 = faceVertices[0];
                    auto v1 = faceVertices[i];
                    auto v2 = faceVertices[i + 1];

                    // 变换顶点到全局坐标
                    carve::geom::vector<3> p0 = localTransform * v0->v;
                    carve::geom::vector<3> p1 = localTransform * v1->v;
                    carve::geom::vector<3> p2 = localTransform * v2->v;

                    // 添加顶点
                    uint32_t baseIndex = static_cast<uint32_t>(geomData.vertices.size() / 3);

                    geomData.vertices.insert(geomData.vertices.end(), {
                        static_cast<float>(p0.x), static_cast<float>(p0.y), static_cast<float>(p0.z),
                        static_cast<float>(p1.x), static_cast<float>(p1.y), static_cast<float>(p1.z),
                        static_cast<float>(p2.x), static_cast<float>(p2.y), static_cast<float>(p2.z)
                    });

                    // 计算法线
                    carve::geom::vector<3> normal = face->plane.N;
                    for (int j = 0; j < 3; ++j)
                    {
                        geomData.normals.insert(geomData.normals.end(), {
                            static_cast<float>(normal.x),
                            static_cast<float>(normal.y),
                            static_cast<float>(normal.z)
                        });
                    }

                    // 添加纹理坐标 (简单UV映射)
                    for (int j = 0; j < 3; ++j)
                    {
                        geomData.texCoords.insert(geomData.texCoords.end(), { 0.0f, 0.0f });
                    }

                    // 添加索引
                    geomData.indices.insert(geomData.indices.end(), {
                        baseIndex, baseIndex + 1, baseIndex + 2
                    });
                }
            }
        }
    }

    /**
     * 应用缩放变换
     */
    void ApplyScale(GeometryData& geomData, float scale)
    {
        if (scale != 1.0f)
        {
            for (size_t i = 0; i < geomData.vertices.size(); ++i)
            {
                geomData.vertices[i] *= scale;
            }
        }
    }

    /**
     * 从几何项目中提取材质信息
     */
    std::string ExtractMaterialFromItem(shared_ptr<ItemShapeData>& item, std::shared_ptr<GltfNode> node)
    {
        // 默认材质ID
        std::string materialId = "default";
        
        try {
            // 从几何项目的外观信息中提取颜色
            if (item && !item->m_vec_item_appearances.empty())
            {
                for (auto& appearance : item->m_vec_item_appearances)
                {
                    if (appearance && appearance->m_color_rgba.size() >= 4)
                    {
                        // 创建新材质
                        MaterialData material;
                        material.name = "Material_" + std::to_string(m_materials.size() + 1);
                        material.baseColorFactor[0] = static_cast<float>(appearance->m_color_rgba[0]);
                        material.baseColorFactor[1] = static_cast<float>(appearance->m_color_rgba[1]);
                        material.baseColorFactor[2] = static_cast<float>(appearance->m_color_rgba[2]);
                        material.baseColorFactor[3] = static_cast<float>(appearance->m_color_rgba[3]);
                        
                        // 设置材质属性
                        material.metallicFactor = 0.0f;
                        material.roughnessFactor = 0.9f;
                        
                        // 添加到材质映射
                        materialId = material.name;
                        m_materials[materialId] = material;
                        
                        LogDebug("Extracted material: " + material.name + 
                                " RGBA(" + std::to_string(material.baseColorFactor[0]) + "," +
                                std::to_string(material.baseColorFactor[1]) + "," +
                                std::to_string(material.baseColorFactor[2]) + "," +
                                std::to_string(material.baseColorFactor[3]) + ")");
                        return materialId;
                    }
                }
            }
            
            // 确保有默认材质
            if (m_materials.find("default") == m_materials.end())
            {
                CreateDefaultMaterial();
            }
        }
        catch (const std::exception& e)
        {
            LogWarning("Failed to extract material: " + std::string(e.what()));
        }
        
        return materialId;
    }

    /**
     * 创建默认材质
     */
    void CreateDefaultMaterial()
    {
        MaterialData defaultMaterial;
        defaultMaterial.name = "Default";
        defaultMaterial.baseColorFactor[0] = 0.8f;
        defaultMaterial.baseColorFactor[1] = 0.8f;
        defaultMaterial.baseColorFactor[2] = 0.8f;
        defaultMaterial.baseColorFactor[3] = 1.0f;
        defaultMaterial.metallicFactor = 0.0f;
        defaultMaterial.roughnessFactor = 0.9f;
        
        m_materials["default"] = defaultMaterial;
        LogDebug("Created default material");
    }

    /**
     * 获取材质在数组中的索引
     */
    int GetMaterialIndex(const std::string& materialId)
    {
        int index = 0;
        for (const auto& materialPair : m_materials)
        {
            if (materialPair.first == materialId)
            {
                return index;
            }
            index++;
        }
        return 0; // 默认返回第一个材质
    }

    /**
     * 写出输出文件
     */
    bool WriteOutputFile()
    {
        if (m_args.outputGlb)
        {
            return WriteGlbFile();
        }
        else
        {
            return WriteGltfFile();
        }
    }

    /**
     * 写出glTF文件
     */
    bool WriteGltfFile()
    {
        LogInfo("Writing glTF file: " + m_args.outputFile);

        try
        {
            std::ofstream file(m_args.outputFile);
            if (!file.is_open())
            {
                LogError("Cannot open output file: " + m_args.outputFile);
                return false;
            }

            // 写出glTF JSON
            WriteGltfJson(file);

            file.close();
            
            // 写入二进制数据文件
            WriteBinaryData();
            
            LogInfo("glTF file written successfully");
            return true;
        }
        catch (const std::exception& e)
        {
            LogError("Error writing glTF file: " + std::string(e.what()));
            return false;
        }
    }

    /**
     * 写出GLB文件
     */
    bool WriteGlbFile()
    {
        LogInfo("Writing GLB file: " + m_args.outputFile);

        try
        {
            std::ofstream file(m_args.outputFile, std::ios::binary);
            if (!file.is_open())
            {
                LogError("Cannot open output file: " + m_args.outputFile);
                return false;
            }

            // 生成JSON字符串
            std::stringstream jsonStream;
            WriteGltfJsonForGlb(jsonStream);
            std::string jsonString = jsonStream.str();

            // 收集二进制数据
            std::vector<uint8_t> binaryData;
            CollectBinaryData(binaryData);

            // 写出GLB格式
            WriteGlbHeader(file, jsonString, binaryData);
            WriteGlbJsonChunk(file, jsonString);
            if (!binaryData.empty())
            {
                WriteGlbBinaryChunk(file, binaryData);
            }

            file.close();
            LogInfo("GLB file written successfully (Size: " + std::to_string(binaryData.size()) + " bytes binary data)");
            return true;
        }
        catch (const std::exception& e)
        {
            LogError("Error writing GLB file: " + std::string(e.what()));
            return false;
        }
    }

    /**
     * 写出glTF JSON格式
     */
    void WriteGltfJson(std::ofstream& file)
    {
        file << "{\n";
        file << "  \"asset\": {\n";
        file << "    \"version\": \"2.0\",\n";
        file << "    \"generator\": \"Simple IFC2GLTF Converter v1.0 (Fixed)\"\n";
        file << "  },\n";

        // 场景
        file << "  \"scene\": 0,\n";
        file << "  \"scenes\": [\n";
        file << "    {\n";
        file << "      \"name\": \"" << m_rootNode->name << "\",\n";
        file << "      \"nodes\": [";
        
        for (size_t i = 0; i < m_rootNode->children.size(); ++i)
        {
            if (i > 0) file << ", ";
            file << i;
        }
        
        file << "]\n";
        file << "    }\n";
        file << "  ],\n";

        // 节点
        WriteNodes(file);

        // 网格
        WriteMeshes(file);

        // 访问器
        WriteAccessors(file);

        // 缓冲区视图
        WriteBufferViews(file);

        // 缓冲区
        WriteBuffers(file);

        // 材质
        WriteMaterials(file);

        file << "}\n";
    }

    /**
     * 为GLB写出JSON（不包含外部文件引用）
     */
    void WriteGltfJsonForGlb(std::stringstream& stream)
    {
        stream << "{\n";
        stream << "  \"asset\": {\n";
        stream << "    \"version\": \"2.0\",\n";
        stream << "    \"generator\": \"Simple IFC2GLTF Converter v1.0 (GLB)\"\n";
        stream << "  },\n";

        // 场景
        stream << "  \"scene\": 0,\n";
        stream << "  \"scenes\": [\n";
        stream << "    {\n";
        stream << "      \"name\": \"" << m_rootNode->name << "\",\n";
        stream << "      \"nodes\": [";
        
        for (size_t i = 0; i < m_rootNode->children.size(); ++i)
        {
            if (i > 0) stream << ", ";
            stream << i;
        }
        
        stream << "]\n";
        stream << "    }\n";
        stream << "  ],\n";

        // 节点
        WriteNodesForGlb(stream);

        // 网格
        WriteMeshesForGlb(stream);

        // 访问器
        WriteAccessorsForGlb(stream);

        // 缓冲区视图
        WriteBufferViewsForGlb(stream);

        // 缓冲区（GLB中引用内部二进制数据）
        WriteBuffersForGlb(stream);

        // 材质
        WriteMaterialsForGlb(stream);

        stream << "}";
    }

    /**
     * 写出节点
     */
    void WriteNodes(std::ofstream& file)
    {
        file << "  \"nodes\": [\n";
        
        for (size_t i = 0; i < m_rootNode->children.size(); ++i)
        {
            auto node = m_rootNode->children[i];
            if (i > 0) file << ",\n";
            
            file << "    {\n";
            file << "      \"name\": \"" << node->name << "\"";
            
            if (!node->geometries.empty())
            {
                file << ",\n      \"mesh\": " << i;
            }
            
            file << "\n    }";
        }
        
        file << "\n  ],\n";
    }

    /**
     * 为GLB写出节点
     */
    void WriteNodesForGlb(std::stringstream& stream)
    {
        stream << "  \"nodes\": [\n";
        
        for (size_t i = 0; i < m_rootNode->children.size(); ++i)
        {
            auto node = m_rootNode->children[i];
            if (i > 0) stream << ",\n";
            
            stream << "    {\n";
            stream << "      \"name\": \"" << node->name << "\"";
            
            if (!node->geometries.empty())
            {
                stream << ",\n      \"mesh\": " << i;
            }
            
            stream << "\n    }";
        }
        
        stream << "\n  ],\n";
    }

    /**
     * 写出网格
     */
    void WriteMeshes(std::ofstream& file)
    {
        file << "  \"meshes\": [\n";
        
        bool first = true;
        size_t accessorIndex = 0;
        
        for (auto node : m_rootNode->children)
        {
            if (node->geometries.empty()) continue;
            
            if (!first) file << ",\n";
            first = false;
            
            file << "    {\n";
            file << "      \"name\": \"" << node->name << "_Mesh\",\n";
            file << "      \"primitives\": [\n";
            
            for (size_t i = 0; i < node->geometries.size(); ++i)
            {
                if (i > 0) file << ",\n";
                
                file << "        {\n";
                file << "          \"attributes\": {\n";
                file << "            \"POSITION\": " << (accessorIndex * 3) << ",\n";
                file << "            \"NORMAL\": " << (accessorIndex * 3 + 1) << "\n";
                file << "          },\n";
                file << "          \"indices\": " << (accessorIndex * 3 + 2) << ",\n";
                file << "          \"material\": 0\n";
                file << "        }";
                
                accessorIndex++;
            }
            
            file << "\n      ]\n";
            file << "    }";
        }
        
        file << "\n  ],\n";
    }

    /**
     * 为GLB写出网格
     */
    void WriteMeshesForGlb(std::stringstream& stream)
    {
        stream << "  \"meshes\": [\n";
        
        bool first = true;
        size_t accessorIndex = 0;
        
        for (auto node : m_rootNode->children)
        {
            if (node->geometries.empty()) continue;
            
            if (!first) stream << ",\n";
            first = false;
            
            stream << "    {\n";
            stream << "      \"name\": \"" << node->name << "_Mesh\",\n";
            stream << "      \"primitives\": [\n";
            
            for (size_t i = 0; i < node->geometries.size(); ++i)
            {
                if (i > 0) stream << ",\n";
                
                stream << "        {\n";
                stream << "          \"attributes\": {\n";
                stream << "            \"POSITION\": " << (accessorIndex * 3) << ",\n";
                stream << "            \"NORMAL\": " << (accessorIndex * 3 + 1) << "\n";
                stream << "          },\n";
                stream << "          \"indices\": " << (accessorIndex * 3 + 2) << ",\n";
                stream << "          \"material\": 0\n";
                stream << "        }";
                
                accessorIndex++;
            }
            
            stream << "\n      ]\n";
            stream << "    }";
        }
        
        stream << "\n  ],\n";
    }

    /**
     * 写出访问器
     */
    void WriteAccessors(std::ofstream& file)
    {
        file << "  \"accessors\": [\n";
        
        bool first = true;
        size_t bufferViewIndex = 0;
        
        for (auto node : m_rootNode->children)
        {
            for (auto& geom : node->geometries)
            {
                size_t vertexCount = geom.vertices.size() / 3;
                
                if (!first) file << ",\n";
                first = false;
                
                // 位置访问器
                file << "    {\n";
                file << "      \"bufferView\": " << bufferViewIndex << ",\n";
                file << "      \"componentType\": 5126,\n";
                file << "      \"count\": " << vertexCount << ",\n";
                file << "      \"type\": \"VEC3\",\n";
                file << "      \"min\": [" << geom.minBounds[0] << ", " << geom.minBounds[1] << ", " << geom.minBounds[2] << "],\n";
                file << "      \"max\": [" << geom.maxBounds[0] << ", " << geom.maxBounds[1] << ", " << geom.maxBounds[2] << "]\n";
                file << "    },\n";
                
                // 法线访问器
                file << "    {\n";
                file << "      \"bufferView\": " << (bufferViewIndex + 1) << ",\n";
                file << "      \"componentType\": 5126,\n";
                file << "      \"count\": " << vertexCount << ",\n";
                file << "      \"type\": \"VEC3\"\n";
                file << "    },\n";
                
                // 索引访问器
                file << "    {\n";
                file << "      \"bufferView\": " << (bufferViewIndex + 2) << ",\n";
                file << "      \"componentType\": 5125,\n";
                file << "      \"count\": " << geom.indices.size() << ",\n";
                file << "      \"type\": \"SCALAR\"\n";
                file << "    }";
                
                bufferViewIndex += 3;
            }
        }
        
        file << "\n  ],\n";
    }

    /**
     * 为GLB写出访问器
     */
    void WriteAccessorsForGlb(std::stringstream& stream)
    {
        stream << "  \"accessors\": [\n";
        
        bool first = true;
        size_t bufferViewIndex = 0;
        
        for (auto node : m_rootNode->children)
        {
            for (auto& geom : node->geometries)
            {
                size_t vertexCount = geom.vertices.size() / 3;
                
                if (!first) stream << ",\n";
                first = false;
                
                // 位置访问器
                stream << "    {\n";
                stream << "      \"bufferView\": " << bufferViewIndex << ",\n";
                stream << "      \"componentType\": 5126,\n";
                stream << "      \"count\": " << vertexCount << ",\n";
                stream << "      \"type\": \"VEC3\",\n";
                stream << "      \"min\": [" << geom.minBounds[0] << ", " << geom.minBounds[1] << ", " << geom.minBounds[2] << "],\n";
                stream << "      \"max\": [" << geom.maxBounds[0] << ", " << geom.maxBounds[1] << ", " << geom.maxBounds[2] << "]\n";
                stream << "    },\n";
                
                // 法线访问器
                stream << "    {\n";
                stream << "      \"bufferView\": " << (bufferViewIndex + 1) << ",\n";
                stream << "      \"componentType\": 5126,\n";
                stream << "      \"count\": " << vertexCount << ",\n";
                stream << "      \"type\": \"VEC3\"\n";
                stream << "    },\n";
                
                // 索引访问器
                stream << "    {\n";
                stream << "      \"bufferView\": " << (bufferViewIndex + 2) << ",\n";
                stream << "      \"componentType\": 5125,\n";
                stream << "      \"count\": " << geom.indices.size() << ",\n";
                stream << "      \"type\": \"SCALAR\"\n";
                stream << "    }";
                
                bufferViewIndex += 3;
            }
        }
        
        stream << "\n  ],\n";
    }

    /**
     * 写出缓冲区视图
     */
    void WriteBufferViews(std::ofstream& file)
    {
        file << "  \"bufferViews\": [\n";
        
        size_t byteOffset = 0;
        bool first = true;
        
        for (auto node : m_rootNode->children)
        {
            for (auto& geom : node->geometries)
            {
                size_t vertexBytes = geom.vertices.size() * sizeof(float);
                size_t normalBytes = geom.normals.size() * sizeof(float);
                size_t indexBytes = geom.indices.size() * sizeof(uint32_t);
                
                if (!first) file << ",\n";
                first = false;
                
                // 顶点缓冲区视图
                file << "    {\n";
                file << "      \"buffer\": 0,\n";
                file << "      \"byteOffset\": " << byteOffset << ",\n";
                file << "      \"byteLength\": " << vertexBytes << ",\n";
                file << "      \"target\": 34962\n";
                file << "    },\n";
                byteOffset += vertexBytes;
                
                // 法线缓冲区视图
                file << "    {\n";
                file << "      \"buffer\": 0,\n";
                file << "      \"byteOffset\": " << byteOffset << ",\n";
                file << "      \"byteLength\": " << normalBytes << ",\n";
                file << "      \"target\": 34962\n";
                file << "    },\n";
                byteOffset += normalBytes;
                
                // 索引缓冲区视图
                file << "    {\n";
                file << "      \"buffer\": 0,\n";
                file << "      \"byteOffset\": " << byteOffset << ",\n";
                file << "      \"byteLength\": " << indexBytes << ",\n";
                file << "      \"target\": 34963\n";
                file << "    }";
                byteOffset += indexBytes;
            }
        }
        
        file << "\n  ],\n";
    }

    /**
     * 为GLB写出缓冲区视图
     */
    void WriteBufferViewsForGlb(std::stringstream& stream)
    {
        stream << "  \"bufferViews\": [\n";
        
        size_t byteOffset = 0;
        bool first = true;
        
        for (auto node : m_rootNode->children)
        {
            for (auto& geom : node->geometries)
            {
                size_t vertexBytes = geom.vertices.size() * sizeof(float);
                size_t normalBytes = geom.normals.size() * sizeof(float);
                size_t indexBytes = geom.indices.size() * sizeof(uint32_t);
                
                if (!first) stream << ",\n";
                first = false;
                
                // 顶点缓冲区视图
                stream << "    {\n";
                stream << "      \"buffer\": 0,\n";
                stream << "      \"byteOffset\": " << byteOffset << ",\n";
                stream << "      \"byteLength\": " << vertexBytes << ",\n";
                stream << "      \"target\": 34962\n";
                stream << "    },\n";
                byteOffset += vertexBytes;
                
                // 法线缓冲区视图
                stream << "    {\n";
                stream << "      \"buffer\": 0,\n";
                stream << "      \"byteOffset\": " << byteOffset << ",\n";
                stream << "      \"byteLength\": " << normalBytes << ",\n";
                stream << "      \"target\": 34962\n";
                stream << "    },\n";
                byteOffset += normalBytes;
                
                // 索引缓冲区视图
                stream << "    {\n";
                stream << "      \"buffer\": 0,\n";
                stream << "      \"byteOffset\": " << byteOffset << ",\n";
                stream << "      \"byteLength\": " << indexBytes << ",\n";
                stream << "      \"target\": 34963\n";
                stream << "    }";
                byteOffset += indexBytes;
            }
        }
        
        stream << "\n  ],\n";
    }

    /**
     * 写出缓冲区
     */
    void WriteBuffers(std::ofstream& file)
    {
        // 计算总字节数
        size_t totalBytes = 0;
        for (auto node : m_rootNode->children)
        {
            for (auto& geom : node->geometries)
            {
                totalBytes += geom.vertices.size() * sizeof(float);
                totalBytes += geom.normals.size() * sizeof(float);
                totalBytes += geom.indices.size() * sizeof(uint32_t);
            }
        }
        
        std::string binFileName = m_args.outputFile.substr(0, m_args.outputFile.find_last_of('.')) + ".bin";
        std::string binFileNameOnly = binFileName.substr(binFileName.find_last_of('/') + 1);
        if (binFileNameOnly == binFileName)
        {
            binFileNameOnly = binFileName.substr(binFileName.find_last_of('\\') + 1);
        }
        
        file << "  \"buffers\": [\n";
        file << "    {\n";
        file << "      \"uri\": \"" << binFileNameOnly << "\",\n";
        file << "      \"byteLength\": " << totalBytes << "\n";
        file << "    }\n";
        file << "  ],\n";
    }

    /**
     * 为GLB写出缓冲区（引用内部二进制数据）
     */
    void WriteBuffersForGlb(std::stringstream& stream)
    {
        // 计算总字节数
        size_t totalBytes = 0;
        for (auto node : m_rootNode->children)
        {
            for (auto& geom : node->geometries)
            {
                totalBytes += geom.vertices.size() * sizeof(float);
                totalBytes += geom.normals.size() * sizeof(float);
                totalBytes += geom.indices.size() * sizeof(uint32_t);
            }
        }
        
        stream << "  \"buffers\": [\n";
        stream << "    {\n";
        stream << "      \"byteLength\": " << totalBytes << "\n";
        stream << "    }\n";
        stream << "  ],\n";
    }

    /**
     * 写出材质
     */
    void WriteMaterials(std::ofstream& file)
    {
        file << "  \"materials\": [\n";
        file << "    {\n";
        file << "      \"name\": \"Default\",\n";
        file << "      \"pbrMetallicRoughness\": {\n";
        file << "        \"baseColorFactor\": [0.8, 0.8, 0.8, 1.0],\n";
        file << "        \"metallicFactor\": 0.0,\n";
        file << "        \"roughnessFactor\": 0.9\n";
        file << "      }\n";
        file << "    }\n";
        file << "  ]\n";
    }

    /**
     * 为GLB写出材质
     */
    void WriteMaterialsForGlb(std::stringstream& stream)
    {
        stream << "  \"materials\": [\n";
        stream << "    {\n";
        stream << "      \"name\": \"Default\",\n";
        stream << "      \"pbrMetallicRoughness\": {\n";
        stream << "        \"baseColorFactor\": [0.8, 0.8, 0.8, 1.0],\n";
        stream << "        \"metallicFactor\": 0.0,\n";
        stream << "        \"roughnessFactor\": 0.9\n";
        stream << "      }\n";
        stream << "    }\n";
        stream << "  ]\n";
    }

    /**
     * 写入二进制数据文件
     */
    void WriteBinaryData()
    {
        std::string binFile = m_args.outputFile.substr(0, m_args.outputFile.find_last_of('.')) + ".bin";
        std::ofstream binOut(binFile, std::ios::binary);
        
        if (!binOut.is_open())
        {
            LogWarning("Cannot create binary file: " + binFile);
            return;
        }

        // 写入所有几何数据
        for (auto node : m_rootNode->children)
        {
            for (auto& geom : node->geometries)
            {
                // 写入顶点数据
                binOut.write(reinterpret_cast<const char*>(geom.vertices.data()), 
                            geom.vertices.size() * sizeof(float));
                
                // 写入法线数据
                binOut.write(reinterpret_cast<const char*>(geom.normals.data()), 
                            geom.normals.size() * sizeof(float));
                
                // 写入索引数据
                binOut.write(reinterpret_cast<const char*>(geom.indices.data()), 
                            geom.indices.size() * sizeof(uint32_t));
            }
        }

        binOut.close();
        LogInfo("Binary data written to: " + binFile);
    }

    /**
     * 收集二进制数据
     */
    void CollectBinaryData(std::vector<uint8_t>& binaryData)
    {
        for (auto node : m_rootNode->children)
        {
            for (auto& geom : node->geometries)
            {
                // 添加顶点数据
                const uint8_t* vertexBytes = reinterpret_cast<const uint8_t*>(geom.vertices.data());
                size_t vertexSize = geom.vertices.size() * sizeof(float);
                binaryData.insert(binaryData.end(), vertexBytes, vertexBytes + vertexSize);
                
                // 添加法线数据
                const uint8_t* normalBytes = reinterpret_cast<const uint8_t*>(geom.normals.data());
                size_t normalSize = geom.normals.size() * sizeof(float);
                binaryData.insert(binaryData.end(), normalBytes, normalBytes + normalSize);
                
                // 添加索引数据
                const uint8_t* indexBytes = reinterpret_cast<const uint8_t*>(geom.indices.data());
                size_t indexSize = geom.indices.size() * sizeof(uint32_t);
                binaryData.insert(binaryData.end(), indexBytes, indexBytes + indexSize);
            }
        }
    }

    /**
     * 写出GLB头部
     */
    void WriteGlbHeader(std::ofstream& file, const std::string& jsonString, const std::vector<uint8_t>& binaryData)
    {
        uint32_t magic = 0x46546C67; // "glTF"
        uint32_t version = 2;
        uint32_t jsonLength = static_cast<uint32_t>((jsonString.length() + 3) & ~3); // 4字节对齐
        uint32_t binaryLength = static_cast<uint32_t>((binaryData.size() + 3) & ~3); // 4字节对齐
        uint32_t totalLength = 12 + 8 + jsonLength + (binaryData.empty() ? 0 : 8 + binaryLength);
        
        file.write(reinterpret_cast<const char*>(&magic), 4);
        file.write(reinterpret_cast<const char*>(&version), 4);
        file.write(reinterpret_cast<const char*>(&totalLength), 4);
    }

    /**
     * 写出GLB JSON块
     */
    void WriteGlbJsonChunk(std::ofstream& file, const std::string& jsonString)
    {
        uint32_t jsonLength = static_cast<uint32_t>((jsonString.length() + 3) & ~3);
        uint32_t jsonType = 0x4E4F534A; // "JSON"
        
        file.write(reinterpret_cast<const char*>(&jsonLength), 4);
        file.write(reinterpret_cast<const char*>(&jsonType), 4);
        file.write(jsonString.c_str(), jsonString.length());
        
        // 填充到4字节对齐
        for (size_t i = jsonString.length(); i < jsonLength; ++i)
        {
            file.put(' ');
        }
    }

    /**
     * 写出GLB二进制块
     */
    void WriteGlbBinaryChunk(std::ofstream& file, const std::vector<uint8_t>& binaryData)
    {
        uint32_t binaryLength = static_cast<uint32_t>((binaryData.size() + 3) & ~3);
        uint32_t binaryType = 0x004E4942; // "BIN\0"
        
        file.write(reinterpret_cast<const char*>(&binaryLength), 4);
        file.write(reinterpret_cast<const char*>(&binaryType), 4);
        file.write(reinterpret_cast<const char*>(binaryData.data()), binaryData.size());
        
        // 填充到4字节对齐
        for (size_t i = binaryData.size(); i < binaryLength; ++i)
        {
            file.put('\0');
        }
    }

    /**
     * 获取实体名称
     */
    std::string GetEntityName(shared_ptr<IfcObjectDefinition> entity)
    {
        if (!entity) return "Unknown";

        auto root = std::dynamic_pointer_cast<IfcRoot>(entity);
        if (!root) return "Unnamed";

        std::string name = "Unnamed";
        if (root->m_Name && !root->m_Name->m_value.empty())
        {
            name = root->m_Name->m_value;
        }
        else if (root->m_GlobalId && !root->m_GlobalId->m_value.empty())
        {
            name = root->m_GlobalId->m_value;
        }

        return name;
    }

    /**
     * 获取实体类型
     */
    std::string GetEntityType(shared_ptr<IfcObjectDefinition> entity)
    {
        if (!entity) return "Unknown";

        // 简化的类型名提取
        std::string typeName = typeid(*entity).name();
        size_t pos = typeName.find("Ifc");
        if (pos != std::string::npos)
        {
            return typeName.substr(pos);
        }

        return "IfcProduct";
    }

    /**
     * 日志输出方法
     */
    void LogInfo(const std::string& message)
    {
        if (m_args.logLevel >= 1)
        {
            std::cout << "[INFO] " << message << std::endl;
        }
    }

    void LogWarning(const std::string& message)
    {
        if (m_args.logLevel >= 1)
        {
            std::cout << "[WARNING] " << message << std::endl;
        }
    }

    void LogError(const std::string& message)
    {
        std::cerr << "[ERROR] " << message << std::endl;
    }

    void LogDebug(const std::string& message)
    {
        if (m_args.logLevel >= 2)
        {
            std::cout << "[DEBUG] " << message << std::endl;
        }
    }
};

/**
 * 解析命令行参数
 */
CommandLineArgs ParseCommandLine(int argc, char* argv[])
{
    CommandLineArgs args;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "-i" || arg == "--input")
        {
            if (i + 1 < argc)
            {
                args.inputFile = argv[++i];
            }
        }
        else if (arg == "-o" || arg == "--output")
        {
            if (i + 1 < argc)
            {
                args.outputFile = argv[++i];
            }
        }
        else if (arg == "-s" || arg == "--scale")
        {
            if (i + 1 < argc)
            {
                args.scale = std::stof(argv[++i]);
            }
        }
        else if (arg == "-l" || arg == "--log")
        {
            if (i + 1 < argc)
            {
                args.logLevel = std::stoi(argv[++i]);
            }
        }
        else if (arg == "--glb")
        {
            args.outputGlb = true;
        }
        else if (arg == "-h" || arg == "--help")
        {
            std::cout << "Usage: simple_ifc2gltf [OPTIONS]\n";
            std::cout << "Options:\n";
            std::cout << "  -i, --input FILE    Input IFC file path\n";
            std::cout << "  -o, --output FILE   Output glTF file path\n";
            std::cout << "  -s, --scale FLOAT   Scale factor (default: 1.0)\n";
            std::cout << "  -l, --log LEVEL     Log level (0=silent, 1=normal, 2=verbose)\n";
            std::cout << "  --glb               Output GLB format\n";
            std::cout << "  -h, --help          Show this help message\n";
            exit(0);
        }
    }

    return args;
}

/**
 * 验证命令行参数
 */
bool ValidateArgs(const CommandLineArgs& args)
{
    if (args.inputFile.empty())
    {
        std::cerr << "Error: Input file not specified. Use -i or --input option." << std::endl;
        return false;
    }

    if (args.outputFile.empty())
    {
        std::cerr << "Error: Output file not specified. Use -o or --output option." << std::endl;
        return false;
    }

    if (args.scale <= 0.0f)
    {
        std::cerr << "Error: Scale factor must be positive." << std::endl;
        return false;
    }

    if (args.logLevel < 0 || args.logLevel > 2)
    {
        std::cerr << "Error: Log level must be 0, 1, or 2." << std::endl;
        return false;
    }

    return true;
}

/**
 * 主函数入口
 */
int main(int argc, char* argv[])
{
    try
    {
        std::cout << "Simple IFC to glTF/GLB Converter v1.0" << std::endl;
        std::cout << "=====================================" << std::endl;

        // 解析命令行参数
        CommandLineArgs args = ParseCommandLine(argc, argv);

        // 如果没有提供参数，使用默认测试参数
        if (args.inputFile.empty())
        {
            args.inputFile = "example.ifc";
            args.outputFile = "output_fixed.gltf";
            args.scale = 1.0f;
            args.logLevel = 2; // 使用详细日志
            
            std::cout << "No arguments provided, using defaults:" << std::endl;
        }

        // 验证参数
        if (!ValidateArgs(args))
        {
            return 1;
        }

        // 显示转换参数
        std::cout << "Input file: " << args.inputFile << std::endl;
        std::cout << "Output file: " << args.outputFile << std::endl;
        std::cout << "Scale factor: " << args.scale << std::endl;
        std::cout << "Output format: " << (args.outputGlb ? "GLB" : "glTF") << std::endl;
        std::cout << "Log level: " << args.logLevel << std::endl;
        std::cout << std::endl;

        // 创建转换器并执行转换
        SimpleIFC2GLTF converter(args);
        bool success = converter.Convert();

        if (success)
        {
            std::cout << std::endl << "Conversion completed successfully!" << std::endl;
            return 0;
        }
        else
        {
            std::cerr << std::endl << "Conversion failed!" << std::endl;
            return 1;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "Unknown fatal error occurred!" << std::endl;
        return 1;
    }
}