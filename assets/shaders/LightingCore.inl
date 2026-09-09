// Shared by the CPU reference and compute shader. Distances are in horizon radii.
real lapseAt(vec3 position)
{
    int hole = blackHole();

    if (hole < 0 || !redshiftEnabled())
        return real(1);

    real radius = length(position - sphereCenter(hole));
    return sqrt(max(real(1e-6), real(1) - real(1) / max(radius, real(1))));
}

vec3 blackbody(real temperature)
{
    if (temperature < real(500))
        return vec3(0);

    real coordinate =
        clamp(log(temperature / real(500)) * real(2048.0 / 11.51292546497), real(0), real(2048));
    int lower = min(int(coordinate), 2047);
    real weight = coordinate - real(lower);

    return (real(1) - weight) * thermalTexel(lower) + weight * thermalTexel(lower + 1);
}

bool diskHit(vec3 origin, vec3 direction, real minimum, real maximum, OUT(real) distance)
{
    if (!diskEnabled() || blackHole() < 0)
        return false;

    vec3 relative = origin - sphereCenter(blackHole());
    real denominator = dot(direction, diskNormal());

    if (abs(denominator) < real(1e-12))
        return false;

    distance = -dot(relative, diskNormal()) / denominator;
    real radius = length(relative + distance * direction);

    return distance > minimum && distance < maximum && radius >= diskInner() && radius <= diskOuter();
}

real diskTemperature(real radius)
{
    real ratio = diskInner() / radius;
    // Zero-torque thin-disk flux peaks at r = (49/36)*r_in.
    real flux = pow(ratio, real(3)) * max(real(0), real(1) - sqrt(ratio));
    return diskPeakTemperature() * pow(flux / real(0.0566527795), real(0.25));
}

real diskFrequencyShift(vec3 position, vec3 photonDirection, real observerLapse)
{
    if (!redshiftEnabled())
        return real(1);

    vec3 relative = position - sphereCenter(blackHole());
    real radius = length(relative);
    vec3 radial = relative / radius;
    real localLapse = lapseAt(position);

    // Express the photon direction in the static observer's orthonormal frame.
    vec3 localPhoton =
        safeUnit(photonDirection + radial * dot(photonDirection, radial) * (real(1) / localLapse - real(1)));
    vec3 orbitalDirection = safeUnit(cross(diskNormal(), radial));
    real speed = sqrt(real(1) / (real(2) * (radius - real(1))));
    real doppler = sqrt(real(1) - speed * speed) / (real(1) - speed * dot(orbitalDirection, localPhoton));

    return localLapse * doppler / observerLapse;
}

real emissionNoise(vec3 position)
{
    vec3 cell = vec3(floor(position.x), floor(position.y), floor(position.z));
    vec3 fraction = position - cell;
    vec3 weight = fraction * fraction * (vec3(3) - real(2) * fraction);
    real result = real(0);

    for (int z = 0; z < 2; ++z)
    {
        for (int y = 0; y < 2; ++y)
        {
            for (int x = 0; x < 2; ++x)
            {
                uint key = uint(int(cell.x) + x) * 73856093u ^ uint(int(cell.y) + y) * 19349663u ^
                           uint(int(cell.z) + z) * 83492791u;
                real value = real(hashBits(key) >> 8) / real(16777216);
                real wx = x == 0 ? real(1) - weight.x : weight.x;
                real wy = y == 0 ? real(1) - weight.y : weight.y;
                real wz = z == 0 ? real(1) - weight.z : weight.z;
                result += value * wx * wy * wz;
            }
        }
    }

    return result;
}

vec3 diskRadiance(vec3 position, vec3 photonDirection, real observerLapse)
{
    vec3 relative = position - sphereCenter(blackHole());
    real radius = length(relative);
    real shift = diskFrequencyShift(position, safeUnit(photonDirection), observerLapse);

    // Bounded, stationary emissivity structure illustrates turbulent gas. This
    // is a visual perturbation of the thin-disk flux, not a fluid simulation.
    vec3 tangent = safeUnit(cross(diskNormal(), vec3(1, 0, 0)));
    if (dot(tangent, tangent) < real(0.5))
        tangent = safeUnit(cross(diskNormal(), vec3(0, 0, 1)));
    vec3 bitangent = cross(diskNormal(), tangent);
    real angle = atan(dot(relative, bitangent), dot(relative, tangent));
    real shear = real(8) * log(radius / diskInner());
    vec3 coordinates = vec3(real(18) * radius, real(4) * cos(angle + shear), real(4) * sin(angle + shear));
    real structure = real(0.65) * emissionNoise(coordinates) +
                     real(0.25) * emissionNoise(real(2.1) * coordinates) +
                     real(0.1) * emissionNoise(real(4.3) * coordinates);
    real temperatureVariation = pow(real(0.5) + structure, real(0.25));

    // B_nu(g*T) = g^3 B_(nu/g)(T); do not multiply by g^4 again.
    return diskScale() * blackbody(diskTemperature(radius) * temperatureVariation * shift);
}

vec3 surfaceRadiance(SurfaceHit hit, vec3 photonDirection, real observerLapse)
{
    int material = sphereMaterial(hit.sphere);
    vec3 emission = materialEmission(material);
    real shift = lapseAt(hit.p) / observerLapse;

    if (emission.x <= real(0))
        return textureValue(materialTexture(material), hit.u, hit.v, hit.p) * pow(shift, real(4));

    real cosine = max(real(0), dot(hit.normal, safeUnit(photonDirection)));
    real limb = (real(1) - emission.z * (real(1) - cosine)) / (real(1) - emission.z / real(3));
    return emission.y * limb * blackbody(emission.x * shift);
}

vec3 tangentAt(vec3 normal)
{
    vec3 axis = abs(normal.y) < real(0.9) ? vec3(0, 1, 0) : vec3(1, 0, 0);
    return safeUnit(cross(axis, normal));
}

bool visibleLight(vec3 origin, vec3 direction, real distance, int light)
{
    real t = real(0);

    for (int i = 0; i < sphereCount(); ++i)
    {
        if (i == light || materialKind(sphereMaterial(i)) == 6)
            continue;

        real radius = materialKind(sphereMaterial(i)) == 4 ? real(1) : sphereRadius(i);
        if (sphereRoot(origin, direction, sphereCenter(i), radius, EPS, distance - EPS, t))
            return false;
    }

    return !diskHit(origin, direction, EPS, distance - EPS, t);
}

vec3 earthBrdf(vec3 albedo, vec3 normal, vec3 view, vec3 light)
{
    real nv = max(dot(normal, view), real(0));
    real nl = max(dot(normal, light), real(0));

    if (nv <= real(0) || nl <= real(0))
        return vec3(0);

    // The supplied color map has no ocean mask; classify blue-dominant texels.
    real ocean = albedo.z > real(1.15) * albedo.x && albedo.z > real(0.95) * albedo.y ? real(1) : real(0);
    real roughness = ocean > real(0) ? real(0.12) : real(0.65);
    real f0 = ocean > real(0) ? real(0.02) : real(0.04);
    vec3 halfVector = safeUnit(view + light);
    real nh = max(dot(normal, halfVector), real(0));
    real vh = max(dot(view, halfVector), real(0));
    real alpha2 = pow(roughness, real(4));
    real denominator = nh * nh * (alpha2 - real(1)) + real(1);
    real distribution = alpha2 / (PI * denominator * denominator);
    real k = pow(roughness + real(1), real(2)) / real(8);
    real geometry = (nv / (nv * (real(1) - k) + k)) * (nl / (nl * (real(1) - k) + k));
    real fresnel = f0 + (real(1) - f0) * pow(real(1) - vh, real(5));

    return (real(1) - fresnel) * albedo / PI +
           vec3(distribution * geometry * fresnel / max(real(4) * nv * nl, real(1e-8)));
}

vec3 atmosphereTransmission(vec3 origin, vec3 direction, real maximum);

vec3 illuminateEarth(INOUT(TraceState) state, SurfaceHit hit, vec3 albedo)
{
    vec3 result = vec3(0);
    vec3 view = -safeUnit(state.v);
    vec3 origin = hit.p + EPS * hit.normal;

    // Sample the solid angle of each spherical source: finite area, soft shadows,
    // and geometric falloff without relying on accidental BSDF/light hits.
    for (int i = 0; i < sphereCount(); ++i)
    {
        if (materialKind(sphereMaterial(i)) != 3)
            continue;

        vec3 offset = sphereCenter(i) - origin;
        real distance2 = dot(offset, offset);
        real radius = sphereRadius(i);
        real cosineMaximum = sqrt(max(real(0), real(1) - radius * radius / distance2));
        vec3 axis = safeUnit(offset);
        vec3 tangent = tangentAt(axis);
        vec3 bitangent = cross(axis, tangent);

        for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex)
        {
            real u = randomValue(state.rng);
            real phi = real(2) * PI * randomValue(state.rng);
            real cosine = real(1) - u * (real(1) - cosineMaximum);
            real sine = sqrt(max(real(0), real(1) - cosine * cosine));
            vec3 light = cosine * axis + sine * (cos(phi) * tangent + sin(phi) * bitangent);
            real t = real(0);

            if (dot(hit.normal, light) <= real(0) ||
                !sphereRoot(origin, light, sphereCenter(i), radius, EPS, real(1e30), t) ||
                !visibleLight(origin, light, t, i))
                continue;

            SurfaceHit source = makeHit(i, origin, light, t);
            real solidAngle = real(2) * PI * (real(1) - cosineMaximum);
            result += surfaceRadiance(source, -light, state.observerLapse) *
                      atmosphereTransmission(origin, light, t) * earthBrdf(albedo, hit.normal, view, light) *
                      (dot(hit.normal, light) * solidAngle / real(4));
        }
    }

    if (diskEnabled())
    {
        vec3 tangent = tangentAt(diskNormal());
        vec3 bitangent = cross(diskNormal(), tangent);
        real area = PI * (diskOuter() * diskOuter() - diskInner() * diskInner());

        for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex)
        {
            real u = randomValue(state.rng);
            real phi = real(2) * PI * randomValue(state.rng);
            real radius = sqrt(diskInner() * diskInner() + u * area / PI);
            vec3 point = sphereCenter(blackHole()) + radius * (cos(phi) * tangent + sin(phi) * bitangent);
            vec3 offset = point - origin;
            real distance = length(offset);
            vec3 light = offset / distance;
            real cosine = max(dot(hit.normal, light), real(0));

            if (cosine <= real(0) || !visibleLight(origin, light, distance, -1))
                continue;

            real geometry = abs(dot(diskNormal(), light)) * cosine / (distance * distance);
            result += diskRadiance(point, -light, state.observerLapse) *
                      atmosphereTransmission(origin, light, distance) *
                      earthBrdf(albedo, hit.normal, view, light) * (area * geometry / real(4));
        }
    }

    return result;
}

vec3 airExtinction(real altitude)
{
    real density = exp(-max(real(0), altitude) / (atmosphereHeight() / real(6)));
    return density * vec3(real(0.10), real(0.23), real(0.55)) / atmosphereHeight();
}

vec3 attenuationFor(vec3 opticalDepth)
{
    return vec3(exp(-opticalDepth.x), exp(-opticalDepth.y), exp(-opticalDepth.z));
}

vec3 atmosphereTransmission(vec3 origin, vec3 direction, real maximum)
{
    int earth = planet();
    if (earth < 0)
        return vec3(1);

    vec3 center = sphereCenter(earth);
    real radius = sphereRadius(earth);
    real outer = radius + atmosphereHeight();
    vec3 relative = origin - center;
    real b = dot(relative, direction);
    real discriminant = b * b - dot(relative, relative) + outer * outer;

    if (discriminant <= real(0))
        return vec3(1);

    real start = max(real(0), -b - sqrt(discriminant));
    real end = min(maximum, -b + sqrt(discriminant));
    real step = max(real(0), end - start) / real(8);
    vec3 opticalDepth = vec3(0);

    for (int i = 0; i < 8; ++i)
    {
        vec3 point = origin + (start + (real(i) + real(0.5)) * step) * direction;
        opticalDepth += step * airExtinction(length(point - center) - radius);
    }

    return attenuationFor(opticalDepth);
}

// Single-scattering Rayleigh shell. View and solar optical depths use the same
// exponential density profile; the planet itself blocks the night-side source.
void transferAtmosphere(INOUT(TraceState) state, vec3 direction, real maximum)
{
    int earth = planet();
    if (earth < 0 || maximum <= real(0))
        return;

    vec3 center = sphereCenter(earth);
    real radius = sphereRadius(earth);
    real outer = radius + atmosphereHeight();
    vec3 relative = state.p - center;
    real b = dot(relative, direction);
    real discriminant = b * b - dot(relative, relative) + outer * outer;

    if (discriminant <= real(0))
        return;

    real start = max(real(0), -b - sqrt(discriminant));
    real end = min(maximum, -b + sqrt(discriminant));
    if (end <= start)
        return;

    real step = (end - start) / real(8);
    vec3 transmittance = vec3(1);
    vec3 scattering = vec3(0);

    for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex)
    {
        vec3 point = state.p + (start + (real(sampleIndex) + real(0.5)) * step) * direction;
        vec3 extinction = airExtinction(length(point - center) - radius);
        vec3 localTransmission = attenuationFor(step * extinction);

        for (int i = 0; i < sphereCount(); ++i)
        {
            if (materialKind(sphereMaterial(i)) != 3)
                continue;

            vec3 light = safeUnit(sphereCenter(i) - point);
            real t = real(0);
            real distance = length(sphereCenter(i) - point);

            if (!visibleLight(point, light, distance - sphereRadius(i), i))
                continue;

            sphereRoot(point, light, center, outer, real(0), real(1e30), t);
            vec3 solarDepth = vec3(0);
            for (int j = 0; j < 4; ++j)
            {
                vec3 solarPoint = point + light * (real(j) + real(0.5)) * t / real(4);
                solarDepth += (t / real(4)) * airExtinction(length(solarPoint - center) - radius);
            }

            SurfaceHit source = makeHit(i, sphereCenter(i) - sphereRadius(i) * light, light, real(0));
            real cosine = dot(direction, light);
            real phase = real(3) * (real(1) + cosine * cosine) / (real(16) * PI);
            real solidAngle = PI * sphereRadius(i) * sphereRadius(i) / (distance * distance);
            real meanLimb = real(1) - materialEmission(sphereMaterial(i)).z / real(3);
            scattering += transmittance * (vec3(1) - localTransmission) * attenuationFor(solarDepth) *
                          surfaceRadiance(source, -light, state.observerLapse) *
                          (phase * solidAngle * meanLimb);
        }

        transmittance *= localTransmission;
    }

    state.radiance += state.throughput * scattering;
    state.throughput *= transmittance;
}
