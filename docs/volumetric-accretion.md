# Seeing through the accretion gas

The bright ring around the black hole is now a volume. A ray can pass above it,
skim its surface, or travel through its interior. Its brightness and opacity
depend on how much gas the ray encounters. There is no opaque plane underneath.

This is a model of the **appearance** of hot orbiting gas, not a simulation of
how an accretion flow forms. The thermal emitting region represents hot gas or
plasma; it should not be interpreted as ordinary cold dust surviving at 6000 K.

## Giving the ring a height

Distances are measured in horizon radii. Relative to the black hole, let `z`
be height along the disk normal and `R` be distance from that axis. The gas
occupies the annulus between `innerRadius` and `outerRadius`.

We prescribe a flared vertical scale:

```text
H(R) = scaleHeight * (0.5 + 0.5 * R / outerRadius)
```

The density falls with height as `exp(-z² / (2 H(R)²))`. This is a Gaussian:
there is more gas near the midplane and progressively less above and below it.
Smooth fades remove gas at the radial edges and at `|z| = 3 * scaleHeight`.
This finite boundary lets the renderer skip empty space cheaply.

Three scales of smooth, deterministic 3-D noise modulate the density. Twisting
the noise coordinates with radius gives elongated, sheared clouds. The noise
also varies with height, so these are spatial structures, not a texture pasted
onto a plane. Their pattern is stationary and follows the black hole's position
and orientation. There is no simulated turbulence or time evolution.

## Adding light a little at a time

Imagine dividing the ray's path through the gas into short cells. Within one
cell, approximate its density and temperature as constant. Define:

- `rho`: the dimensionless density at the cell midpoint.
- `k`: `extinction`, the absorption coefficient at unit density.
- `ds`: the cell's length in horizon-radius coordinate units.
- `tau = k * rho * ds`: optical depth, measuring how opaque this cell is.
- `T = exp(-tau)`: the fraction of light transmitted through the cell.

For example, optical depth zero transmits everything. Optical depth one
transmits about 37 percent. This exponential attenuation is the Beer–Lambert law.

The gas also emits. With a thermal source function `S`, the solution for a
homogeneous cell is:

```text
outgoing light = T * incoming light + (1 - T) * S
```

The renderer traces from the camera toward the sources. It keeps `throughput`,
the fraction still transmitted through all the foreground material, and adds
each cell in that order:

```text
radiance   += throughput * (1 - T) * S
throughput *= T
```

This makes thick clouds obscure the background and thinner wisps transmit it.
Gas in front of the black-hole shadow still emits even if the ray is later
captured. An opaque object truncates the segment before gas behind it can
contribute. Once throughput is below a small threshold, tracing can finish.

## Temperature, redshift, and illumination

The radial temperature still follows the existing zero-torque thin-disk profile.
Here it supplies a temperature prescription for a thick volume; it does not
solve the thick flow's thermal balance. Temperature decreases away from the
midplane. The source function is `emissionScale * blackbody(g * temperature)`.

The existing frequency ratio `g` combines gravity, observer motion, and the gas's
prescribed orbital Doppler shift. Shifting the blackbody temperature also changes
its brightness, so multiplying by another `g^4` would count that change twice.
See [observer and horizon physics](observer-and-horizon.md) for those factors.
Extending the equatorial circular speed to gas above the plane is a visual
kinematic approximation, not an off-plane circular geodesic solution.

Earth and Moon receive light sampled from the gas volume. A source sample has
emissivity `k * rho * S`, multiplied by sampled volume and geometric falloff.
Shadow connections include partial gas absorption rather than an opaque disk
shadow. These connections remain straight; the camera rays follow the chosen
gravity mode and can produce lensed images of the volume.

## Controls in the scene definition

`SceneData::disk` retains its radii, temperature, emission scale, and local normal.
Two additional fields control the volume:

| Field | Default | Effect |
| --- | --- | --- |
| `scaleHeight` | 0.10 horizon radii | Gaussian height at the outer edge; the bounding volume extends three times this distance above and below the plane. |
| `extinction` | 8 per horizon radius | Higher values make gas more opaque; zero makes the volume invisible. |

These are scene parameters, not new Inspector controls. Both renderers use the
same density and transfer functions in `assets/shaders/LightingCore.inl`.

## Numerical and physical limits

Near the volume, camera integration steps are limited to about 0.08 horizon
radii in spatial length, independently of the gravitational error estimate.
Each accepted volume segment uses four midpoint cells. Straight flights likewise
process short chunks and preserve their position between dispatches. Rejected
RK4 steps never add emission. Shadow transmission uses 24 midpoint samples.
These are finite quadratures, so very small cloud scales would need finer steps.

Absorption is grey (the same coefficient for RGB), and optical depth uses
coordinate segment length. It is not a full covariant, frequency-dependent
transfer calculation in the moving gas frame. There is no scattering within the
accretion gas, hydrodynamics, magnetic field, or Kerr spin. The atmosphere, solar
corona, and gas are separate transfer approximations; overlapping these media
arbitrarily in the editor is not a jointly integrated participating medium.

Tests cover finite-height intersections, the empty cavity, partial transmission,
translation and rotation invariance, segment refinement, and CPU/GPU agreement
for edge-on and embedded rays in all three integration modes.
