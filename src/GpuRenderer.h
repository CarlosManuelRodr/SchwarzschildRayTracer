/**
 * @file
 * @brief OpenGL progressive rendering, presentation, picking, and readback.
 */
#pragma once
#include "CpuRenderer.h"
#include <memory>

namespace rt
{
    /**
     * @brief Borrowed OpenGL texture and pixel dimensions for the viewport.
     *
     * The renderer owns the texture; consumers must not delete it. Texture 0 means no image.
     */
    struct DisplayImage
    {
        unsigned int texture = 0;
        int width = 0, height = 0;
    };

    /**
     * @brief Statistics from completed GPU batches; polling refreshes these values.
     *
     * Times are elapsed GPU milliseconds, not wall-clock completion times. failures
     * counts numerical terminations; firstPassComplete means every pixel has a sample.
     */
    struct GpuProgress
    {
        double meanSamples = 0, lastBatchMilliseconds = 0, maxBatchMilliseconds = 0, totalGpuMilliseconds = 0;
        std::uint64_t failures = 0;
        bool finished = false;
        bool firstPassComplete = false;
    };

    /**
     * @brief Own the OpenGL programs, scene buffers, ray states, and display textures.
     *
     * Requires a current OpenGL 4.3 context on the calling thread, including destruction.
     * Typical use: uploadScene, reset, then dispatch/poll between input events.
     * Presentation retains the previous image until the new first sample pass completes.
     * Methods throw std::runtime_error on capability, shader, asset, or GL failures.
     */
    class GpuRenderer
    {
    public:
        /**
         * @brief Load shader assets, check GPU capabilities, and allocate renderer resources.
         */
        explicit GpuRenderer(const std::filesystem::path &assets);

        /**
         * @brief Release GPU resources while the owning OpenGL context is still current.
         */
        ~GpuRenderer();
        GpuRenderer(const GpuRenderer &) = delete;
        GpuRenderer &operator=(const GpuRenderer &) = delete;

        /**
         * @brief Validate and upload the full scene, textures, and thermal lookup table.
         *
         * Call reset before starting a new render.
         */
        void uploadScene(const SceneData &scene);

        /**
         * @brief Upload edited sphere geometry without reuploading textures.
         *
         * Sphere count and material identity must remain stable. Call reset to render the edits.
         */
        void updateGeometry(const SceneData &scene);

        /**
         * @brief Pick against the camera and geometry snapshot of the displayed image.
         *
         * Inputs are lower-left normalized image coordinates; returns -1 for no selectable body.
         */
        int pickDisplayed(double u, double v) const;

        /**
         * @brief Return one image-space gizmo anchor per sphere, indexed by stable body ID.
         *
         * Each anchor is (u,v,visible), with lower-left UVs and visible equal to 0 or 1.
         * Anchors lie on the largest connected first-pass image and publish with that image.
         */
        const std::vector<Vec3> &bodyAnchors() const;

        /**
         * @brief Validate camera/settings and restart accumulation after any view or scene change.
         *
         * Requires an uploaded scene; waits for outstanding work before replacing its state.
         */
        void reset(const RenderSettings &settings, const CameraData &camera);

        /**
         * @brief Fit a requested resolution within texture and ray-state buffer limits.
         */
        std::array<int, 2> fitResolution(int width, int height) const;

        /**
         * @brief Submit one bounded GPU batch without waiting for its completion.
         * @return True if submitted; false if work is already in flight or rendering has finished.
         */
        bool dispatch();

        /**
         * @brief Check the batch fence without blocking and publish any completed results.
         * @return True if a completed batch was consumed; false otherwise.
         */
        bool poll();

        /**
         * @brief Draw the published image into the currently bound framebuffer at the requested size.
         *
         * Applies bloom, exposure, tone mapping, and explicit sRGB encoding; changes GL draw state.
         */
        void present(int width, int height);

        /**
         * @brief Produce the tone-mapped viewport texture with bottom-up UVs.
         *
         * Returns an empty descriptor until a complete first-pass image is available.
         */
        DisplayImage displayImage();

        /**
         * @brief Read the published image as top-row-first sRGB RGBA8 and report its dimensions.
         *
         * May synchronize with the GPU. Throws if the first displayed image is not yet available.
         */
        std::vector<unsigned char> readDisplayedRgba(int &width, int &height);

        /**
         * @brief Save the currently published image as a PNG, including its display transform.
         */
        void saveDisplayed(const std::filesystem::path &path);

        /**
         * @brief Wait for pending work and read current averaged linear RGB, bottom row first.
         *
         * The w lane retains each pixel's sample count. This may precede first-pass publication.
         */
        std::vector<Float4> readback();

        /**
         * @brief Replace render state and synchronously trace diagnostic origin/direction pairs.
         *
         * Observer velocity uses world-aligned local-frame components, as in traceCpu.
         * Used for CPU/GPU numerical comparisons, not the interactive scheduling loop.
         */
        std::vector<RayResult> traceRays(const SceneData &, const RenderSettings &,
                                         const std::vector<std::array<Vec3, 2>> &rays);

        /**
         * @brief Return the latest polled statistics; this accessor does not synchronize.
         */
        const GpuProgress &progress() const;

        /**
         * @brief Return the detected OpenGL device description.
         */
        const std::string &device() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace rt
