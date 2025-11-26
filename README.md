# PMXImporter for Unreal Engine

PMXImporter is a plugin for Unreal Engine that imports MikuMikuDance PMX models as Skeletal Mesh-centric assets.
It provides Pmx Translator with a dedicated PMX import pipeline.


## Features
Included :
- Skeletal Mesh and Skeleton import
- Vertex Morph import as Morph Targets (UMorphTarget)
- PhysicsAsset generation (RigidBody and Joint mapping)
- Basic Materials/Textures: Base Color and Metadata
- Reimport support

Out of scope :
- VMD animation import
- Full support for UV/Material/Bone morphs


## Requirements
- Unreal Engine 5.6+ (Windows)


## Installation
Option A – Copy into your project:
   1. Create (or open) your UE 5.6+ C++ project.
   2. Place the `Plugins/PMXImporter` folder into your project's `Plugins` directory:
      - ProjectRoot\Plugins\PMXImporter
   3. Open the project in Unreal Editor. UE will compile the plugin on first load.

Option B - via Fab
   - https://www.fab.com/ko/listings/d0d45e1b-c8b1-43a5-b0d4-2ac98b183917


## Usage
1. Open your project in Unreal Editor (5.6+).
2. Drag & drop a `.pmx` file into the Content Browser, or use: Content Browser > Import.
3. PMX import dialog appears. Configurable options:
   - Common: Scale
   - Mesh: Import Morph Targets
   - Physics: Enable Physics, Shape Scale options
   - Material: Create Material Instances
4. Confirm. The pipeline will create SkeletalMesh, Skeleton, Materials, Textures, and PhysicsAsset.

Reimport:
- Right-click the generated SkeletalMesh (or Interchange Source Data) and choose Reimport to reflect changes from the original `.pmx`.


## Morph Targets
- Vertex Morphs are imported as UMorphTarget.


## Logging & Diagnostics
- Log category: `LogPMXImporter`, `LogPmxReader`


## Limitations
Limitations (current):
- No VMD import; limited morph types (vertex only), basic material graph.
- Physics constraints use soft settings; some complex PMX physics setups may require manual tuning.
