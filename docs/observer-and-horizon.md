# What does a camera see inside a black hole?

A black hole does not let light escape from inside its event horizon. Does that
mean a camera inside must see nothing?

No. Light from outside can still fall in and reach the camera. The interesting
question is **which incoming light reaches each pixel, and what the camera
measures when it arrives**. That question is the starting point of this renderer.

This guide is for physics undergraduates meeting general relativity (GR) for the
first time. You will need vectors, derivatives, and a little mechanics. We will
introduce the relativity vocabulary as we use it. You do not need tensor calculus
or experience solving Einstein's field equations.

For a first reading, follow sections 1–5, then try the experiments in section 10.
Sections 6–9 connect the physical picture to the code. Section 7 derives the ray
equation rather than just quoting it.

## 1. A camera is more than a position

Imagine two people passing each other: one stands still, the other travels very
fast. They can receive light from the same star at the same location, but disagree
about its frequency and apparent direction.

The frequency change is a **Doppler shift**. A change of apparent direction due
to relative motion is **aberration**. Both already exist in special relativity,
without a black hole.

An **observer** is a local measuring system: a clock, three spatial axes, and a
state of motion. Its position tells us *where* it measures light; its velocity
helps tell us *what it measures*.

The **Physical state** panel lets you choose a reference observer:

| Selection | What zero velocity in the panel means |
| --- | --- |
| **Freely falling** | Falling radially from rest very far away, without rockets. This is the current default. |
| **Hovering** | Staying at fixed spatial coordinates relative to the hole. This requires acceleration and is possible only outside the horizon. |

The velocity controls describe motion **relative to the selected reference
observer**. Zero in Freely falling mode does not mean stationary relative to the
hole. Nonzero velocity in Hovering mode means a moving camera measured relative
to the local hovering frame; the camera itself is then not hovering.

The controls follow your view: right/left, up/down, and forward/backward.
Rotating the view rotates those directions. They are not fixed world XYZ axes.

Changing velocity changes the image, but does not automatically move the camera.
Positioning the editor camera chooses the location of a measurement; setting its
velocity chooses who makes it. A physical observer would move between measurements.
The editor can pause at a chosen location for inspection.

## 2. Our black hole and our units

We use a **Schwarzschild black hole**: the ideal spacetime outside a spherical,
nonrotating, uncharged source, continued through the black-hole horizon. The
background is fixed. The illustrative Sun, Earth, Moon, and disk do not create
their own additional spacetime curvature.

The **event horizon** is the boundary beyond which a future-directed light
signal cannot reach the distant exterior. Its radius is

\[
r_s=\frac{2GM}{c^2},
\]

where \(G\) is Newton's gravitational constant, \(M\) is the black-hole mass,
and \(c\) is the locally measured speed of light in vacuum.

We measure lengths in units of \(r_s\), and times in units of \(r_s/c\).
In these units, \(c=1\) and the horizon radius is 1. This removes constants
from the equations; it does not change the physics.

Throughout this guide:

\[
\mathbf x=\mathbf x_{\rm world}-\mathbf x_{\rm hole},\qquad
r=|\mathbf x|,\qquad \mathbf n=\frac{\mathbf x}{r}.
\]

Here \(\mathbf x\) is position relative to the black-hole center, \(r\) is its
radial coordinate, and \(\mathbf n\) is the outward unit vector. Moving the hole
changes the subtraction, not the equations.

A sphere of coordinate radius \(r\) has area \(4\pi r^2\). That makes \(r\)
an **areal radius**. It is not necessarily a ruler distance measured on every
observer's choice of simultaneous slice through spacetime.

Bold symbols are three-component vectors. A dot product is written
\(\mathbf a\cdot\mathbf b\), a cross product \(\mathbf a\times\mathbf b\), and
\(|\mathbf a|=\sqrt{\mathbf a\cdot\mathbf a}\).

## 3. What a metric tells us

An **event** is a location and a time; **spacetime** is the collection of events.
An inertial observer is one moving freely, without acceleration, in flat spacetime.

Pythagoras relates coordinate differences to physical lengths in Euclidean
geometry. A **metric** plays a similar role in spacetime: it relates changes
in time and position to clocks and light propagation.

For an inertial observer in special relativity, our sign convention is

\[
ds^2=-dt^2+dx^2+dy^2+dz^2.
\]

The symbols \(dt,dx,dy,dz\) mean infinitesimal coordinate changes. The symbol
\(ds^2\) is the **spacetime interval**, not an ordinary squared spatial length.
The minus sign distinguishes time from space.

- For a massive observer, \(ds^2=-d\tau^2<0\). The **proper time** \(\tau\)
  is time on that observer's own clock. Such a path is *timelike*.
- For light, \(ds^2=0\). Such a path is *null*. For light moving along the x axis
  in these flat coordinates, this gives \(|dx/dt|=1\), as expected when \(c=1\).

In curved spacetime, metric coefficients depend on position. Different coordinates
can describe the same geometry, just as Cartesian and polar coordinates describe
the same plane.

### Start with Schwarzschild coordinates

Einstein's equations determine the geometry. Deriving their Schwarzschild solution
is a separate project. Here we take that known solution as our physical input,
just as an introductory mechanics problem might supply a potential.

Define \(f(r)=1-1/r\). In the usual Schwarzschild coordinates,

\[
ds^2=-f\,dt^2+\frac{dr^2}{f}
     +r^2(d\theta^2+\sin^2\theta\,d\phi^2).
\]

Here \(t\) is Schwarzschild coordinate time, and \(\theta,\phi\) are polar and
azimuthal angles. The last term describes angular displacement on a sphere.

Far away, \(f\) approaches 1 and we recover flat spacetime in spherical
coordinates. At \(r=1\), however, \(1/f\) blows up. This is a failure of these
coordinates, not infinite physical curvature at the horizon. We need a better
clock coordinate to cross it.

### Change the clock coordinate

Keep the spatial coordinates, but introduce time \(T\) through

\[
dT=dt+\frac{1/\sqrt r}{f}\,dr.
\]

This transformation is singular at the old chart's horizon; the *new metric*
will have a regular extension there. Write \(w=1/\sqrt r\), then substitute
\(dt=dT-(w/f)dr\) into the time and radial terms:

\[
-f\left(dT-\frac{w}{f}dr\right)^2+\frac{dr^2}{f}
=-f\,dT^2+2w\,dT\,dr+\frac{1-w^2}{f}dr^2.
\]

Since \(1-w^2=f\), the last coefficient is 1. Completing the square gives

\[
ds^2=-dT^2+(dr+w\,dT)^2+r^2(d\theta^2+\sin^2\theta\,d\phi^2).
\]

These are **ingoing Painlevé–Gullstrand coordinates**, or PG coordinates.
With \(\mathbf w=\mathbf n/\sqrt r\), the same equation becomes

\[
\boxed{ds^2=-dT^2+|d\mathbf x+\mathbf w\,dT|^2.}
\]

All coefficients are finite at the horizon. The curvature singularity at \(r=0\)
remains; changing coordinates cannot remove a physical singularity.

## 4. A falling laboratory, and the one-way horizon

Consider a laboratory moving with

\[
\frac{d\mathbf x}{dT}=-\mathbf w.
\]

Its spatial term in the PG metric vanishes, giving \(ds^2=-dT^2\), so its clock
reads \(d\tau=dT\). This particular family follows radial free fall from rest
at infinity. Its members are called **rain observers**: imagine freely falling
laboratories arriving from very far away.

The substitution establishes their clock rate. Establishing that their motion
is free fall also uses the geodesic equation; section 7 introduces that principle
for light rays.

Notice the sign: \(\mathbf w\) points outward, but the laboratories move **inward**,
with coordinate velocity \(-\mathbf w\). A “river flowing inward” is a useful
picture of these coordinates. It is not a material substance. Coordinate speeds
need not equal locally measured speeds, and values above 1 here do not violate
special relativity's local speed limit.

For radial light, set the angular changes and \(ds^2\) to zero:

\[
0=-1+\left(\frac{dr}{dT}+w\right)^2,\qquad
\frac{dr}{dT}=-w\pm1.
\]

The plus sign means light directed outward relative to the falling laboratory.
The minus sign means light directed inward. Try three radii:

| Radius | \(w\) | Outward-directed light: \(dr/dT=1-w\) |
| --- | --- | --- |
| \(4\) | \(1/2\) | \(+1/2\): reaches larger radii |
| \(1\) | \(1\) | \(0\): follows the horizon |
| \(1/4\) | \(2\) | \(-1\): still moves to smaller radii |

Inside, even light aimed outward moves inward in these coordinates. Yet light
from outside can enter and reach you. The two statements fit together.

### Why hovering stops being possible

For fixed spatial coordinates, \(d\mathbf x=0\). The metric gives

\[
ds^2=-\left(1-\frac1r\right)dT^2=-\alpha^2dT^2,\qquad
\alpha=\sqrt{1-\frac1r}.
\]

Outside, the hovering clock satisfies \(d\tau=\alpha dT\). The factor \(\alpha\),
called the **lapse** here, compares that clock with coordinate time. At the
horizon the fixed-position path becomes null; inside it is not timelike. It
cannot be the path of a massive camera.

The app displays black for Hovering at or inside \(r=1\), explaining the invalid
frame in the panel. That is a response to an impossible observer choice, not a
prediction that a falling camera sees nothing. Observer types never switch
automatically.

## 5. Tracing a photograph backward

A real photon travels from source to camera. A ray tracer starts at the camera
and reconstructs the journey in reverse. This does not make a real photon escape
from inside the hole.

```text
Physical photon:   exterior source ---> horizon ---> camera inside
Our calculation:   exterior source <--- horizon <--- camera inside
```

Let \(\mathbf d\) be a unit direction from the camera toward a pixel's apparent
source. The arriving photon's local direction is \(\mathbf q=-\mathbf d\).

We need a parameter to label successive points on its path. A photon's proper
time is zero, so we cannot use a photon wristwatch. We use an **affine parameter**:
a path parameter for which the free ray equations have their standard form.
Its scale can be chosen freely, then must be kept consistent. Our \(\lambda\)
increases toward the past, away from the camera. Define

\[
\mathbf V=\frac{d\mathbf x}{d\lambda}.
\]

This spatial tangent is **not** camera velocity or a locally measured photon
speed. Its length need not be 1. Normalizing it after each step would change the
problem we are solving.

## 6. Turn one pixel into initial ray data

We make two transformations: from camera measurements to a local reference
laboratory, then from that laboratory to coordinates.

### The velocity controls and a Lorentz transformation

Write the panel velocity as fractions of \(c\). Its vector is \(\boldsymbol\beta\);
a speed of \(0.3c\) has magnitude 0.3. If \(\mathbf f\) is unit camera forward,
camera right and screen up are

\[
\mathbf r_c=\operatorname{normalize}(\mathbf f\times\mathbf{cameraUp}),\qquad
\mathbf u_c=\mathbf r_c\times\mathbf f.
\]

Here `normalize` means divide a nonzero vector by its length. The local laboratory
components, expressed using world-aligned axes, are

\[
\boldsymbol\beta=\beta_{\rm right}\mathbf r_c+
\beta_{\rm up}\mathbf u_c+\beta_{\rm forward}\mathbf f.
\]

The combined speed must obey

\[
\beta=|\boldsymbol\beta|<1,\qquad \gamma=\frac1{\sqrt{1-\beta^2}}.
\]

The factor \(\gamma\) is the Lorentz factor from special relativity. The app
caps \(\beta\) at 0.999. Two perpendicular components of 0.8 would already
exceed the allowed total speed, although each is individually below 1.

Photon energy is proportional to frequency. Scale its energy and momentum by
its camera-measured energy: its energy component is then 1 and its momentum
vector is \(\mathbf q\). This fixes a convenient normalization, not a physical
frequency shared by all photons.

In the reference laboratory, call these scaled quantities \(\omega\) and
\(\mathbf p\). A **Lorentz transformation** mixes energy and momentum parallel
to the relative velocity, leaving perpendicular momentum unchanged. For
\(\beta>0\), the special-relativity formulas are

\[
\omega=\gamma(1+\beta q_\parallel),\qquad
p_\parallel=\gamma(q_\parallel+\beta),\qquad
\mathbf p_\perp=\mathbf q_\perp.
\]

Here \(q_\parallel=\mathbf q\cdot\boldsymbol\beta/\beta\) is a scalar.
Recombining the parts gives the code's vector form:

\[
\omega=\gamma(1+\boldsymbol\beta\cdot\mathbf q),
\]

\[
\mathbf p=\mathbf q+
\left[\gamma+\frac{\gamma^2}{\gamma+1}
(\boldsymbol\beta\cdot\mathbf q)\right]\boldsymbol\beta.
\]

The identity \((\gamma-1)/\beta^2=\gamma^2/(\gamma+1)\) avoids a zero-speed
division by zero. At zero velocity, \(\omega=1\) and \(\mathbf p=\mathbf q\).
Light remains null: \(|\mathbf p|=\omega\).

### From the falling laboratory to coordinates

For Freely falling, the reference laboratory is the rain observer. A **tetrad**
is four local unit directions: one clock direction and three ruler directions.
“Orthonormal” means their metric products reproduce the local special-relativity
interval from section 3.

Writing a spacetime vector as `(time component, spatial vector)`, this frame has

\[
e_{(0)}=(1,-\mathbf w),\qquad e_{(i)}=(0,\hat{\mathbf e}_i),\quad i=x,y,z.
\]

The hats mean unit coordinate-axis vectors. Insert \(dT=1,d\mathbf x=-\mathbf w\)
into the PG metric: the clock direction has squared interval \(-1\). A spatial
unit direction has interval \(+1\), and its metric product with the clock
direction is zero. That is the orthonormality check.

We can also check what an arbitrary camera velocity means inside. In the local
rain laboratory its spacetime tangent per unit proper time has components
\((\gamma,\gamma\boldsymbol\beta)\), by special relativity. Combining these
with the tetrad gives

\[
\frac{dT}{d\tau}=\gamma,\qquad
\frac{d\mathbf x}{d\tau}=\gamma(\boldsymbol\beta-\mathbf w),\qquad
\frac{dr}{d\tau}=\gamma(\beta_r-1/\sqrt r).
\]

Here \(\beta_r=\boldsymbol\beta\cdot\mathbf n\) is the outward velocity component
relative to the falling laboratory. Inside, \(1/\sqrt r>1\) while \(\beta_r<1\),
so \(dr/d\tau<0\). Every allowed physical camera moves toward smaller radii,
even if an editor snapshot holds its position fixed for inspection.

Combine the photon components with these directions. Its future-directed
coordinate **four-momentum**, denoted by \(k\), is

\[
k=\omega e_{(0)}+p_xe_{(x)}+p_ye_{(y)}+p_ze_{(z)},\qquad
k^T=\omega,\quad \mathbf k=\mathbf p-\omega\mathbf w.
\]

The backward spatial tangent has the opposite sign:

\[
\boxed{\mathbf V=\omega\mathbf w-\mathbf p.}
\]

We also need a conserved energy label \(E\). Because the metric does not depend
on \(T\), there is a conserved quantity associated with time translation, much
as mechanics with a time-independent potential conserves energy. Expand the metric:

\[
ds^2=-f\,dT^2+2\mathbf w\cdot d\mathbf x\,dT+|d\mathbf x|^2.
\]

For two spacetime vectors \(a=(a^T,\mathbf a)\) and \(b=(b^T,\mathbf b)\),
the metric product is the bilinear expression
\(-f a^T b^T+a^T\mathbf w\cdot\mathbf b+b^T\mathbf w\cdot\mathbf a+\mathbf a\cdot\mathbf b\).
It comes from the coefficients of the expanded interval above. In particular,
the product of \(k\) with the coordinate time direction \((1,\mathbf0)\) is
\(k_T=-f k^T+\mathbf w\cdot\mathbf k\). The subscript means the metric was used
to form this product; it is not interchangeable with the superscript in \(k^T\).
Define \(E=-k_T\). Substituting and using \(f+|\mathbf w|^2=1\) gives

\[
\boxed{E=\omega-\mathbf w\cdot\mathbf p.}
\]

This is also called **Killing energy**. It is a conserved label, not the frequency
every observer measures. Setting the camera's measurement to 1 does not generally
make \(E=1\).

**Check the signs:** at zero panel velocity, look outward. Then
\(\mathbf d=\mathbf n\), \(\mathbf p=-\mathbf n\),
\(\mathbf V=(1+w)\mathbf n\), and \(E=1+w>0\). Even inside, this backward ray
moves outward. The physical, future-directed photon moves inward.

### The hovering reference outside the horizon

Use the same local Lorentz formulas, now relative to a static laboratory. To
convert its momentum, separate radial and tangential parts. The Schwarzschild
metric assigns proper radial length \(dr/\alpha\), so a local radial component
becomes a coordinate component multiplied by \(\alpha\). Tangential components
already have their usual spatial lengths. Consequently,

\[
\mathbf V=-[\mathbf p+(\alpha-1)(\mathbf p\cdot\mathbf n)\mathbf n],\qquad
E=\alpha\omega.
\]

The bracket leaves the tangential part alone and multiplies the radial part by
\(\alpha\). The energy relation also follows from the clock rate \(d\tau=\alpha dT\).

There is one compatibility exception: **exactly zero hovering velocity uses the
original exterior camera initialization and shading**. It preserves the earlier
appearance, but is approximate. An arbitrarily small nonzero velocity need not
reproduce that image exactly. Do not use that special case as a test of continuity
of the ideal observer equations.

## 7. Where the ray acceleration comes from

A **geodesic** is a free-particle path in spacetime. For light, it must also
satisfy the null condition. “Free” means no scattering or other nongravitational
interaction along that segment.

### Use symmetry to reduce the problem

Spherical symmetry keeps a ray in a plane through the center. Choose that plane
as \(\theta=\pi/2\). Temporarily use a future-increasing affine parameter
\(\sigma\), and let primes denote derivatives with respect to it.

For an affinely parametrized geodesic, the metric supplies the Lagrangian

\[
\mathcal L=\frac12[-f(t')^2+(r')^2/f+r^2(\phi')^2].
\]

A **Lagrangian** is a function of coordinates and their rates of change. The
path makes its integral (the **action**) stationary under small path variations
with fixed endpoints. This is the GR free-path principle in mechanics notation;
stationary does not necessarily mean a minimum. Applying the
Euler–Lagrange equation
\(d(\partial\mathcal L/\partial q')/d\sigma=\partial\mathcal L/\partial q\)
to a coordinate \(q\) gives the geodesic equations. For light, also impose
\(\mathcal L=0\). We use affine parameter, not the zero proper time of light.

Neither \(t\) nor \(\phi\) appears explicitly, only their derivatives. Their
corresponding momenta are constant:

\[
\frac{\partial\mathcal L}{\partial t'}=-f t'=-E,\qquad
\frac{\partial\mathcal L}{\partial\phi'}=r^2\phi'=L.
\]

The magnitude \(|L|\) is an angular-momentum label for the ray. Reversing tracing
direction reverses signed angular momentum but preserves \(L^2\). Squared radial
derivatives are also unchanged.

### Substitute into the null condition

Replace \(t'\) with \(E/f\) and \(\phi'\) with \(L/r^2\):

\[
0=-E^2/f+(r')^2/f+L^2/r^2.
\]

Multiply by \(f\) and rearrange. Now let dots denote derivatives with respect
to our past-increasing parameter \(\lambda\):

\[
\dot r^2=E^2-\frac{L^2}{r^2}+\frac{L^2}{r^3}.
\]

The Schwarzschild-coordinate algebra is valid away from the horizon. This result
has no division by \(f\); its regular PG formulation continues through the
horizon. We do not integrate the singular Schwarzschild time coordinate there.

Differentiate:

\[
2\dot r\ddot r=(2L^2/r^3-3L^2/r^4)\dot r.
\]

Where \(\dot r\ne0\), divide by \(2\dot r\), giving

\[
\ddot r=\frac{L^2}{r^3}-\frac{3L^2}{2r^4}.
\]

The full geodesic equation gives the same result at radial turning points; we
must not literally divide by zero there.

### Convert to vector acceleration

In planar polar coordinates, the radial component of vector acceleration is
\(\ddot r-r\dot\phi^2\), not just \(\ddot r\). Since
\(|\dot\phi|=|L|/r^2\), the term \(r\dot\phi^2=L^2/r^3\) cancels the first
term above. Conservation of angular momentum makes tangential acceleration zero:

\[
\boxed{\frac{d\mathbf x}{d\lambda}=\mathbf V,\qquad
\frac{d\mathbf V}{d\lambda}=-\frac{3L^2}{2r^5}\mathbf x,\qquad
L^2=|\mathbf x\times\mathbf V|^2.}
\]

That is the equation in the integrator. It is not Newton's inverse-square force
law: it describes light using an affine parameter in this spacetime.

For a radial ray, \(L=0\), so its spatial affine tangent is constant. That does
not mean gravity has vanished. Clock-coordinate behavior, frequency, and ability
to connect to exterior sources still depend on the geometry.

### A debugging check you can derive yourself

Polar-coordinate kinematics gives \(|\mathbf V|^2=\dot r^2+L^2/r^2\).
Substitution leaves

\[
\boxed{|\mathbf V|^2-\frac{L^2}{r^3}=E^2.}
\]

Both this relation and \(L^2\) should stay constant during a free integrated
segment, up to numerical error. Normalizing \(\mathbf V\) repeatedly would
generally break them.

## 8. What frequency reaches the camera?

An observer's **four-velocity** \(u\) is its spacetime tangent per unit proper
time. Its measured photon frequency, in our energy normalization, is

\[
\omega_{\rm measured}=-k\mathbin{\cdot_g}u.
\]

The notation \(\cdot_g\) means the spacetime metric product, not the Euclidean
three-vector dot product. Why this formula? In the observer's own local frame,
\(u=(1,\mathbf0)\); the metric product picks out minus the photon energy.
The same scalar product can be evaluated in any coordinates.

For a stationary exterior emitter, \(u=(1/\alpha_e,\mathbf0)\), where
\(\alpha_e=\sqrt{1-1/r_e}\) and \(r_e\) is its radius relative to the hole.
Consequently, \(\omega_e=E/\alpha_e\). Our camera measurement is normalized
to 1, so the frequency ratio is

\[
g\equiv\frac{\omega_{\rm camera}}{\omega_e}=\frac{\alpha_e}{E}.
\]

Here \(g\) means a frequency ratio, not gravitational acceleration. Values below
1 mean redshift; values above 1 mean blueshift. Camera motion is already included
in \(E\); adding another camera Doppler factor would count it twice.

**Example:** for the outward-looking radial rain observer in section 6,
\(E=1+1/\sqrt r\). A distant stationary source has \(\alpha_e\simeq1\).
At \(r=1\), \(g\simeq1/2\); at \(r=1/4\), \(g\simeq1/3\). These are finite
incoming signals, not an entirely black sky. Other directions can behave very
differently.

The disk material orbits, so it needs an emitter Doppler factor too. If its local
velocity is \(\boldsymbol\beta_e\), and \(\boldsymbol\ell\) is the unit direction
of the physical photon leaving it, the Lorentz energy transformation gives

\[
D_e=\frac{1}{\gamma_e(1-\boldsymbol\beta_e\cdot\boldsymbol\ell)},\qquad
\gamma_e=(1-|\boldsymbol\beta_e|^2)^{-1/2}.
\]

The disk uses \(g=(\alpha_e/E)D_e\). Its orbital speed can also be obtained
from the metric Lagrangian in section 7,
now for massive matter in a circular orbit. Constant radius makes the radial
Euler–Lagrange equation reduce to

\[
0=-\frac12\frac{df}{dr}(t')^2+r(\phi')^2,\qquad
\left(\frac{d\phi}{dt}\right)^2=\frac{1}{2r^3}.
\]

The primes here use an affine parameter along the orbit, and \(df/dr=1/r^2\).
A local hovering clock ticks at \(d\tau=\alpha dt\), so the locally measured
orbital speed is

\[
|\boldsymbol\beta_e|=\frac{r_e}{\alpha_e}\left|\frac{d\phi}{dt}\right|
=\frac{1}{\sqrt{2(r_e-1)}}.
\]

This assumes a circular geodesic outside the hole, as prescribed by our disk
model. It does not come from the camera velocity controls.

### Brightness, color, and scattering

A **blackbody spectrum** is the equilibrium radiation spectrum at a temperature.
For thermal sources, the renderer evaluates it at shifted temperature
\(gT_{\rm emit}\). Here \(T_{\rm emit}\) is temperature, not PG time.

Here **spectral intensity** means radiant energy per unit time, projected area,
solid angle, and frequency interval. An additional result from relativistic
radiative transfer is conservation of spectral intensity divided by frequency
cubed along a vacuum ray. Frequency-integrated intensity gets a factor \(g^4\):
three powers from spectral intensity, one from the frequency integration interval.
The code uses this factor as an approximation for RGB-only emission; three color
channels cannot reconstruct an arbitrary spectrum.

Turning redshift off disables these frequency/brightness factors, but leaves
aberration enabled. Image geometry can still change with velocity.

Surface scattering uses an approximate local static frame outside the horizon.
To recover a local direction, undo the conversion in section 6: divide the
coordinate radial component by \(\alpha_e\), then normalize for the material
calculation. For elastic stationary scattering—no frequency change in the
surface rest frame—the next backward segment has

\[
\mathbf V_{\rm new}=\frac{E}{\alpha_e}
[\mathbf d_s+(\alpha_e-1)(\mathbf d_s\cdot\mathbf n)\mathbf n].
\]

Here \(\mathbf d_s\) is the unit local direction selected for that segment.
The bracket converts its direction; \(E/\alpha_e\) restores its scale.

## 9. From equations to pixels

For each pixel sample, the renderer constructs its direction, transforms it into
initial \(\mathbf V,E,L^2\), then advances the ray. It tests traversed segments
for surfaces, the disk, and horizon boundaries. Source light or scattered light
contributes to a linear-light accumulation; display encoding comes afterward.
“Linear light” means the stored values are proportional to radiance, so averaging
samples corresponds to averaging light rather than encoded display colors.

The integrator is adaptive fourth-order Runge–Kutta (**RK4**). It compares one
full step with two half steps to estimate error, then adjusts the next step.
Default relative and absolute tolerances are \(10^{-4}\) and \(10^{-6}\).
Relative tolerance scales with the state being integrated; absolute tolerance
provides a scale for components near zero. These control local error estimates,
not guaranteed final pixel accuracy.

The near-hole step scale is 0.05. Full-scene integration permits larger steps
farther away, with additional limits near the hole and scene geometry. Rays
retain their unfinished state between bounded GPU work batches, keeping input
responsive. A gravity traversal has at most 16,384 integration attempts.

When does the code return darkness?

- An exterior backward ray entering the horizon is terminated. The scene does
  not supply radiation from a past white-hole region of the eternal solution.
  A white hole is a time-reversed black-hole region, from which signals emerge;
  it is not part of the astrophysical scenario being illustrated here.
- An interior ray may be traced outward through the horizon. If it later crosses
  inward again, that past branch is terminated.
- For \(E\le0\), no stationary exterior emitter can give a positive measured
  frequency, since \(\omega_e=E/\alpha_e\) there. This exterior-source model
  supplies no light for those rays.
- Hovering at or inside the horizon is an invalid observer choice.
- The code stops at a small singularity guard, \(r\le10^{-4}\). It does not
  model physics at the singularity itself.

Nonfinite calculations and exhausted integration budgets also terminate rays,
but increment a failure diagnostic. They must not be interpreted as physical
shadows. We do not include arbitrary interior light sources or reconstruct a
star's collapse history; some dark directions reflect those missing sources.

## 10. Experiments to try

Choose **Full scene** integration for these comparisons. Start outside with zero
velocity components. All radii below are measured relative to the hole's center.

### Same location, different observer

At \(r=4\), switch between Hovering and Freely falling without moving the camera
or changing its viewing direction. Which objects move in the image? Then change
one velocity component slightly. Toggle redshift to distinguish geometrical
changes from color changes. Remember the zero-hovering compatibility exception.

### Cross the horizon without switching observers

Select Freely falling first. Compare views at radii 1.1, 1.01, 0.99, and 0.9,
keeping orientation and velocity components fixed. The horizon is not a screen
that suddenly removes incoming light. A jump caused by changing observer type
is a different experiment.

### Look outward, then sideways

Inside, compare the direction away from the center with directions perpendicular
to that radial axis. The radial redshift example in section 8 predicts a distant
source directly outward. It does not describe the whole sky.

Near the singularity, a radially falling observer's exterior view becomes
concentrated and blueshifted into a band perpendicular to the radial direction;
other directions become redshifted. This is the “halo around your sides.”
[Hamilton and Polhemus discuss this interior view](https://arxiv.org/abs/0903.4717).
It is not the shrinking spot associated with hovering just outside the horizon.
Compare several radii rather than trusting only the last frame by the numerical guard.

### Find the photon sphere on paper

Write the radial equation as \(\dot r^2=E^2-U(r)\), where

\[
U(r)=L^2\left(\frac1{r^2}-\frac1{r^3}\right).
\]

This **effective potential** organizes the radial equation; it is not a Newtonian
potential for a massive particle. For \(L\ne0\), setting \(dU/dr=0\) gives its
maximum at \(r=3/2\): the **photon sphere**, where circular light orbits are
possible but unstable. A slight displacement from the circular orbit grows
rather than restoring the orbit.

At that radius, \(U=4L^2/27\). The critical ratio is therefore
\(L^2/E^2=27/4\) for \(E\ne0\). Starting radius and radial direction also matter;
the ratio alone does not determine the whole trajectory. Near this threshold,
tiny changes can determine whether a ray turns, lingers near the hole, or crosses
the horizon. This explains both striking image structure and demanding numerical tests.

## 11. What is exact, and what is approximate?

The derived ray equation and physical observer transformations belong to the
Schwarzschild model. The application is an educational renderer, not an exact
model of every process in an astrophysical black hole.

| Feature | How to interpret it |
| --- | --- |
| Full-scene integration | Uses the ray equation throughout the scene, subject to numerical error and finite boundaries. |
| Fixed-radius integration | Deliberately uses straight segments outside a finite gravity region. Full-metric invariants need not hold across that approximation. |
| Adaptive cutoff | Stops integration where an estimated bending contribution is small, adding approximation error. |
| Interior Freely falling | Uses full integration even if an exterior cutoff mode was selected. Observer type itself does not switch. |
| Zero-speed Hovering | Preserves the original approximate exterior rendering path. |
| Surfaces, direct lighting, atmosphere | Retain approximate spatial geometry and lighting connections. Static material frames are invalid inside the horizon. |
| Animation | Renders successive scene snapshots, without tracking emission history and relativistic light-travel delays between moving objects. |

For a clean horizon-crossing comparison, select Freely falling and Full scene on
both sides. Changing from an exterior cutoff to full integration inside can
affect the image. Deliberately changing observer frame can also change it at
the same position.

Tests check angular momentum, the null constraint, radial frequency shifts,
horizon behavior, the photon-sphere threshold, and CPU/GPU agreement. The CPU
reference uses double precision; the GPU integrator uses single precision.
Agreement is useful evidence, but shared code can share a mistake: independent
analytic checks matter. Near-critical bright rays make pixel comparisons
particularly sensitive to precision and sampling.

## 12. Code map and further reading

| Question | Starting point |
| --- | --- |
| How are view-relative velocities converted? | `observerSettings` in `src/SceneData.cpp` |
| How does a pixel become a ray? | `initTrace` in `assets/shaders/TraceCore.inl` |
| How are rays integrated and stopped? | `rk4`, `integrateField`, and `advanceTrace` in the same file |
| How are frequency shifts used? | `diskFrequencyShift`, `surfaceRadiance`, and `staticDirection` in `assets/shaders/LightingCore.inl` |
| Where are the reference and tests? | `src/CpuRenderer.cpp` and `src/Validation.cpp` |

The code's `observerLapse` field has a historical name: on the physical observer
path it stores the conserved energy denominator when redshift is enabled, not
necessarily a hovering lapse. Diagnostic ray APIs accept already transformed
local laboratory components, since they have no camera orientation for interpreting
view-relative controls.

For further reading, start with the figures and physical discussions:

- [Hamilton and Polhemus, *The edge of locality: visualizing a black hole from the inside*](https://arxiv.org/abs/0903.4717)
  develops the interior-observer picture, including the near-singularity view.
- [Kanai, Siino, and Hosoya, *Gravitational collapse in Painlevé–Gullstrand coordinates*](https://arxiv.org/abs/1008.0470)
  uses PG coordinates to describe spherical collapse across the horizon. Its
  collapse model goes beyond the stationary geometry rendered here.

A good first milestone is explaining the signs in
\(\mathbf V=\omega\mathbf w-\mathbf p\) and deriving \(dr/dT=-w\pm1\).
Those two calculations already explain how an interior camera can receive
exterior light without any future-directed photon escaping the hole.
