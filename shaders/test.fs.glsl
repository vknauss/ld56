#version 450 core
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 texCoord;
layout(location = 1) in flat uint textureIndex;
layout(location = 2) in vec3 tintColor;

layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D texSamplers[];

void main()
{
    fragColor = vec4((textureIndex + 1) & 1, ((textureIndex + 1) >> 1) & 1, ((textureIndex + 1) >> 2) & 1, 1);
    fragColor = vec4(tintColor * texture(texSamplers[nonuniformEXT(textureIndex)], texCoord).rgb, 1);
}
