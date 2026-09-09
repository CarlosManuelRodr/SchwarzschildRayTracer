#version 430 core
in vec2 uv;
out vec4 color;
uniform sampler2D accumulation;
uniform float exposure;

void main()
{
    vec4 sum = texture(accumulation, uv);
    vec3 hdr = max(sum.rgb / max(sum.a, 1.0), vec3(0)) * exposure;
    ivec2 size = textureSize(accumulation, 0);
    ivec2 pixel = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
    float weights[5] = float[5](1, 4, 6, 4, 1);
    vec3 bloom = vec3(0);

    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            ivec2 samplePixel = clamp(pixel + 4 * ivec2(x, y), ivec2(0), size - 1);
            vec4 sampleSum = texelFetch(accumulation, samplePixel, 0);
            vec3 sampleHdr = exposure * sampleSum.rgb / max(sampleSum.a, 1.0);
            float luminance = dot(sampleHdr, vec3(0.2126, 0.7152, 0.0722));
            float excess = max(0.0, luminance - 1.0) / max(luminance, 1e-8);
            bloom += sampleHdr * (excess * weights[x + 2] * weights[y + 2] / 256.0);
        }
    }

    hdr += 0.08 * bloom;
    vec3 linear = clamp((hdr * (2.51 * hdr + 0.03)) / (hdr * (2.43 * hdr + 0.59) + 0.14), 0.0, 1.0);
    vec3 srgb = mix(
        1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055, 12.92 * linear, lessThanEqual(linear, vec3(0.0031308)));
    color = vec4(srgb, 1);
}
