#version 430 core
in vec2 uv;
out vec4 color;
uniform sampler2D accumulation;
void main() {
    vec4 sum=texture(accumulation,uv);
    vec3 linear=clamp(sum.rgb/max(sum.a,1.0),0.0,1.0);
    vec3 srgb=mix(1.055*pow(linear,vec3(1.0/2.4))-0.055,12.92*linear,lessThanEqual(linear,vec3(0.0031308)));
    color=vec4(srgb,1);
}
