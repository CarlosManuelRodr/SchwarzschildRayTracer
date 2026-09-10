#include <GL/glew.h>
#include "GpuRenderer.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <cmath>

namespace rt
{
namespace
{
std::string readText(const std::filesystem::path& path)
{
    std::ifstream f(path);

    if (!f)
        throw std::runtime_error("Cannot read shader: " + path.string());
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

GLuint compile(GLenum kind, const std::string& source)
{
    GLuint shader = glCreateShader(kind);
    const char* data = source.c_str();
    glShaderSource(shader, 1, &data, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (!ok)
    {
        GLint n = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &n);
        std::string log(std::max(n, 1), 0);
        glGetShaderInfoLog(shader, n, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("Shader compilation failed:\n" + log);
    }

    return shader;
}

GLuint program(const std::vector<std::pair<GLenum, std::string>>& sources)
{
    GLuint p = glCreateProgram();
    std::vector<GLuint> shaders;

    try
    {
        for (auto& source : sources)
        {
            auto shader = compile(source.first, source.second);
            shaders.push_back(shader);
            glAttachShader(p, shader);
        }

        glLinkProgram(p);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);

        if (!ok)
        {
            GLint n = 0;
            glGetProgramiv(p, GL_INFO_LOG_LENGTH, &n);
            std::string log(std::max(n, 1), 0);
            glGetProgramInfoLog(p, n, nullptr, log.data());
            throw std::runtime_error("Shader link failed:\n" + log);
        }
    }
    catch (...)
    {
        for (auto s : shaders)
            glDeleteShader(s);
        glDeleteProgram(p);
        throw;
    }

    for (auto s : shaders)
    {
        glDetachShader(p, s);
        glDeleteShader(s);
    }

    return p;
}

void checkGl(const char* operation)
{
    GLenum e = glGetError();

    if (e != GL_NO_ERROR)
        throw std::runtime_error(std::string(operation) + ": OpenGL error " + std::to_string(e));
}

struct StoredState
{
    Float4 p;
    Float4 v;
    Float4 throughput;
    Float4 radiance;
    Float4 integration;

    Int4 meta;
    std::array<std::uint32_t, 4> random;
};

static_assert(sizeof(StoredState) == 112 && sizeof(SphereData) == 32 && sizeof(MaterialData) == 32 &&
                  sizeof(TextureData) == 48,
              "GPU storage layout mismatch");
} // namespace

struct GpuRenderer::Impl
{
    GLuint trace = 0;
    GLuint display = 0;
    GLuint vao = 0;
    GLuint texture = 0;
    GLuint displayedTexture = 0;
    RenderSettings displayedSettings;
    GLuint buffers[7]{};
    GLuint query = 0;

    GLsync fence = nullptr;
    RenderSettings settings;
    SceneData observerGeometry; // Only sphere/material metadata needed for camera-frame selection.
    GpuProgress stats;
    bool resetPending = true;
    bool hasImage = false;
    bool sceneReady = false;
    bool apiReady = false;
    std::string deviceName;
    GLint64 maxStorage = 0;

    ~Impl()
    {
        if (!apiReady)
            return;

        if (fence)
        {
            glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
            glDeleteSync(fence);
        }

        glDeleteQueries(1, &query);
        glDeleteBuffers(7, buffers);
        glDeleteTextures(1, &texture);
        glDeleteTextures(1, &displayedTexture);
        glDeleteVertexArrays(1, &vao);

        if (trace)
            glDeleteProgram(trace);

        if (display)
            glDeleteProgram(display);
    }

    void wait()
    {
        if (!fence)
            return;
        GLenum result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);

        if (result == GL_WAIT_FAILED)
            throw std::runtime_error("GPU wait failed");
    }

    void buffer(int binding, const void* data, std::size_t bytes)
    {
        if (bytes > std::size_t(maxStorage))
            throw std::runtime_error(
                "Scene or image exceeds GPU shader-storage block limit; reduce resolution");
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[binding]);
        glBufferData(
            GL_SHADER_STORAGE_BUFFER, GLsizeiptr(std::max<std::size_t>(bytes, 16)), data, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GLuint(binding), buffers[binding]);
    }

    void integer(const char* name, int v)
    {
        glUniform1i(glGetUniformLocation(trace, name), v);
    }
};

GpuRenderer::GpuRenderer(const std::filesystem::path& assets) : impl(new Impl)
{
    glewExperimental = GL_TRUE;
    auto error = glewInit();

    if (error != GLEW_OK)
        throw std::runtime_error("GLEW initialization failed");

    while (glGetError() != GL_NO_ERROR)
    {
    } // GLEW may probe compatibility-only extensions.
    if (!GLEW_VERSION_4_3)
        throw std::runtime_error(
            "OpenGL 4.3 compute shaders are required. Update the GPU driver or select a supported GPU.");
    auto& g = *impl;
    g.apiReady = true;
    g.deviceName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &g.maxStorage);

    auto source = readText(assets / "shaders" / "trace.comp");
    auto marker = source.find("// TRACE_CORE");

    if (marker == std::string::npos)
        throw std::runtime_error("Missing trace shader include marker");
    source.replace(
        marker, std::string("// TRACE_CORE").size(), readText(assets / "shaders" / "TraceCore.inl"));
    auto lightingMarker = source.find("#include \"LightingCore.inl\"");
    if (lightingMarker == std::string::npos)
        throw std::runtime_error("Missing lighting shader include marker");

    source.replace(lightingMarker,
                   std::string("#include \"LightingCore.inl\"").size(),
                   readText(assets / "shaders" / "LightingCore.inl"));
    g.trace = program({{GL_COMPUTE_SHADER, source}});
    g.display = program({{GL_VERTEX_SHADER, readText(assets / "shaders" / "fullscreen.vert")},
                         {GL_FRAGMENT_SHADER, readText(assets / "shaders" / "display.frag")}});
    glGenBuffers(7, g.buffers);
    glGenVertexArrays(1, &g.vao);
    glGenQueries(1, &g.query);
    g.buffer(6, nullptr, 32);
    checkGl("Renderer initialization");
}

GpuRenderer::~GpuRenderer() = default;

std::array<int, 2> GpuRenderer::fitResolution(int width, int height) const
{
    if (width <= 0 || height <= 0)
        throw std::runtime_error("Window dimensions must be positive");
    GLint textureLimit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &textureLimit);

    // Keep interactive state below both the driver block limit and 256 MiB.
    double maxPixels = double(std::min<GLint64>(impl->maxStorage, 256ll * 1024 * 1024)) / sizeof(StoredState);
    double sideLimit = std::min(textureLimit, 8192);
    double scale = std::min(
        {1.0, sideLimit / width, sideLimit / height, std::sqrt(maxPixels / (double(width) * height))});
    return {std::max(1, int(std::floor(width * scale))), std::max(1, int(std::floor(height * scale)))};
}

void GpuRenderer::uploadScene(const SceneData& scene)
{
    impl->observerGeometry.spheres = scene.spheres;
    impl->observerGeometry.materials = scene.materials;
    scene.validate();
    auto& g = *impl;
    g.wait();
    poll();
    g.buffer(0, scene.spheres.data(), scene.spheres.size() * sizeof(SphereData));
    g.buffer(1, scene.materials.data(), scene.materials.size() * sizeof(MaterialData));
    g.buffer(2, scene.textures.data(), scene.textures.size() * sizeof(TextureData));

    auto texels = scene.texels;
    const auto& thermal = blackbodyTable();
    texels.insert(texels.end(), thermal.begin(), thermal.end());
    g.buffer(3, texels.data(), texels.size() * sizeof(Float4));

    glUseProgram(g.trace);
    int hole = -1;
    int earth = -1;
    for (int i = 0; i < int(scene.spheres.size()); ++i)
    {
        int kind = scene.materials[scene.spheres[i].material.x].kindTexture.x;
        if (kind == Schwarzschild)
            hole = i;
        if (kind == Earth)
            earth = i;
    }

    g.integer("holeIndex", hole);
    g.integer("planetIndex", earth);
    g.integer("thermalOffset", int(scene.texels.size()));
    g.integer("diskOn", scene.disk.enabled ? 1 : 0);
    auto normal = normalized(scene.disk.normal);
    glUniform3f(glGetUniformLocation(g.trace, "diskAxis"), float(normal.x), float(normal.y), float(normal.z));
    glUniform4f(glGetUniformLocation(g.trace, "diskConfig"),
                scene.disk.innerRadius,
                scene.disk.outerRadius,
                scene.disk.peakTemperature,
                scene.disk.emissionScale);
    glUniform1f(glGetUniformLocation(g.trace, "airHeight"), scene.atmosphereHeight);
    g.sceneReady = true;
    checkGl("Scene upload");
}

void GpuRenderer::reset(const RenderSettings& settings, const CameraData& camera)
{
    settings.validate();
    auto basis = camera.basis(double(settings.width) / settings.height);
    auto& g = *impl;
    g.wait();
    poll();

    if (!g.sceneReady)
        throw std::runtime_error("Upload a scene before rendering");
    GLint maxTexture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);

    if (settings.width > maxTexture || settings.height > maxTexture)
        throw std::runtime_error("Image exceeds GPU texture limit");
    auto pixels = std::size_t(settings.width) * settings.height;
    bool resized = !g.texture || settings.width != g.settings.width || settings.height != g.settings.height;

    if (resized)
    {
        g.buffer(4, nullptr, pixels * sizeof(StoredState));
        g.buffer(5, nullptr, 16);

        glDeleteTextures(1, &g.texture);
        glGenTextures(1, &g.texture);
        glBindTexture(GL_TEXTURE_2D, g.texture);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, settings.width, settings.height);
        g.hasImage = false;
    }
    else
        glBindTexture(GL_TEXTURE_2D, g.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindImageTexture(0, g.texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);

    g.settings = settings;
    g.stats = {};
    g.resetPending = true;
    g.hasImage = false;

    glUseProgram(g.trace);
    glUniform2i(glGetUniformLocation(g.trace, "dimensions"), settings.width, settings.height);
    glUniform1ui(glGetUniformLocation(g.trace, "seed"), settings.seed);
    g.integer("targetSamples", settings.samples);
    g.integer("testMode", 0);
    g.integer("maxAttempts", settings.maxIntegrationAttempts);
    g.integer("useRedshift", settings.redshift ? 1 : 0);
    g.integer("gravityMode", int(settings.integrationMode));
    auto observer = observerSettings(g.observerGeometry, settings, camera);
    g.integer("observerType", int(settings.observerType));
    g.integer("useObserverFrame", observer.useObserverFrame ? 1 : 0);
    glUniform3f(glGetUniformLocation(g.trace, "observerBeta"),
                float(observer.observerVelocity.x),
                float(observer.observerVelocity.y),
                float(observer.observerVelocity.z));

    glUniform1f(glGetUniformLocation(g.trace, "relTolerance"), settings.relativeTolerance);
    glUniform1f(glGetUniformLocation(g.trace, "absTolerance"), settings.absoluteTolerance);
    glUniform1f(glGetUniformLocation(g.trace, "maxStep"), settings.maxStep);

    const char* names[] = {"cameraOrigin", "cameraCorner", "cameraHorizontal", "cameraVertical"};

    for (int i = 0; i < 4; ++i)
        glUniform3f(
            glGetUniformLocation(g.trace, names[i]), float(basis[i].x), float(basis[i].y), float(basis[i].z));
    checkGl("Renderer reset");
}

bool GpuRenderer::poll()
{
    auto& g = *impl;

    if (!g.fence)
        return false;
    auto result = glClientWaitSync(g.fence, 0, 0);

    if (result == GL_TIMEOUT_EXPIRED)
        return false;

    if (result == GL_WAIT_FAILED)
        throw std::runtime_error("GPU fence failed");
    glDeleteSync(g.fence);
    g.fence = nullptr;

    GLuint64 ns = 0;
    glGetQueryObjectui64v(g.query, GL_QUERY_RESULT, &ns);

    std::uint32_t counters[4]{};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g.buffers[5]);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(counters), counters);

    g.stats.lastBatchMilliseconds = double(ns) * 1e-6;
    g.stats.totalGpuMilliseconds += g.stats.lastBatchMilliseconds;
    g.stats.maxBatchMilliseconds = std::max(g.stats.maxBatchMilliseconds, g.stats.lastBatchMilliseconds);
    g.stats.failures += counters[1];
    g.stats.meanSamples = double(counters[2]) / (double(g.settings.width) * g.settings.height);
    g.stats.finished = counters[0] == 0;
    g.stats.firstPassComplete = counters[3] == 0;
    g.hasImage = true;

    // Keep the last coherent image visible across camera resets and resizes.
    // Never publish the black placeholders of an unfinished first pass.
    if (g.stats.firstPassComplete)
    {
        if (!g.displayedTexture || g.displayedSettings.width != g.settings.width ||
            g.displayedSettings.height != g.settings.height)
        {
            glDeleteTextures(1, &g.displayedTexture);
            glGenTextures(1, &g.displayedTexture);
            glBindTexture(GL_TEXTURE_2D, g.displayedTexture);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, g.settings.width, g.settings.height);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        glCopyImageSubData(g.texture,
                           GL_TEXTURE_2D,
                           0,
                           0,
                           0,
                           0,
                           g.displayedTexture,
                           GL_TEXTURE_2D,
                           0,
                           0,
                           0,
                           0,
                           g.settings.width,
                           g.settings.height,
                           1);
        g.displayedSettings = g.settings;
    }
    checkGl("GPU completion");

    return true;
}

bool GpuRenderer::dispatch()
{
    auto& g = *impl;

    if (!g.texture)
        throw std::runtime_error("Reset the renderer before dispatch");

    if (g.fence || g.stats.finished)
        return false;
    std::uint32_t counters[4]{};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g.buffers[5]);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(counters), counters);

    glUseProgram(g.trace);
    g.integer("resetState", g.resetPending ? 1 : 0);
    g.integer("firstPassOnly", g.stats.firstPassComplete ? 0 : 1);

    glBeginQuery(GL_TIME_ELAPSED, g.query);
    glDispatchCompute(GLuint((g.settings.width + 7) / 8), GLuint((g.settings.height + 7) / 8), 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_FETCH_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT |
                    GL_TEXTURE_UPDATE_BARRIER_BIT);
    glEndQuery(GL_TIME_ELAPSED);

    g.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    g.resetPending = false;
    checkGl("Compute dispatch");

    return true;
}

void GpuRenderer::present(int width, int height)
{
    auto& g = *impl;
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!g.displayedTexture)
        return;
    glUseProgram(g.display);
    glUniform1f(glGetUniformLocation(g.display, "exposure"), g.displayedSettings.exposure);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g.displayedTexture);
    glUniform1i(glGetUniformLocation(g.display, "accumulation"), 0);
    glBindVertexArray(g.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void GpuRenderer::saveDisplayed(const std::filesystem::path& path)
{
    auto& g = *impl;
    if (!g.displayedTexture)
        throw std::runtime_error("The first image is still rendering");

    const auto& settings = g.displayedSettings;
    std::vector<Float4> pixels(std::size_t(settings.width) * settings.height);
    glBindTexture(GL_TEXTURE_2D, g.displayedTexture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
    for (auto& pixel : pixels)
    {
        pixel.x /= pixel.w;
        pixel.y /= pixel.w;
        pixel.z /= pixel.w;
    }

    checkGl("Displayed image readback");
    savePng(path, settings.width, settings.height, pixels, settings.exposure);
}

std::vector<Float4> GpuRenderer::readback()
{
    auto& g = *impl;
    g.wait();
    poll();
    std::vector<Float4> pixels(std::size_t(g.settings.width) * g.settings.height);

    if (!g.hasImage)
        return pixels;
    glBindTexture(GL_TEXTURE_2D, g.texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());

    for (auto& c : pixels)
    {
        float n = std::max(c.w, 1.f);
        c.x /= n;
        c.y /= n;
        c.z /= n;
    }

    checkGl("Image readback");

    return pixels;
}

std::vector<RayResult> GpuRenderer::traceRays(const SceneData& scene,
                                              const RenderSettings& settings,
                                              const std::vector<std::array<Vec3, 2>>& rays)
{
    if (rays.empty())
        return {};
    auto s = settings;
    s.width = int(rays.size());
    s.height = 1;
    s.samples = 1;
    uploadScene(scene);
    reset(s, CameraData{});
    auto& g = *impl;
    std::vector<Float4> input;

    for (auto ray : rays)
        for (auto v : ray)
            input.push_back({float(v.x), float(v.y), float(v.z), 0});
    g.buffer(6, input.data(), input.size() * sizeof(Float4));
    glUseProgram(g.trace);
    g.integer("testMode", 1);
    // Diagnostic rays have no camera basis: their supplied velocity uses the
    // same world-aligned tetrad convention as traceCpu's low-level API.
    g.integer("useObserverFrame", settings.useObserverFrame ? 1 : 0);
    glUniform3f(glGetUniformLocation(g.trace, "observerBeta"),
                float(settings.observerVelocity.x),
                float(settings.observerVelocity.y),
                float(settings.observerVelocity.z));

    while (!g.stats.finished)
    {
        dispatch();
        g.wait();
        poll();
    }

    std::vector<StoredState> states(rays.size());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g.buffers[4]);
    glGetBufferSubData(
        GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(states.size() * sizeof(StoredState)), states.data());
    std::vector<RayResult> result;

    for (auto a : states)
        result.push_back({{a.radiance.x, a.radiance.y, a.radiance.z},
                          {a.p.x, a.p.y, a.p.z},
                          {a.v.x, a.v.y, a.v.z},
                          a.meta.w,
                          a.meta.y});
    checkGl("Diagnostic ray readback");

    return result;
}

const GpuProgress& GpuRenderer::progress() const
{
    return impl->stats;
}

const std::string& GpuRenderer::device() const
{
    return impl->deviceName;
}
} // namespace rt
