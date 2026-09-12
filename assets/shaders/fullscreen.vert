/**
 * @file
 * @brief Generate a fullscreen triangle without a vertex buffer.
 * Draw three vertices with a bound VAO; oversized edges cover the viewport without a
 * diagonal seam. Interpolated UVs cover [0,1] with a lower-left origin.
 */
#version 430 core
out vec2 uv;

/**
 * @brief Derive triangle positions and texture coordinates directly from gl_VertexID.
 */
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0, 1);
}
