#include "BunnyTest.h"
#include "BunnyData.h"

namespace cc {

namespace {
enum class Binding : uint8_t { MVP, COLOR };
}

void BunnyTest::destroy() {
    CC_SAFE_DESTROY(_shader);
    CC_SAFE_DESTROY(_vertexBuffer);
    CC_SAFE_DESTROY(_indexBuffer);
    CC_SAFE_DESTROY(_mvpMatrix);
    CC_SAFE_DESTROY(_color);
    CC_SAFE_DESTROY(_rootUBO);
    CC_SAFE_DESTROY(_inputAssembler);
    CC_SAFE_DESTROY(_descriptorSet);
    CC_SAFE_DESTROY(_descriptorSetLayout);
    CC_SAFE_DESTROY(_pipelineLayout);
    CC_SAFE_DESTROY(_pipelineState);
}

bool BunnyTest::initialize() {
    createShader();
    createBuffers();
    createInputAssembler();
    createPipelineState();
    return true;
}

void BunnyTest::createShader() {

    ShaderSources sources;
    sources.glsl4 = {
        R"(
            layout(location = 0) in vec3 a_position;

            layout(set = 0, binding = 0) uniform MVP_Matrix {
                mat4 u_model, u_view, u_projection;
            };

            layout(location = 0) out vec3 v_position;

            void main () {
                vec4 pos = u_projection * u_view * u_model * vec4(a_position, 1);
                v_position = a_position.xyz;
                gl_Position = pos;
            }
        )",
        R"(
            layout(set = 0, binding = 1) uniform Color {
                vec4 u_color;
            };
            layout(location = 0) in vec3 v_position;
            layout(location = 0) out vec4 o_color;
            void main () {
                o_color = u_color * vec4(v_position, 1);
            }
        )",
    };

    sources.glsl3 = {
        R"(
            in vec3 a_position;

            layout(std140) uniform MVP_Matrix {
                mat4 u_model, u_view, u_projection;
            };

            out vec3 v_position;

            void main () {
                vec4 pos = u_projection * u_view * u_model * vec4(a_position, 1);
                v_position = a_position.xyz;
                gl_Position = pos;
            }
        )",
        R"(
            precision mediump float;
            layout(std140) uniform Color {
                vec4 u_color;
            };
            in vec3 v_position;
            out vec4 o_color;
            void main () {
                o_color = u_color * vec4(v_position, 1);
            }
        )",
    };

    sources.glsl1 = {
        R"(
            attribute vec3 a_position;
            uniform mat4 u_model, u_view, u_projection;
            varying vec3 v_position;

            void main () {
                vec4 pos = u_projection * u_view * u_model * vec4(a_position, 1);
                v_position = a_position.xyz;
                gl_Position = pos;
            }
        )",
        R"(
            precision mediump float;
            uniform vec4 u_color;
            varying vec3 v_position;

            void main () {
                gl_FragColor = u_color * vec4(v_position, 1);
            }
        )",
    };

    ShaderSource &source = TestBaseI::getAppropriateShaderSource(sources);

    gfx::ShaderStageList shaderStageList;
    gfx::ShaderStage vertexShaderStage;
    vertexShaderStage.stage = gfx::ShaderStageFlagBit::VERTEX;
    vertexShaderStage.source = source.vert;
    shaderStageList.emplace_back(std::move(vertexShaderStage));

    // fragment shader
    gfx::ShaderStage fragmentShaderStage;
    fragmentShaderStage.stage = gfx::ShaderStageFlagBit::FRAGMENT;
    fragmentShaderStage.source = source.frag;
    shaderStageList.emplace_back(std::move(fragmentShaderStage));

    gfx::AttributeList attributeList = {{"a_position", gfx::Format::RGB32F, false, 0, false, 0}};
    gfx::UniformList mvpMatrix = {
        {"u_model", gfx::Type::MAT4, 1},
        {"u_view", gfx::Type::MAT4, 1},
        {"u_projection", gfx::Type::MAT4, 1},
    };
    gfx::UniformList color = {{"u_color", gfx::Type::FLOAT4, 1}};
    gfx::UniformBlockList uniformBlockList = {
        {0, static_cast<uint>(Binding::MVP), "MVP_Matrix", mvpMatrix, 1},
        {0, static_cast<uint>(Binding::COLOR), "Color", color, 1},
    };

    gfx::ShaderInfo shaderInfo;
    shaderInfo.name = "Bunny Test";
    shaderInfo.stages = std::move(shaderStageList);
    shaderInfo.attributes = std::move(attributeList);
    shaderInfo.blocks = std::move(uniformBlockList);
    _shader = _device->createShader(shaderInfo);
}

void BunnyTest::createBuffers() {
    // vertex buffer
    _vertexBuffer = _device->createBuffer({
        gfx::BufferUsage::VERTEX,
        gfx::MemoryUsage::DEVICE,
        sizeof(bunny_positions),
        3 * sizeof(float),
    });
    _vertexBuffer->update((void *)&bunny_positions[0][0], 0, sizeof(bunny_positions));

    // index buffer
    _indexBuffer = _device->createBuffer({
        gfx::BufferUsage::INDEX,
        gfx::MemoryUsage::DEVICE,
        sizeof(bunny_cells),
        sizeof(uint16_t),
    });
    _indexBuffer->update((void *)&bunny_cells[0], 0, sizeof(bunny_cells));

    // root UBO
    uint offset = TestBaseI::getAlignedUBOStride(_device, 3 * sizeof(Mat4));
    uint size = offset + 4 * sizeof(float);
    _rootUBO = _device->createBuffer({
        gfx::BufferUsage::UNIFORM,
        gfx::MemoryUsage::DEVICE | gfx::MemoryUsage::HOST,
        TestBaseI::getUBOSize(size),
    });
    _rootBuffer.resize(size / sizeof(float));

    // mvp matrix uniform
    _mvpMatrix = _device->createBuffer({
        _rootUBO,
        0,
        3 * sizeof(Mat4),
    });
    // color uniform
    _color = _device->createBuffer({
        _rootUBO,
        offset,
        4 * sizeof(float),
    });

    Mat4 model;
    std::copy(model.m, model.m + 16, &_rootBuffer[0]);

    float color[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    std::copy(color, color + 4, &_rootBuffer[offset / sizeof(float)]);
}

void BunnyTest::createInputAssembler() {
    gfx::Attribute position = {"a_position", gfx::Format::RGB32F, false, 0, false};
    gfx::InputAssemblerInfo inputAssemblerInfo;
    inputAssemblerInfo.attributes.emplace_back(std::move(position));
    inputAssemblerInfo.vertexBuffers.emplace_back(_vertexBuffer);
    inputAssemblerInfo.indexBuffer = _indexBuffer;
    _inputAssembler = _device->createInputAssembler(inputAssemblerInfo);
}

void BunnyTest::createPipelineState() {
    gfx::DescriptorSetLayoutInfo dslInfo;
    dslInfo.bindings.push_back({0, gfx::DescriptorType::UNIFORM_BUFFER, 1, gfx::ShaderStageFlagBit::VERTEX});
    dslInfo.bindings.push_back({1, gfx::DescriptorType::UNIFORM_BUFFER, 1, gfx::ShaderStageFlagBit::FRAGMENT});
    _descriptorSetLayout = _device->createDescriptorSetLayout(dslInfo);

    _pipelineLayout = _device->createPipelineLayout({{_descriptorSetLayout}});

    _descriptorSet = _device->createDescriptorSet({_descriptorSetLayout});

    _descriptorSet->bindBuffer(static_cast<uint>(Binding::MVP), _mvpMatrix);
    _descriptorSet->bindBuffer(static_cast<uint>(Binding::COLOR), _color);
    _descriptorSet->update();

    gfx::PipelineStateInfo pipelineStateInfo;
    pipelineStateInfo.primitive = gfx::PrimitiveMode::TRIANGLE_LIST;
    pipelineStateInfo.shader = _shader;
    pipelineStateInfo.inputState = {_inputAssembler->getAttributes()};
    pipelineStateInfo.renderPass = _fbo->getRenderPass();
    pipelineStateInfo.depthStencilState.depthTest = true;
    pipelineStateInfo.depthStencilState.depthWrite = true;
    pipelineStateInfo.depthStencilState.depthFunc = gfx::ComparisonFunc::LESS;
    pipelineStateInfo.pipelineLayout = _pipelineLayout;
    _pipelineState = _device->createPipelineState(pipelineStateInfo);
}

void BunnyTest::tick() {
    lookupTime();
    _dt += hostThread.dt;

    Mat4::createLookAt(Vec3(30.0f * std::cos(_dt), 20.0f, 30.0f * std::sin(_dt)),
                       Vec3(0.0f, 2.5f, 0.0f), Vec3(0.0f, 1.0f, 0.f), &_view);
    std::copy(_view.m, _view.m + 16, &_rootBuffer[16]);

    Mat4 projection;
    gfx::Extent orientedSize = TestBaseI::getOrientedSurfaceSize();
    TestBaseI::createPerspective(60.0f, 1.0f * orientedSize.width / orientedSize.height, 0.01f, 1000.0f, &projection);
    std::copy(projection.m, projection.m + 16, &_rootBuffer[32]);

    gfx::Color clearColor = {0.0f, 0, 0, 1.0f};

    _device->acquire();

    _rootUBO->update(_rootBuffer.data(), 0, _rootBuffer.size() * sizeof(float));
    gfx::Rect renderArea = {0, 0, _device->getWidth(), _device->getHeight()};

    auto commandBuffer = _commandBuffers[0];
    commandBuffer->begin();
    commandBuffer->beginRenderPass(_fbo->getRenderPass(), _fbo, renderArea, &clearColor, 1.0f, 0);

    commandBuffer->bindInputAssembler(_inputAssembler);
    commandBuffer->bindPipelineState(_pipelineState);
    commandBuffer->bindDescriptorSet(0, _descriptorSet);
    commandBuffer->draw(_inputAssembler);

    commandBuffer->endRenderPass();
    commandBuffer->end();

    _device->getQueue()->submit(_commandBuffers);
    _device->present();
}

} // namespace cc
