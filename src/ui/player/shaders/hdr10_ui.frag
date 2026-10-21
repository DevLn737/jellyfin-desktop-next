#version 440
// Encodes the window's sRGB interface for an HDR10 swap chain: sRGB to linear
// BT.709, to BT.2020, scaled so that sRGB white is the SDR white level, then
// PQ. The layer texture holds premultiplied alpha.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float sdrWhiteNits;
};

layout(binding = 1) uniform sampler2D source;

vec3 srgbToLinear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 pqFromNits(vec3 nits)
{
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * y) / (1.0 + c3 * y), vec3(m2));
}

void main()
{
    vec4 c = texture(source, qt_TexCoord0);
    if (c.a <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    // Columns: the BT.2020 contribution of linear BT.709 red, green and blue.
    const mat3 bt709ToBt2020 = mat3(0.6274, 0.0691, 0.0164,
                                    0.3293, 0.9195, 0.0880,
                                    0.0433, 0.0114, 0.8956);
    vec3 linear = bt709ToBt2020 * srgbToLinear(c.rgb / c.a);
    fragColor = vec4(pqFromNits(linear * sdrWhiteNits) * c.a, c.a) * qt_Opacity;
}
