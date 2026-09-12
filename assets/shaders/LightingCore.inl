/**
 * @file
 * @brief Shared linear-light emission, surface shading, and volume transfer.
 * Included after TraceState, intersections, textures, and RNG helpers in TraceCore.inl.
 * Coordinates use horizon radii, temperature uses kelvin, and colors are linear RGB.
 * Camera paths follow the selected geodesic mode; direct-light and shadow connections
 * are straight-segment approximations. Corona and atmosphere are illustrative models.
 */
/**
 * @brief Return sqrt(1 - 1/r), the exterior static-clock lapse, or 1 with redshift disabled.
 * A small floor prevents division by zero; it does not define a physical hovering frame inside.
 */
real lapseAt(vec3 position)
{
    int hole = blackHole();

    if (hole < 0 || !redshiftEnabled())
        return real(1);

    real radius = length(position - sphereCenter(hole));

    return sqrt(max(real(1e-6), real(1) - real(1) / max(radius, real(1))));
}

/**
 * @brief Interpolate the host's logarithmic Planck-spectrum-to-RGB table in kelvin.
 * Below 500 K return black; clamp the upper temperature to the table range.
 * The table includes relative brightness, so do not multiply thermal emission by g^4 again.
 */
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

/**
 * @brief Intersect a ray with the planar annulus centered on the hole.
 * minimum/maximum are exclusive ray-parameter bounds; direction need not be unit length.
 */
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

/**
 * @brief Evaluate the zero-torque thin-disk temperature profile outside the inner edge.
 * Flux is proportional to (r_in/r)^3 * (1 - sqrt(r_in/r)); T is its fourth root.
 * The normalization makes diskPeakTemperature the maximum of this profile.
 */
real diskTemperature(real radius)
{
    real ratio = diskInner() / radius;
    // Zero-torque thin-disk flux peaks at r = (49/36)*r_in.
    real flux = pow(ratio, real(3)) * max(real(0), real(1) - sqrt(ratio));

    return diskPeakTemperature() * pow(flux / real(0.0566527795), real(0.25));
}

/**
 * @brief Return observed/emitted frequency g for orbiting disk gas.
 * Convert the future-directed photon tangent to a local static direction and combine
 * gravitational lapse with special-relativistic Doppler shift for circular orbital speed.
 * observerLapse is the camera ray's conserved-energy normalization, not always a clock lapse.
 */
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

/**
 * @brief Deterministic smooth value noise used to illustrate stationary gas structure.
 */
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

/**
 * @brief Shade disk emission using its temperature, stationary structure, and frequency shift.
 * photonDirection points from emitter toward observer. Evaluating blackbody(g*T) shifts
 * the Planck spectrum and its brightness together; an extra g^4 factor would double-count it.
 */
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

/**
 * @brief Convert an affine spatial tangent to a unit direction in a local static frame.
 * Only exterior static frames are physical; legacy fixtures simply normalize the input.
 */
vec3 staticDirection(vec3 position, vec3 direction)
{
    if (!observerFrameEnabled() || blackHole() < 0)
        return safeUnit(direction);

    vec3 offset = position - sphereCenter(blackHole());
    real radius = length(offset);
    vec3 radial = offset / max(radius, real(1e-8));
    real lapse = sqrt(max(real(1e-8), real(1) - real(1) / max(radius, real(1))));

    return safeUnit(direction + (real(1) / lapse - real(1)) * dot(direction, radial) * radial);
}

/**
 * @brief Evaluate a diffuse light's emission toward the observer in linear RGB.
 * Thermal lights shift temperature and apply limb darkening; ordinary RGB emission uses
 * an approximate bolometric g^4 factor. Solar texture luminance modulates structure.
 */
vec3 surfaceRadiance(SurfaceHit hit, vec3 photonDirection, real observerLapse)
{
    int material = sphereMaterial(hit.sphere);
    vec3 emission = materialEmission(material);
    real shift = lapseAt(hit.p) / observerLapse;

    if (emission.x <= real(0))
        return textureValue(materialTexture(material), hit.u, hit.v, hit.p) * pow(shift, real(4));

    real cosine = max(real(0), dot(hit.normal, staticDirection(hit.p, photonDirection)));
    real limb = (real(1) - emission.z * (real(1) - cosine)) / (real(1) - emission.z / real(3));
    vec3 texture = textureValue(materialTexture(material), hit.u, hit.v, hit.p);
    // The supplied solar image is orange false color, not an emission spectrum.
    // Preserve its surface structure without tinting the entire planetary scene.
    real structure = dot(texture, vec3(real(0.2126), real(0.7152), real(0.0722)));

    if (textureKind(materialTexture(material)) == 2)
        structure /= real(0.4);
    return emission.y * limb * structure * blackbody(emission.x * shift);
}

/**
 * @brief Choose a stable unit tangent perpendicular to a unit normal.
 */
vec3 tangentAt(vec3 normal)
{
    vec3 axis = abs(normal.y) < real(0.9) ? vec3(0, 1, 0) : vec3(1, 0, 0);

    return safeUnit(cross(axis, normal));
}

/**
 * @brief Test a straight shadow connection, excluding the source and environment.
 * The black hole occludes with horizon radius 1; its larger cutoff sphere is not opaque.
 * direction must be unit length and distance is the connection length.
 */
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

/**
 * @brief Evaluate reflected radiance per incident irradiance, excluding the cosine factor.
 * Uses a diffuse term plus a GGX microfacet specular term for land/ocean. Negative ocean
 * selects Lambertian lunar regolith. All direction and normal inputs must be unit vectors.
 */
vec3 earthBrdf(vec3 albedo, vec3 normal, vec3 view, vec3 light, real ocean)
{
    real nv = max(dot(normal, view), real(0));
    real nl = max(dot(normal, light), real(0));

    if (nv <= real(0) || nl <= real(0))
        return vec3(0);

    if (ocean < real(0))
        return albedo / PI; // Rough lunar regolith.
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

/**
 * @brief Estimate RGB transmission through the Earth shell along a finite unit-direction ray.
 */
vec3 atmosphereTransmission(vec3 origin, vec3 direction, real maximum);

/**
 * @brief Shade Earth or Moon with sampled finite lights and disk illumination.
 * Earth combines daytime albedo, tangent-space normals, clouds, ocean mask, and city
 * lights faded by solar elevation. Four samples per source estimate direct lighting.
 * Uses straight shadow connections and atmospheric extinction, not a full GR light solver.
 */
vec3 illuminateEarth(INOUT(TraceState) state, SurfaceHit hit, vec3 albedo)
{
    vec3 result = vec3(0);
    vec3 view = -staticDirection(hit.p, state.v);
    vec3 origin = hit.p + EPS * hit.normal;
    int material = sphereMaterial(hit.sphere);
    bool earth = materialKind(material) == 5;
    vec3 normal = hit.normal;
    real cloud = real(0);
    real ocean =
        earth && albedo.z > real(1.15) * albedo.x && albedo.z > real(0.95) * albedo.y ? real(1) : real(0);
    int layer = materialLayer(material, 1);

    if (earth && layer >= 0)
        cloud = clamp(textureValue(layer, hit.u, hit.v, hit.p).x, real(0), real(1));
    layer = materialLayer(material, 3);

    if (earth && layer >= 0)
        ocean = textureValue(layer, hit.u, hit.v, hit.p).x;
    ocean *= real(1) - cloud;

    if (!earth)
        ocean = real(-1);
    albedo = (real(1) - cloud) * albedo + cloud * vec3(real(0.8));
    layer = materialLayer(material, 2);

    if (earth && layer >= 0)
    {
        vec3 mapped = real(2) * textureValue(layer, hit.u, hit.v, hit.p) - vec3(1);
        vec3 tangent = safeUnit(vec3(normal.z, 0, -normal.x));

        if (dot(tangent, tangent) < real(0.5))
            tangent = vec3(0, 0, -1);
        vec3 bitangent = cross(normal, tangent);
        vec3 perturbed = safeUnit(mapped.x * tangent + mapped.y * bitangent + mapped.z * normal);
        normal = safeUnit((real(1) - cloud) * perturbed + cloud * normal);
    }

    // Solar elevation controls city lights, with a smooth twilight transition.
    real solarCosine = real(-1);

    for (int i = 0; i < sphereCount(); ++i)
        if (materialKind(sphereMaterial(i)) == 3)
            solarCosine = max(solarCosine, dot(hit.normal, safeUnit(sphereCenter(i) - hit.p)));
    real night = clamp(-solarCosine / real(0.12), real(0), real(1));
    night = night * night * (real(3) - real(2) * night);
    layer = materialLayer(material, 0);

    if (earth && layer >= 0)
        result += real(0.6) * night * (real(1) - real(0.85) * cloud) *
                  textureValue(layer, hit.u, hit.v, hit.p) *
                  pow(lapseAt(hit.p) / state.observerLapse, real(4));


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
                      atmosphereTransmission(origin, light, t) *
                      earthBrdf(albedo, normal, view, light, ocean) *
                      (max(dot(normal, light), real(0)) * solidAngle / real(4));
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
            real cosine = max(dot(normal, light), real(0));

            if (cosine <= real(0) || !visibleLight(origin, light, distance, -1))
                continue;

            real geometry = abs(dot(diskNormal(), light)) * cosine / (distance * distance);
            result += diskRadiance(point, -light, state.observerLapse) *
                      atmosphereTransmission(origin, light, distance) *
                      earthBrdf(albedo, normal, view, light, ocean) * (area * geometry / real(4));
        }
    }

    return result;
}

/**
 * @brief Return RGB extinction per horizon-radius length for an exponential atmosphere.
 * altitude is height above Earth's surface, clamped to zero below the surface.
 */
vec3 airExtinction(real altitude)
{
    real density = exp(-max(real(0), altitude) / (atmosphereHeight() / real(6)));

    return density * vec3(real(0.10), real(0.23), real(0.55)) / atmosphereHeight();
}

/**
 * @brief Apply Beer-Lambert transmission exp(-opticalDepth) independently to each RGB channel.
 */
vec3 attenuationFor(vec3 opticalDepth)
{
    return vec3(exp(-opticalDepth.x), exp(-opticalDepth.y), exp(-opticalDepth.z));
}

/**
 * @brief Integrate shell extinction with eight midpoint samples up to maximum distance.
 * direction must be unit length; return unit transmission if no planet or shell is crossed.
 */
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

// An optically thin, illustrative solar corona. Integrating over the actual ray
// segment keeps occultation and gravitational lensing consistent with geometry.
/**
 * @brief Add optically thin solar glow along the actual accepted camera-ray segment.
 * direction is unit length and maximum is the segment length. Sixteen midpoint samples
 * resolve a fading shell extending to four solar radii, clipped by foreground intersections.
 */
void transferCorona(INOUT(TraceState) state, vec3 direction, real maximum)
{
    for (int i = 0; i < sphereCount(); ++i)
    {
        if (materialKind(sphereMaterial(i)) != 3 || materialEmission(sphereMaterial(i)).x <= real(0))
            continue;

        real radius = sphereRadius(i);
        real outer = real(4) * radius;
        vec3 relative = state.p - sphereCenter(i);
        real b = dot(relative, direction);
        real disc = b * b - dot(relative, relative) + outer * outer;

        if (disc <= real(0))
            continue;

        real start = max(real(0), -b - sqrt(disc));
        real end = min(maximum, -b + sqrt(disc));

        if (end <= start)
            continue;

        real step = (end - start) / real(16);
        vec3 glow = vec3(0);

        for (int j = 0; j < 16; ++j)
        {
            vec3 point = state.p + (start + (real(j) + real(0.5)) * step) * direction;
            vec3 offset = point - sphereCenter(i);
            real ratio = length(offset) / radius;

            if (ratio < real(1))
                continue;

            real fade = clamp((real(4) - ratio) / real(2), real(0), real(1));
            real angle = atan(offset.z, offset.x);
            real streamers = real(0.75) + real(0.25) * pow(abs(cos(real(7) * angle)), real(8));
            real shift = lapseAt(point) / state.observerLapse;
            glow += (step / radius) * real(0.5) * pow(ratio, real(-6)) * fade * fade * streamers *
                    blackbody(real(6500) * shift);
        }

        state.radiance += state.throughput * glow;
    }
}

// Single-scattering Rayleigh shell. View and solar optical depths use the same
// exponential density profile; the planet itself blocks the night-side source.
/**
 * @brief Add corona/atmospheric radiance and attenuate throughput along one accepted segment.
 * The atmosphere uses single Rayleigh scattering and exponential extinction; light
 * connections are straight. direction is unit length and maximum is the segment length.
 * CPU body picking bypasses volume lighting entirely.
 */
void transferAtmosphere(INOUT(TraceState) state, vec3 direction, real maximum)
{
#ifdef __cplusplus
    if (picking)
        return;
#endif
    transferCorona(state, direction, maximum);
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
