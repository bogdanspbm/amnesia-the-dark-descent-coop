# Which textures do the character, hair and eye materials use, and how big are the UV islands.
import bpy
UBC = r"E:\Programs\Blender\assets\quaternius\universal_base_characters\Universal Base Characters[Standard]"
bpy.ops.wm.read_factory_settings(use_empty=True)
for p in (UBC + r"\Base Characters\Godot - UE\Superhero_Male_FullBody.gltf",
          UBC + r"\Hairstyles\Rigged to Head Bone\glTF (Godot -Unreal)\Hair_SimpleParted.gltf"):
    bpy.ops.import_scene.gltf(filepath=p)
for ob in bpy.data.objects:
    if ob.type != "MESH": continue
    print("MESH", ob.name, "slots", [m.name for m in ob.data.materials], "uv layers", [u.name for u in ob.data.uv_layers])
for m in bpy.data.materials:
    imgs = [n.image.name + " <- " + (n.image.filepath or "") for n in m.node_tree.nodes if n.type == "TEX_IMAGE" and n.image] if m.node_tree else []
    bsdf = m.node_tree.nodes.get("Principled BSDF") if m.node_tree else None
    col = tuple(round(c, 3) for c in bsdf.inputs["Base Color"].default_value) if bsdf else None
    print("MAT", m.name, "base", col, "images", imgs)
