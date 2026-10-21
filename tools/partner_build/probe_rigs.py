# Compare rest poses of the base character and the two animation libraries after FBX import.
# blender.exe -b --factory-startup --python probe_rigs.py
import bpy, os

Q = r"E:\Programs\Blender\assets\quaternius"
SRC = {
    "char": Q + r"\universal_base_characters\Universal Base Characters[Standard]\Base Characters\Unity\Superhero_Male_FullBody.fbx",
    "ual2": Q + r"\universal_animation_library_2\Universal Animation Library 2[Standard]\Unity\UAL2_Standard.fbx",
    "ual1": Q + r"\universal_animation_library\Animation Library[Standard]\Unreal Engine\AL_Standard.fbx",
}

bpy.ops.wm.read_factory_settings(use_empty=True)
arms = {}
for key, path in SRC.items():
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    arm = next(o for o in new if o.type == "ARMATURE")
    arm.name = "ARM_" + key
    arms[key] = arm
    mw = arm.matrix_world
    print(f"{key}: obj scale {tuple(round(s,4) for s in arm.scale)} rot {tuple(round(r,3) for r in arm.rotation_euler)}")
    hd = arm.data.bones["Head"].head_local
    print(f"  Head head_local {tuple(round(v,3) for v in hd)}  world {tuple(round(v,3) for v in mw @ hd)}")
    for o in new:
        if o.type == "MESH":
            zs = [ (o.matrix_world @ v.co).z for v in o.data.vertices ]
            print(f"  mesh {o.name}: world z {min(zs):.3f}..{max(zs):.3f}")
    print(f"  actions now: {len(bpy.data.actions)}")

def local_rest(arm, name):
    b = arm.data.bones[name]
    return b.matrix_local if b.parent is None else b.parent.matrix_local.inverted() @ b.matrix_local

names = [b.name for b in arms["char"].data.bones]
for other in ("ual2", "ual1"):
    worst = (0, "")
    for n in names:
        if n not in arms[other].data.bones: continue
        a = local_rest(arms["char"], n).to_quaternion()
        b = local_rest(arms[other], n).to_quaternion()
        d = a.rotation_difference(b).angle
        if d > worst[0]: worst = (d, n)
    print(f"char vs {other}: worst local rest rotation diff {worst[0]*57.3:.2f} deg at {worst[1]}")
