#version 450 core
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 texCoord;
layout(location = 1) in flat uint textureIndex;
layout(location = 2) in vec4 tintColor;

layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D texSamplers[];

void main()
{
    vec4 texColor = texture(texSamplers[nonuniformEXT(textureIndex)], texCoord);
    fragColor = vec4(pow(tintColor.rgb, vec3(2.2)) * texColor.rgb, tintColor.a * texColor.a);
}
