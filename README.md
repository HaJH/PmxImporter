# PMXImporter for Unreal Engine

PMXImporter is a plugin for Unreal Engine that imports MikuMikuDance PMX models as Skeletal Mesh-centric assets. 
It provides Pmx Translator to Unreal's Generic Interchange Factories/Pipelines.


## Features
Included :
- Skeletal Mesh and Skeleton import
- Vertex Morph import as Morph Targets (UMorphTarget)
- Basic Materials/Textures: Base Color and Metadata
- Reimport support

Out of scope :
- VMD animation import
- Full support for UV/Material/Bone morphs
- Physics Asset. RigidBody and Joint mapping


## Requirements
- Unreal Engine 5.6 (Windows)


## Installation
Option A – Copy into your project:
   1. Create (or open) your UE 5.6 C++ project.
   2. Place the `Plugins/PMXImporter` folder into your project’s `Plugins` directory:
      - ProjectRoot\Plugins\PMXImporter
   3. Open the project in Unreal Editor. UE will compile the plugin on first load.
Option B - via Fab
   https://www.fab.com/ko/listings/d0d45e1b-c8b1-43a5-b0d4-2ac98b183917
   

## Usage
1. Open your project in Unreal Editor (5.6).
2. Drag & drop a `.pmx` file into the Content Browser, or use: Content Browser > Import.
3. Interchange import dialog appears. Recommended options:
   - Skeletal Mesh
     - Import Morph Targets: ON
     - Compute Tangents: Engine
   - Materials
     - Create Material Instances: ON
       - Parent material is not supported. The default material of the plug-in will be used.
4. Confirm. The Interchange pipeline will create SkeletalMesh/Skeleton, Materials, Textures, and a PhysicsAsset approximation.

Reimport:
- Right-click the generated SkeletalMesh (or Interchange Source Data) and choose Reimport to reflect changes from the original `.pmx`.


## Morph Targets
- Vertex Morphs are imported as UMorphTarget.


## Logging & Diagnostics
- Log category: `LogPMXImporter`, `LogPmxReader`


## Limitations
Limitations (current):
- No VMD import; limited morph types (vertex only), basic material graph, approximate physics.
- Some Interchange pipeline options are not functional during the import process (e.g., PhysicsAsset Create Flag, Uniform Scale, etc.)
