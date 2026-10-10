# Lake-bed visibility

Water now blends surface colour over the already-rendered lake bed using
source-alpha blending. Vertex-baked shore depths are interpolated into the
fragment shader. Optical depth grows with water depth and oblique viewing
angles, giving shallow water more transmission while deeper water darkens.
Fresnel reflection and foam increase opacity. Wave lighting and planar
reflections remain part of the surface colour.

Depth writes retain the water surface for subsequent effects; alpha blending
preserves the destination scene alpha for existing postprocessing. The
underwater terrain remains actual scene geometry. This is an inexpensive
transmission approximation, without a separate refraction target or bent
screen-space samples.

The changed stages compile and pass SPIR-V validation. On the target GPU,
inspect a shallow shoreline looking down, then a deep basin and grazing view;
verify visible bottoms, stronger reflection at low angles, wave silhouettes,
and Vulkan validation output. GPU appearance has not been verified in the
editing environment.
