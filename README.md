# PMXImporter for Unreal Engine

PMXImporter is an Interchange-based Translator plugin for Unreal Engine that imports MikuMikuDance PMX models as Skeletal Mesh-centric assets. 
It focuses on building a robust Interchange node graph and delegates asset creation to Unreal’s factories/pipelines.


## Features
Included :
- Skeletal Mesh and Skeleton import
- Vertex Morph import as Morph Targets (UMorphTarget)
- Basic Materials/Textures: Base Color and Metadata
- Physics Asset: Basic RigidBody and Joint mapping
- Reimport support

Out of scope :
- VMD animation import
- Full support for UV/Material/Bone morphs
- Advanced physics tuning


## Requirements
- Unreal Engine 5.6 (Windows)


## Installation
Option A – Copy into your project:
1. Create (or open) your UE 5.6 C++ project.
2. Place the `Plugins/PMXImporter` folder into your project’s `Plugins` directory:
   - ProjectRoot\Plugins\PMXImporter
3. Open the project in Unreal Editor. UE will compile the plugin on first load.

Option B - via Fab:
1. TODO


## Usage
1. Open your project in Unreal Editor (5.6).
2. Drag & drop a `.pmx` file into the Content Browser, or use: Content Browser > Import.
3. Interchange import dialog appears. Recommended options:
   - Skeletal Mesh
     - Import Morph Targets: ON
     - Compute Tangents: Engine
     - Create Physics Asset: ON
   - Materials
     - Create Material Instances: ON
       - Parent material is not supported. The default material of the plug-in will be used.
4. Confirm. The Interchange pipeline will create SkeletalMesh/Skeleton, Materials, Textures, and a PhysicsAsset approximation.

Reimport:
- Right-click the generated SkeletalMesh (or Interchange Source Data) and choose Reimport to reflect changes from the original `.pmx`.


## Morph Targets
- Vertex Morphs are imported as UMorphTarget.


## Physics (Chaos)
- PMX rigid bodies and joints are approximated:
  - Shapes: Sphere/Box/Capsule
  - Constraints: Linear/Angular limits and basic drives


## Data Safety & Parser Notes
- The PMX reader validates lengths/offsets/indices/endianness.
- Large data blobs are passed via payload keys to reduce peak memory.
- On failure: clear error messages and safe termination (no partial state corruption).


## Logging & Diagnostics
- Log category: `LogPMXImporter`, `LogPmxReader`


## Limitations
Limitations (current):
- No VMD import; limited morph types (vertex only), basic material graph, approximate physics.
- Some Interchange pipeline options are not functional during the import process (e.g., PhysicsAsset Create Flag, Uniform Scale, etc.)
