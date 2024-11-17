# Transform

Various types of transforms are defined in [math.h](../src/core/math.h):

* `LocalTransform` - single precision position, quaternion rotation, and uniform scale.
* `LocalRigidTransform` - similar to `LocalTransform`, but without scale.
* `Transform` - double precision position, quaternion rotation, and nonuniform scale.
* `RigidTransform` - similar to `Transform`, but without scale.

"Local" indicates single precision position, while "Rigid" indicates the absence of scale.

## Entity transform

Entities utilize `Transform`. Note that `Transform` is not equivalent to a matrix. Its components are applied in **SRT** order: scale, rotation, position: `pos + rot.rotate(value * scale)`. Consequently, it cannot represent skew. Additionally, the composition of two transforms does not behave like matrix multiplication if the scale is nonuniform. The scale is "lossy", i.e. when composing multiple transforms, "direction" of the original scale is lost.