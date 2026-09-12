/**
 * @file
 * @brief Shared ray state machine and Schwarzschild geodesic integration.
 * The GPU compiles this as GLSL float; CpuRenderer.cpp includes it with double-precision
 * scalar/vector adapters. Keep random draws explicitly sequenced for reproducibility.
 * Distances use horizon radii (r_s = 1). Integration follows a past-directed affine
 * parameter, not coordinate time: v must not be normalized between accepted steps.
 * Scene accessors and INOUT/OUT are supplied by the host. LightingCore.inl supplies
 * emission and surface/volume shading. See docs/observer-and-horizon.md for derivations.
 */
const real PI = real(3.14159265358979323846);
const real EPS = real(0.0001);

/**
 * @brief One resumable sample, preserved between bounded compute dispatches.
 * p is world position; v is the past-directed spatial tangent dx/dlambda.
 * throughput is the remaining RGB weight and radiance is accumulated linear light.
 * h2 is squared angular momentum about the hole; step is an affine integration step.
 * energy is the conserved future-photon Killing energy with camera frequency set to 1.
 * observerLapse is a historical name: it stores energy for physical observers,
 * the camera lapse for legacy rays, or 1 when frequency shifts are disabled.
 * field is a sphere index while integrating, -1 outside, or -2 after an adaptive cutoff.
 * attempts counts accepted AND rejected steps in the current traversal. depth bounds
 * surface/field transitions. status: 0 active, 1 finished, 2 numerical failure,
 * 3 captured/unavailable source or frame; the GPU also uses -1 to request a new sample.
 * rng holds the deterministic random stream; body records the first visible surface.
 */
struct TraceState
{
    vec3 p;
    vec3 v;
    vec3 throughput;
    vec3 radiance;
    real h2;
    real step;
    real observerLapse;
    real energy;
    int observerMode; // 0: legacy fixture, 1: exterior observer, 3: horizon/interior observer.
    int body;         // First visible surface, for editor anchoring.
    int field;
    int attempts;
    int depth;
    int status;
    uint rng;
};

/**
 * @brief Ray-segment intersection: parameter t, world point, outward normal, UVs, and sphere ID.
 * t is a fraction for a segment direction, and a distance only for a unit direction.
 */
struct SurfaceHit
{
    real t;
    vec3 p;
    vec3 normal;
    real u;
    real v;
    int sphere;
};

/**
 * @brief Normalize a vector, returning zero when its length is at most 1e-20.
 */
vec3 safeUnit(vec3 v)
{
    real n = length(v);

    return n > real(1e-20) ? v / n : vec3(0);
}

/**
 * @brief Scramble a 32-bit key to seed independent pixel/sample random streams.
 */
uint hashBits(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;

    return x ^ (x >> 16);
}

/**
 * @brief Advance the 32-bit LCG and return its upper 24 bits scaled to [0,1).
 */
real randomValue(INOUT(uint) state)
{
    state = 1664525u * state + 1013904223u;

    return real(state >> 8) * real(1.0 / 16777216.0);
}

/**
 * @brief Sample within the unit ball, limiting rejection attempts to 32 before returning zero.
 */
vec3 randomSphere(INOUT(uint) state)
{
    for (int i = 0; i < 32; ++i)
    {
        // Explicit sequencing is necessary for CPU/GLSL RNG parity.
        real x = randomValue(state);
        real y = randomValue(state);
        real z = randomValue(state);
        vec3 p = real(2) * vec3(x, y, z) - vec3(1);

        if (dot(p, p) < real(1))
            return p;
    }

    return vec3(0);
}

/**
 * @brief Check that all three components are finite using the host scalar predicate.
 */
bool finiteVector(vec3 v)
{
    return finiteScalar(v.x) && finiteScalar(v.y) && finiteScalar(v.z);
}

/**
 * @brief Return the nearest quadratic root strictly inside (lo,hi).
 * The direction may be non-unit; zero directions are rejected and tangencies accepted.
 * The output t is meaningful only when the function returns true.
 */
bool sphereRoot(vec3 p, vec3 v, vec3 center, real radius, real lo, real hi, OUT(real) t)
{
    vec3 oc = p - center;
    real a = dot(v, v);
    real b = dot(oc, v);
    real c = dot(oc, oc) - radius * radius;
    real d = b * b - a * c;

    if (a <= real(1e-30) || d < real(0))
        return false;

    real root = sqrt(d);
    t = (-b - root) / a;

    if (t > lo && t < hi)
        return true;

    t = (-b + root) / a;

    return t > lo && t < hi;
}

/**
 * @brief Construct a world-space surface hit and equirectangular UV coordinates.
 * Normalizing the distant environment normal keeps rounding from corrupting sky lookup.
 */
SurfaceHit makeHit(int sphere, vec3 p, vec3 v, real t)
{
    SurfaceHit hit;
    hit.t = t;
    hit.p = p + t * v;
    hit.sphere = sphere;
    hit.normal = (hit.p - sphereCenter(sphere)) / sphereRadius(sphere);

    if (materialKind(sphereMaterial(sphere)) == 6)
        hit.normal = safeUnit(hit.normal);
    hit.u = real(1) - (atan(hit.normal.z, hit.normal.x) + PI) / (real(2) * PI);
    hit.v = (asin(clamp(hit.normal.y, real(-1), real(1))) + PI / real(2)) / PI;

    return hit;
}

/**
 * @brief Find the nearest sphere hit; solidsOnly skips the gravity-region boundary.
 * The environment has a narrow outward rounding allowance to prevent missed sky hits.
 */
bool worldHit(vec3 p, vec3 v, real lo, real hi, bool solidsOnly, OUT(SurfaceHit) hit)
{
    bool found = false;

    for (int i = 0; i < sphereCount(); ++i)
    {
        if (solidsOnly && materialKind(sphereMaterial(i)) == 4)
            continue;

        real t = real(0);
        bool intersects = sphereRoot(p, v, sphereCenter(i), sphereRadius(i), lo, hi, t);

        // Integrated rays can round onto the environment surface before the
        // quadratic reports a positive root. Accept an outward zero-distance
        // hit in a small float-precision band instead of stepping past the sky.
        // Apply this only to the terminal environment, never ordinary geometry.
        if (!intersects && materialKind(sphereMaterial(i)) == 6)
        {
            vec3 relative = p - sphereCenter(i);
            real radius = sphereRadius(i);
            real roundingBand = real(8 * 1.1920928955078125e-7) * max(real(1), radius);

            if (abs(length(relative) - radius) <= roundingBand && dot(relative, v) > real(0))
            {
                t = real(0);
                intersects = true;
            }
        }

        if (intersects)
        {
            hi = t;
            hit = makeHit(i, p, v, t);
            found = true;
        }
    }

    return found;
}

/**
 * @brief Evaluate indexed constant/checker/image textures without recursive calls.
 * Scene validation guarantees an acyclic texture graph with at most 32 lookup levels.
 * Image accessors decode color to linear light while leaving data maps linear.
 */
vec3 textureValue(int index, real u, real v, vec3 p)
{
    for (int level = 0; level < 32; ++level)
    {
        int kind = textureKind(index);

        if (kind == 0)
            return textureColor(index);

        if (kind == 1)
        {
            index = textureChild(index, sin(p.x) * sin(p.y) * sin(p.z) < real(0));
            continue;
        }

        return imageValue(index, u, v);
    }

    return vec3(0); // Scene validation rejects cycles/depth overflow.
}

#include "LightingCore.inl"

/**
 * @brief Initialize a primary ray, its random stream, and its observer-frame tangent.
 * p is camera position and v initially points from the pixel toward the source.
 * The arriving photon points along -v. Boost its unit camera frequency/momentum into
 * the selected local frame, then reverse its coordinate tangent for backward tracing.
 * Freely falling frames use ingoing Painleve-Gullstrand coordinates across the horizon;
 * Hovering is valid only outside. Nonpositive-energy exterior-source rays terminate.
 */
void initTrace(OUT(TraceState) s, vec3 p, vec3 v, uint seed)
{
    s.p = p;
    s.v = safeUnit(v);
    s.throughput = vec3(1);
    s.radiance = vec3(0);
    s.h2 = real(0);
    s.step = maximumStep();
    s.body = -1;
    s.field = -1;
    s.attempts = 0;
    s.depth = 0;
    s.status = 0;
    s.rng = seed;
    s.observerLapse = lapseAt(p);
    s.energy = s.observerLapse;
    s.observerMode = 0;

    if (observerFrameEnabled())
    {
        // The pixel points toward the source. The arriving future-directed
        // photon points the other way and has unit frequency in the camera.
        vec3 incoming = -s.v;
        vec3 beta = observerVelocity();
        real gamma = real(1) / sqrt(real(1) - dot(beta, beta));
        real projection = dot(beta, incoming);
        real frequency = gamma * (real(1) + projection);
        vec3 momentum = incoming + (gamma + gamma * gamma / (gamma + real(1)) * projection) * beta;
        vec3 flow = vec3(0);
        s.observerMode = 1;
        int hole = blackHole();

        if (hole >= 0)
        {
            vec3 relative = p - sphereCenter(hole);
            real radius = length(relative);

            if (radius <= real(1e-4))
            {
                s.status = 3; // The singularity has no observer frame.
                return;
            }

            flow = relative / (radius * sqrt(radius));

            if (radius <= real(1))
                s.observerMode = 3;
        }

        if (hoveringObserver() && s.observerMode >= 2)
        {
            s.status = 3; // A static observer has no timelike frame at or inside the horizon.
            return;
        }

        // Past-directed affine spatial tangent in ingoing Painleve-Gullstrand
        // coordinates. This remains finite on the future event horizon.
        s.v = flow * frequency - momentum;
        s.energy = frequency - dot(flow, momentum);

        if (hoveringObserver() && s.observerMode < 2)
        {
            real lapse = sqrt(max(real(1e-12), real(1) - dot(flow, flow)));
            vec3 radial = safeUnit(flow);
            s.v = -(momentum + (lapse - real(1)) * dot(momentum, radial) * radial);
            s.energy = lapse * frequency;
        }

        s.observerLapse = redshiftEnabled() ? s.energy : real(1);

        if (s.energy <= real(0))
            s.status = 3; // No positive-energy exterior source on this past ray.
    }
}

/**
 * @brief Terminate as a numerical failure and discard nonphysical accumulated radiance.
 */
void failTrace(INOUT(TraceState) s)
{
    s.radiance = vec3(0);
    s.status = 2;
}

/**
 * @brief Add terminal surface emission/lighting or choose the next scattering direction.
 * Material IDs match MaterialKind in SceneData.h. Scattering occurs in the local static
 * frame, then converts back to an affine tangent; stationary surfaces inside are invalid.
 * CPU picking returns the first body without evaluating surface lighting.
 */
void scatterSurface(INOUT(TraceState) s, SurfaceHit hit)
{
    if (s.body < 0)
        s.body = hit.sphere;
#ifdef __cplusplus
    if (picking)
    {
        pickedBody = hit.sphere;
        s.status = 1;

        return;
    }
#endif
    int mat = sphereMaterial(hit.sphere);
    int kind = materialKind(mat);
    vec3 color = textureValue(materialTexture(mat), hit.u, hit.v, hit.p);

    if (kind == 3 || kind == 6)
    {
        vec3 emitted = kind == 6
                           ? materialParameter(mat) * color * pow(lapseAt(hit.p) / s.observerLapse, real(4))
                           : surfaceRadiance(hit, -s.v, s.observerLapse);
        s.radiance += s.throughput * emitted;
        s.status = 1;

        return;
    }

    if (kind == 5 || kind == 7)
    {
        s.radiance += s.throughput * illuminateEarth(s, hit, color);
        s.status = 1;

        return;
    }

    if (s.depth >= 50)
    {
        s.status = 1;

        return;
    }

    ++s.depth;
    vec3 d = staticDirection(hit.p, s.v), n = hit.normal;

    if (kind == 0)
    {
        s.v = safeUnit(n + randomSphere(s.rng));
        s.throughput *= color;
    }
    else if (kind == 1)
    {
        s.v = safeUnit(d - real(2) * dot(d, n) * n +
                       clamp(materialParameter(mat), real(0), real(1)) * randomSphere(s.rng));
        s.throughput *= color;

        if (dot(s.v, n) <= real(0))
        {
            s.status = 1;

            return;
        }
    }
    else if (kind == 2)
    {
        real ior = materialParameter(mat);
        bool front = dot(d, n) < real(0);

        if (!front)
            n = -n;
        real eta = front ? real(1) / ior : ior;
        real cosine = clamp(dot(-d, n), real(0), real(1));
        real disc = real(1) - eta * eta * (real(1) - cosine * cosine);
        real r0 = (real(1) - ior) / (real(1) + ior);
        r0 *= r0;
        real probability = r0 + (real(1) - r0) * pow(real(1) - cosine, real(5));
        real choice = randomValue(s.rng);
        s.v = disc <= real(0) || choice < probability ? d - real(2) * dot(d, n) * n
                                                      : eta * (d + cosine * n) - sqrt(max(disc, real(0))) * n;
    }

    if (dot(s.v, s.v) < real(1e-20))
        s.v = n;
    s.p = hit.p + EPS * safeUnit(s.v);

    if (s.observerMode != 0)
    {
        vec3 radial = vec3(0);
        real lapse = real(1);

        if (blackHole() >= 0)
        {
            vec3 offset = hit.p - sphereCenter(blackHole());
            real radius = length(offset);

            if (radius <= real(1))
            {
                s.status = 3; // Stationary material frames do not exist inside.
                return;
            }

            radial = offset / radius;
            lapse = sqrt(real(1) - real(1) / radius);
        }

        s.v = s.energy / lapse * (s.v + (lapse - real(1)) * dot(s.v, radial) * radial);
    }

    // Reinitialize angular momentum after a physical surface scattering.
    s.field = -1;
}

/**
 * @brief Begin a gravity traversal and freeze h2 = |(p-center) cross v| squared.
 * Resets the attempt counter and initial step; field entry also consumes a depth slot.
 */
void enterField(INOUT(TraceState) s, int field)
{
    if (s.depth >= 50)
    {
        s.status = 1;

        return;
    }

    ++s.depth;
    s.field = field;
    s.attempts = 0;
    s.step = maximumStep();
    vec3 angular = cross(s.p - sphereCenter(field), s.v);
    s.h2 = dot(angular, angular);
}

/**
 * @brief Evaluate d2x/dlambda2 = -(3/2) h2 (x-center) / r^5, with r_s = 1.
 * This is the Cartesian form of the Schwarzschild null-orbit equation. h2 remains
 * constant on a free trajectory; the center-relative vector preserves translation invariance.
 */
vec3 acceleration(vec3 p, vec3 center, real h2)
{
    vec3 r = p - center;
    real r2 = max(dot(r, r), real(1e-20));

    return -real(1.5) * h2 * r / (r2 * r2 * sqrt(r2));
}

/**
 * @brief Advance position and affine tangent with one classical fourth-order Runge-Kutta step.
 * The intermediate accelerations sample the coupled system p' = v, v' = acceleration(p).
 * This routine estimates no error; integrateField compares a full step against two half steps.
 */
void rk4(vec3 p, vec3 v, real dt, vec3 center, real h2, OUT(vec3) p1, OUT(vec3) v1)
{
    vec3 a = acceleration(p, center, h2);

    vec3 vb = v + real(0.5) * dt * a;
    vec3 b = acceleration(p + real(0.5) * dt * v, center, h2);

    vec3 vc = v + real(0.5) * dt * b;
    vec3 c = acceleration(p + real(0.5) * dt * vb, center, h2);

    vec3 vd = v + dt * c;
    vec3 d = acceleration(p + dt * vc, center, h2);

    p1 = p + dt / real(6) * (v + real(2) * vb + real(2) * vc + vd);
    v1 = v + dt / real(6) * (a + real(2) * b + real(2) * c + d);
}

/**
 * @brief Test whether the remaining flight may be replaced by a straight segment.
 * Bounds position and angular error using acceleration around the proposed path.
 * This weak-field approximation does not guarantee identical grazing visibility.
 */
bool negligibleBending(TraceState s, real backgroundRadius)
{
    // Bound acceleration in a tube around the proposed straight segment.
    // Its radius is half the segment's closest approach to the hole. Accept
    // only if the displacement bound stays inside that tube and within the
    // positional/angular tolerances. This is a per-flight weak-field estimate,
    // not a guarantee of identical visibility at a grazing silhouette.
    vec3 center = sphereCenter(s.field);
    real speed = length(s.v);
    vec3 direction = safeUnit(s.v);
    real distance = real(0);

    if (!sphereRoot(s.p, direction, center, backgroundRadius, EPS, real(1e30), distance))
        return false;

    SurfaceHit hit;

    if (worldHit(s.p, direction, EPS, distance, true, hit))
        distance = hit.t;
    real diskDistance = real(0);

    if (diskHit(s.p, direction, EPS, distance, diskDistance))
        distance = diskDistance;

    vec3 relative = s.p - center;
    real closestT = clamp(-dot(relative, direction), real(0), distance);
    real closestRadius = length(relative + closestT * direction);

    if (closestRadius <= real(3) || speed <= real(1e-8))
        return false;

    real tubeRadius = closestRadius / real(2);
    real bound = real(1.5) * s.h2 / pow(tubeRadius, real(4));
    real duration = distance / speed;
    real displacement = real(0.5) * bound * duration * duration;
    real directionError = bound * duration / speed;

    return displacement < tubeRadius &&
           displacement <= absoluteTolerance() + relativeTolerance() * distance &&
           directionError <= relativeTolerance();
}

/**
 * @brief Attempt one error-controlled RK4 step and process the nearest segment event.
 * One full step and two half steps estimate error; dividing their difference by 15
 * accounts for fourth-order step doubling. Both position and tangent errors must fit
 * the absolute/relative tolerances. Rejected steps leave the ray at its previous point.
 * Accepted chords are checked for surfaces, disk, capture, and fixed-region exit before
 * committing a position. Step limits also bound chord error and resolve nearby geometry.
 * Interior observers may cross the horizon outward while tracing into the past.
 * A finite attempt budget and minimum step turn stalled/nonfinite paths into failures.
 */
void integrateField(INOUT(TraceState) s)
{
    vec3 center = sphereCenter(s.field);

    if (length(s.p - center) <= (s.observerMode >= 2 ? real(1e-4) : real(1)))
    {
        s.status = 3;

        return;
    }

    if (s.attempts >= maximumAttempts())
    {
        failTrace(s);

        return;
    }

    ++s.attempts;
    real radius = length(s.p - center);
    real speed = max(length(s.v), real(1e-8));

    // maxStep is the near-hole step scale. Far away, let steps grow smoothly
    // with radius, but never cross a large fraction of the radial scale at once.
    real stepLimit = maximumStep() / (s.observerMode != 0 ? max(real(1), speed) : real(1));

    if (integrationMode() != 0 || s.observerMode >= 2)
    {
        stepLimit = min(maximumStep() * max(real(1), radius * radius / real(9)), real(0.1) * radius / speed);
        real backgroundRadius = real(10);

        for (int i = 0; i < sphereCount(); ++i)
        {
            if (materialKind(sphereMaterial(i)) == 4)
                continue;

            real surfaceDistance = abs(length(s.p - sphereCenter(i)) - sphereRadius(i));
            stepLimit = min(stepLimit, max(real(0.005), real(0.25) * surfaceDistance) / speed);
            backgroundRadius =
                max(backgroundRadius, length(sphereCenter(i) - center) + sphereRadius(i) + real(1));

            if (materialKind(sphereMaterial(i)) == 5 && surfaceDistance < atmosphereHeight())
                stepLimit = min(stepLimit, atmosphereHeight() / (real(4) * speed));
        }

        if (diskEnabled())
            backgroundRadius = max(backgroundRadius, diskOuter() + real(1));

        if (integrationMode() == 2 && s.observerMode < 2 && negligibleBending(s, backgroundRadius))
        {
            s.field = -2; // The remaining straight flight has passed the error check.
            s.v = safeUnit(s.v);

            return;
        }

        // Scenes with no enclosing environment end on a finite background screen.
        // Gravity is never disabled and tracing never resumes as a straight ray.
        if (radius >= backgroundRadius && dot(s.p - center, s.v) > real(0))
        {
            s.v = safeUnit(s.v);
            real sky = real(0.5) * (s.v.y + real(1));
            real skyShift = s.observerMode != 0 ? pow(lapseAt(s.p) / s.observerLapse, real(4)) : real(1);
            s.radiance += skyShift * s.throughput *
                          ((real(1) - sky) * vec3(1) + sky * vec3(real(0.5), real(0.7), real(1)));
            s.status = 1;

            return;
        }

        // Bound chord error as well as RK error: accepted segments are also used
        // for disk/sphere intersections, so a long curved chord must stay accurate.
        real curvature = length(acceleration(s.p, center, s.h2));
        real chordTolerance = absoluteTolerance() + relativeTolerance() * max(real(1), radius);
        stepLimit = min(stepLimit, sqrt(real(8) * chordTolerance / max(curvature, real(1e-20))));
    }

    s.step = min(s.step, stepLimit);
    vec3 pf, vf, ph, vh, pn, vn;
    rk4(s.p, s.v, s.step, center, s.h2, pf, vf);
    rk4(s.p, s.v, s.step / real(2), center, s.h2, ph, vh);
    rk4(ph, vh, s.step / real(2), center, s.h2, pn, vn);

    real error =
        max(length(pn - pf) /
                (absoluteTolerance() + relativeTolerance() * max(length(s.p - center), length(pn - center))),
            length(vn - vf) / (absoluteTolerance() + relativeTolerance() * max(length(s.v), length(vn)))) /
        real(15);

    if (!finiteVector(pn) || !finiteVector(vn) || !finiteScalar(error))
    {
        failTrace(s);

        return;
    }

    real nextStep = min(
        stepLimit, s.step * clamp(real(0.9) * pow(max(error, real(1e-10)), real(-0.2)), real(0.2), real(2)));

    if (error > real(1))
    {
        s.step = nextStep;

        if (s.step < real(1e-7))
            failTrace(s);
        return;
    }

    vec3 delta = pn - s.p;
    real eventT = real(1.000001), t = real(0);
    int eventKind = 0;
    SurfaceHit hit;

    if (worldHit(s.p, delta, real(1e-7), eventT, true, hit))
    {
        eventT = hit.t;
        eventKind = 1;
    }

    if (sphereRoot(s.p, delta, center, real(1), real(0), eventT, t) &&
        (s.observerMode < 2 || dot(s.p + t * delta - center, delta) < real(0)))
    {
        eventT = t;
        eventKind = 2;
    }

    if (diskHit(s.p, delta, real(1e-7), eventT, t))
    {
        eventT = t;
        eventKind = 4;
    }

    if (integrationMode() == 0 && s.observerMode < 2 && length(pn - center) >= sphereRadius(s.field) &&
        dot(delta, pn - center) > real(0) &&
        sphereRoot(s.p, delta, center, sphereRadius(s.field), real(1e-7), eventT, t))
    {
        eventT = t;
        eventKind = 3;
    }

    vec3 previousV = s.v;
    real segmentLength = length(delta);
    transferAtmosphere(s, safeUnit(delta), min(eventT, real(1)) * segmentLength);

    if (eventKind != 0)
    {
        s.p = s.p + eventT * delta;
        s.v = previousV + eventT * (vn - previousV);
    }
    else
    {
        s.p = pn;
        s.v = vn;
    }

    s.step = nextStep;

    if (eventKind == 1)
        scatterSurface(s, hit);
    else if (eventKind == 2)
        s.status = 3;
    else if (eventKind == 4)
    {
#ifdef __cplusplus
        if (picking)
        {
            pickedBody = blackHole();
            s.status = 1;

            return;
        }
#endif
        if (s.body < 0)
            s.body = blackHole();
        s.radiance += s.throughput * diskRadiance(s.p, -s.v, s.observerLapse);
        s.status = 1;
    }
    else if (integrationMode() == 0 && s.observerMode < 2 &&
             (eventKind == 3 ||
              (length(s.p - center) >= sphereRadius(s.field) && dot(s.p - center, s.v) > real(0))))
    {
        if (s.observerMode == 0)
            s.v = safeUnit(s.v);
        s.p += EPS * safeUnit(s.v);
        s.field = -1;
    }
}

/**
 * @brief Perform one state-machine action without tracing an entire trajectory.
 * Resume integration, enter a field, shade the nearest straight-flight event, or finish
 * on the background. The GPU calls this at most 64 times per dispatch and stores state.
 */
void advanceTrace(INOUT(TraceState) s)
{
    if (s.status != 0)
        return;

    if (!finiteVector(s.p) || !finiteVector(s.v) || !finiteVector(s.throughput))
    {
        failTrace(s);

        return;
    }

    if (s.field >= 0)
    {
        integrateField(s);

        return;
    }

    for (int i = 0; i < sphereCount(); ++i)
    {
        if (s.field != -2 && materialKind(sphereMaterial(i)) == 4 &&
            (integrationMode() != 0 || s.observerMode >= 2 ||
             length(s.p - sphereCenter(i)) < sphereRadius(i)))
        {
            enterField(s, i);

            return;
        }
    }

    SurfaceHit hit;

    bool hitWorld = worldHit(s.p, s.v, EPS, real(1e30), integrationMode() != 0, hit);
    real maximum = hitWorld ? hit.t : real(1e30);
    real diskDistance = real(0);
    bool hitDisk = diskHit(s.p, s.v, EPS, maximum, diskDistance);
    transferAtmosphere(s, safeUnit(s.v), (hitDisk ? diskDistance : maximum) * length(s.v));

    if (hitDisk)
    {
        s.p = s.p + diskDistance * s.v;
#ifdef __cplusplus
        if (picking)
        {
            pickedBody = blackHole();
            s.status = 1;

            return;
        }
#endif
        if (s.body < 0)
            s.body = blackHole();
        s.radiance += s.throughput * diskRadiance(s.p, -s.v, s.observerLapse);
        s.status = 1;

        return;
    }

    if (!hitWorld)
    {
        real t = real(0.5) * (safeUnit(s.v).y + real(1));
        real skyShift = s.observerMode != 0 ? pow(real(1) / s.observerLapse, real(4)) : real(1);
        s.radiance +=
            skyShift * s.throughput * ((real(1) - t) * vec3(1) + t * vec3(real(0.5), real(0.7), real(1)));
        s.status = 1;

        return;
    }

    if (materialKind(sphereMaterial(hit.sphere)) == 4)
    {
        s.p = hit.p;
        enterField(s, hit.sphere);
    }
    else
        scatterSurface(s, hit);
}
