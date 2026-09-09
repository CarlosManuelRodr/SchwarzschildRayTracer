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

        if (sphereRoot(p, v, sphereCenter(i), sphereRadius(i), lo, hi, t))
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

    if (kind == 3)
    {
        s.radiance += s.throughput * color;
        s.status = 1;

        return;
    }

    if (s.depth >= 50)
    {
        s.status = 1;

        return;
    }
    ++s.depth;
    vec3 d = safeUnit(s.v), n = hit.normal;

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
    s.p = hit.p + EPS * s.v;

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

void integrateField(INOUT(TraceState) s)
{
    vec3 center = sphereCenter(s.field);

    if (length(s.p - center) <= real(1))
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

    real nextStep =
        min(maximumStep(),
            s.step * clamp(real(0.9) * pow(max(error, real(1e-10)), real(-0.2)), real(0.2), real(2)));

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

    if (sphereRoot(s.p, delta, center, real(1), real(0), eventT, t))
    {
        eventT = t;
        eventKind = 2;
    }

    // A boundary point moving inward has a zero entry root; only accept the exit.
    if (length(pn - center) >= sphereRadius(s.field) && dot(delta, pn - center) > real(0) &&
        sphereRoot(s.p, delta, center, sphereRadius(s.field), real(1e-7), eventT, t))
    {
        eventT = t;
        eventKind = 3;
    }

    vec3 previousV = s.v;

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
    else if (eventKind == 3 ||
             (length(s.p - center) >= sphereRadius(s.field) && dot(s.p - center, s.v) > real(0)))
    {
        s.v = safeUnit(s.v);
        s.p = s.p + EPS * s.v;
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
        if (materialKind(sphereMaterial(i)) == 4 && length(s.p - sphereCenter(i)) < sphereRadius(i))
        {
            enterField(s, i);

            return;
        }
    }

    SurfaceHit hit;

    if (!worldHit(s.p, s.v, EPS, real(1e30), false, hit))
    {
        real t = real(0.5) * (safeUnit(s.v).y + real(1));
        s.radiance += s.throughput * ((real(1) - t) * vec3(1) + t * vec3(real(0.5), real(0.7), real(1)));
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
