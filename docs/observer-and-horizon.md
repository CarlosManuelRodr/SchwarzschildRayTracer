# Observer velocity and visualization across the event horizon

## Scope and units

This renderer depicts a stationary Schwarzschild spacetime and stationary scene
geometry. Set `c = 1` and the horizon radius `2GM/c² = 1`. Positions below are
relative to the black-hole center. Camera movement is an editing operation:
changing the observer velocity does **not** advance the camera position.

The implementation uses horizon-regular observer initial data and the exact
spatial reduction of null geodesics. It does not need to retain coordinate time
because the scene is stationary. Animated emitters or gravitational collapse
would require tracking emission time and supplying their history.

## A regular frame at the horizon

Write the ingoing Painlevé–Gullstrand metric as

\[
ds^2=-dT^2+|d\mathbf{x}+\mathbf{w}\,dT|^2,
\qquad r=|\mathbf{x}|,\quad \mathbf{n}=\mathbf{x}/r,
\quad \mathbf{w}=\mathbf{n}/\sqrt r.
\]

There is no coordinate singularity at `r = 1`. The orthonormal rain tetrad is

\[
e_{(0)}=(1,-\mathbf w),\qquad e_{(i)}=(0,\hat{\mathbf e}_i).
\]

Its reference observers fall radially from rest at infinity. The spatial axes
are aligned with the scene's X/Y/Z axes. This frame is used **outside and inside**
the horizon, avoiding a change in the meaning of velocity at the crossing.

## User velocity and the physical observer

The panel edits the components of the local relative velocity
`β = v/c`. Each component has a magnitude slider and a negative-direction toggle.
The invariant physical constraint is on the vector norm, not each component
separately:

\[
|\boldsymbol\beta|<1,\qquad
\gamma=(1-|\boldsymbol\beta|^2)^{-1/2},\qquad
u=\gamma(e_{(0)}+\beta^i e_{(i)}).
\]

Thus `u^T = γ` and `u^i = γ(β^i - w^i)`. Inside the horizon,
`u^r = γ(β_r - 1/√r) < 0`: all physical observers move inward, even when a
paused rendering does not update their positions. A zero velocity setting is
a rain observer, **not a hovering observer**. Outside the horizon, instantaneous
hovering can be represented by `β = w`, when its magnitude fits the UI limit.

A massive camera cannot travel at exactly `c`. The UI shows a 0-to-c component
scale but rescales the vector to a maximum combined speed of `0.999c`; the scene
settings validator enforces the same limit. This also avoids unbounded Lorentz
factors in the single-precision shader.

## Backward ray initialization

Let `d` be the unit direction of a pixel toward its apparent source in the
camera's rest frame. The arriving future-directed photon has unit observed
frequency and four-momentum `(1, q)`, where `q = -d`.

Boost it from the camera frame into the rain tetrad:

\[
\omega=\gamma(1+\boldsymbol\beta\cdot\mathbf q),
\]
\[
\mathbf p=\mathbf q+
\left[\gamma+\frac{\gamma^2}{\gamma+1}
(\boldsymbol\beta\cdot\mathbf q)\right]\boldsymbol\beta.
\]

The factor `γ²/(γ+1) = (γ−1)/β²` avoids division by zero at zero velocity.
The null condition is `|p| = ω`. In coordinates the future photon has
`k^T = ω`, `k^i = p^i − w^i ω`. The stored, past-directed affine spatial
tangent and conserved Killing energy are therefore

\[
\boxed{\mathbf V=\mathbf w\,\omega-\mathbf p},\qquad
\boxed{E=-k_T=\omega-\mathbf w\cdot\mathbf p}.
\]

Both expressions remain finite at the future horizon. An inward-arriving radial
photon can consequently be followed backward from inside the horizon to an
exterior emitter. This is reconstruction of an incoming photon's history, not
a future-directed photon escaping the hole.

## Why the spatial integrator can be reused

Schwarzschild and ingoing PG coordinates have the same spatial `r, θ, φ`.
For an affine parameter increasing backward along the ray, define

\[
L^2=|\mathbf x\times\mathbf V|^2.
\]

Spherical symmetry and the null constraint give

\[
\dot r^2=E^2-\frac{L^2}{r^2}+\frac{L^2}{r^3},\qquad
\ddot r=\frac{L^2}{r^3}-\frac{3L^2}{2r^4}.
\]

Subtracting the radial centripetal term yields

\[
\boxed{\ddot{\mathbf x}=-\frac{3L^2}{2r^5}\mathbf x}.
\]

This is the existing RK4 acceleration, now supplied with the correct observer
initial tangent. It is regular at `r = 1` and applies on both sides. A useful
independent invariant for tests is

\[
|\mathbf V|^2-\frac{L^2}{r^3}=E^2.
\]

Normalizing `V` during integration would destroy this relation and is forbidden.
The renderer retains `E` and the observer branch across GPU work batches using
previously unused storage lanes, without increasing the per-ray SSBO size.

## Horizon rules and automatic selection

* An exterior camera retains the selected fixed-radius, full-scene, or adaptive
  geometry mode. Its initial rays and frequency shifts now include its velocity.
* A camera at `r <= 1` automatically bypasses finite/adaptive cutoffs and uses
  full spatial integration with the horizon-crossing initial data.
* An interior observer's positive-energy past ray may cross `r = 1` outward.
  Crossing inward again is terminated as a dark past boundary.
* Exterior backward rays entering the horizon retain the existing dark-boundary
  rule; the scene does not supply radiation from a white-hole region.
* `E <= 0` cannot connect to a future-directed photon at any stationary exterior
  source. Those directions are dark in this exterior-source model.
* No observer is defined at the curvature singularity. Initial radii at or below
  `1e-4` are terminated. Integration failures and exhausted budgets remain
  diagnostics, rather than being mistaken for valid visible rays.

The model does not reconstruct a collapsing star's history or light emitted by
matter inside the horizon. Such light could fill some directions rendered dark
here. Interior visualization is therefore not a claim to simulate a complete
astrophysical collapse spacetime.

## Frequency shifts and scattering

The measured photon frequency for an observer is `ω_observer = -k·u`. Ray
initialization sets the camera measurement to one. A stationary exterior emitter
at radius `r_e` measures `ω_e = E/α_e`, where `α_e = √(1−1/r_e)`. Hence

\[
g=\frac{\omega_{camera}}{\omega_e}=\frac{\alpha_e}{E}.
\]

This replaces the old static-camera ratio `α_e/α_camera`. The velocity boost is
already included in `E`; adding a separate camera Doppler factor would count it
twice. For the orbiting disk, the existing local orbital Doppler factor multiplies
`α_e/E`. Thermal emission evaluates Planck radiance at `g*T`. RGB-only emission
retains the existing bolometric `g⁴` approximation. Disabling redshift disables
these radiometric factors but leaves geometrical aberration enabled.

For local material directions at an exterior surface, the radial component of
the coordinate tangent is divided by `α_e` before normalization. After elastic
stationary scattering, a unit local direction `d_s` is converted back to an
affine tangent with the same energy:

\[
\mathbf V_{new}=\frac{E}{\alpha_e}
\left[\mathbf d_s+(\alpha_e-1)(\mathbf d_s\cdot\mathbf n)\mathbf n\right].
\]

The existing surface normals, direct source connections, and atmosphere retain
their approximate spatial treatment. Stationary material rest frames are not
defined inside the horizon; this feature is intended for interior cameras
viewing the existing **exterior** scene.

## Accuracy and compatibility

The exterior fixed-radius mode remains a deliberate approximation: straight
segments outside its sphere do not conserve the full metric's ray invariants.
Use full-scene integration for physically consistent crossing comparisons and
for the smoothest exterior-to-interior transition. Adaptive cutoff adds its
documented truncation error. The frame itself is continuous in all modes, but
switching a truncated geometry to full integration can change the image.

At zero panel velocity the exterior image intentionally differs from the former
hovering-camera image. Legacy numerical regression fixtures explicitly set
`useObserverFrame = false`; normal application rendering enables it.

Tests cover the null constraint and angular momentum through a horizon crossing,
analytic radial frequency shifts at radii below/on/above one, boosts up to
`0.999c`, positive/negative-energy classification, automatic interior activation,
CPU/GPU agreement, and a non-black interior image of the exterior scene.
The interior escape cone is checked independently against `L²/E² = 27/4`,
the Schwarzschild photon-sphere barrier. Bright, near-critical paths remain
sensitive to single/double precision and sample count; the image comparison
uses the normal 30-sample target, rather than demanding exact pixel agreement.

## References

The equations above are derived for this implementation from the PG metric,
Lorentz transformations, and the Schwarzschild null first integrals.

* [Hamilton and Polhemus, *The edge of locality: visualizing a black hole from the inside*](https://arxiv.org/abs/0903.4717)
  provides the physical context for interior-observer images.
* [Kanai, Siino, and Hosoya, *Gravitational collapse in Painlevé–Gullstrand coordinates*](https://arxiv.org/abs/1008.0470)
  discusses a horizon-regular description based on freely falling observers.
* [Davelaar et al., *Observing supermassive black holes in virtual reality*](https://doi.org/10.1186/s40668-018-0023-7)
  discusses relativistic observer frames and horizon-penetrating visualization.
