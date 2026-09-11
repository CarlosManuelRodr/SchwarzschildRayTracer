// Compiled as GLSL, and included by the double-precision C++ reference.
// The host supplies scene accessors, scalar/vector aliases, and INOUT/OUT.
const real PI = real(3.14159265358979323846);
const real EPS = real(0.0001);

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
    int field;
    int attempts;
    int depth;
    int status;
    uint rng;
};

struct SurfaceHit
{
    real t;
    vec3 p;
    vec3 normal;
    real u;
    real v;
    int sphere;
};

vec3 safeUnit(vec3 v)
{
    real n = length(v);

    return n > real(1e-20) ? v / n : vec3(0);
}

uint hashBits(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;

    return x ^ (x >> 16);
}

real randomValue(INOUT(uint) state)
{
    state = 1664525u * state + 1013904223u;

    return real(state >> 8) * real(1.0 / 16777216.0);
}

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

bool finiteVector(vec3 v)
{
    return finiteScalar(v.x) && finiteScalar(v.y) && finiteScalar(v.z);
}

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

void initTrace(OUT(TraceState) s, vec3 p, vec3 v, uint seed)
{
    s.p = p;
    s.v = safeUnit(v);
    s.throughput = vec3(1);
    s.radiance = vec3(0);
    s.h2 = real(0);
    s.step = maximumStep();
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

void failTrace(INOUT(TraceState) s)
{
    s.radiance = vec3(0);
    s.status = 2;
}

void scatterSurface(INOUT(TraceState) s, SurfaceHit hit)
{
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

vec3 acceleration(vec3 p, vec3 center, real h2)
{
    vec3 r = p - center;
    real r2 = max(dot(r, r), real(1e-20));

    return -real(1.5) * h2 * r / (r2 * r2 * sqrt(r2));
}

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
