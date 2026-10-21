# Builds the co-op partner model for Amnesia: TDD from the CC0 Quaternius packs.
#   blender.exe -b --factory-startup --python build_partner.py
# Output goes to <game>\_coop\partner: coop_partner.dae/.ent, one .dae_anim per animation slot,
# the lantern mesh, and a flat-colour .png + .mat per material.
import bpy, os, math, random
from mathutils import Matrix, Vector
from bpy_extras import anim_utils

GAME = r"E:\Programs\Steam\steamapps\common\Amnesia The Dark Descent"
OUT = os.path.join(GAME, r"_coop\partner")
Q = r"E:\Programs\Blender\assets\quaternius"
UBC = Q + r"\universal_base_characters\Universal Base Characters[Standard]"
CHAR_FBX = UBC + r"\Base Characters\Unity\Superhero_Male_FullBody.fbx"
HAIR_FBX = UBC + r"\Hairstyles\Rigged to Head Bone\FBX (Unity)\Hair_SimpleParted.fbx"
UAL1_FBX = Q + r"\universal_animation_library\Animation Library[Standard]\Unreal Engine\AL_Standard.fbx"
UAL2_FBX = Q + r"\universal_animation_library_2\Universal Animation Library 2[Standard]\Unity\UAL2_Standard.fbx"

TARGET_HEIGHT = 1.75
FPS = 30
PREFIX = "coop_partner_"

# Slot -> (library, source action)
SLOTS = {
    "Idle": ("ual1", "Idle_Loop"),
    "Walk": ("ual1", "Walk_Loop"),
    "Run": ("ual1", "Jog_Fwd_Loop"),
    "CrouchIdle": ("ual1", "Crouch_Idle_Loop"),
    "CrouchWalk": ("ual1", "Crouch_Fwd_Loop"),
    "IdleLantern": ("ual2", "Idle_Lantern_Loop"),
    "Hurt": ("ual1", "Hit_Chest"),
    "Death": ("ual1", "Death01"),
}
LANTERN_VARIANTS = {"WalkLantern": "Walk", "RunLantern": "Run", "CrouchIdleLantern": "CrouchIdle", "CrouchWalkLantern": "CrouchWalk"}
LANTERN_SIDE = "r"	# probe_actions.py: Idle_Lantern_Loop raises hand_r to 1.4 m

COLORS = {
    "skin": (0.76, 0.60, 0.50),
    "waistcoat": (0.20, 0.14, 0.10),
    "shirt": (0.78, 0.74, 0.65),
    "trousers": (0.22, 0.20, 0.18),
    "boots": (0.08, 0.06, 0.05),
    "hair": (0.18, 0.11, 0.06),
    "eyes": (0.12, 0.08, 0.05),
    "lantern_metal": (0.10, 0.09, 0.08),
    "lantern_glass": (1.00, 0.72, 0.38),
}

log = []
def say(*a):
    s = " ".join(str(x) for x in a)
    log.append(s); print("BUILD:", s)

# --------------------------------------------------------------------------------------------
# Scene and imports

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

def import_fbx(path):
    before = set(bpy.data.objects); acts_before = set(bpy.data.actions)
    bpy.ops.import_scene.fbx(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    return new, [a for a in bpy.data.actions if a not in acts_before]

char_objs, _ = import_fbx(CHAR_FBX)
arm = next(o for o in char_objs if o.type == "ARMATURE")
arm.name = "coop_partner_rig"
body = next(o for o in char_objs if o.type == "MESH" and o.name.lower().startswith("superhero"))
eyes = next(o for o in char_objs if o.type == "MESH" and o.name.lower().startswith("eyes"))
brows = next(o for o in char_objs if o.type == "MESH" and o.name.lower().startswith("eyebrows"))

hair_objs, _ = import_fbx(HAIR_FBX)
hair = next(o for o in hair_objs if o.type == "MESH")
say("hair objects:", [(o.name, o.type) for o in hair_objs])

# Hair: rigged to its own head bone; move it onto our rig, fully weighted to Head
mw = hair.matrix_world.copy()
for m in list(hair.modifiers): hair.modifiers.remove(m)
hair.parent = None
hair.matrix_world = mw
hair.vertex_groups.clear()
vg = hair.vertex_groups.new(name="Head")
vg.add(list(range(len(hair.data.vertices))), 1.0, "REPLACE")
for o in hair_objs:
    if o is not hair: bpy.data.objects.remove(o, do_unlink=True)
hz = [(hair.matrix_world @ v.co).z for v in hair.data.vertices]
say(f"hair z range {min(hz):.3f}..{max(hz):.3f}")

# Scale to the target height and bake rotation/scale into the data, so the rig's object transform is identity
bz = [(body.matrix_world @ v.co).z for v in body.data.vertices]
height = max(max(bz), max(hz)) - min(bz)
s = TARGET_HEIGHT / height
say(f"source height {height:.3f} m, scale {s:.4f}")
arm.scale = [v * s for v in arm.scale]
hair.scale = [v * s for v in hair.scale]
hair.location = hair.location * s
bpy.ops.object.select_all(action="DESELECT")
for o in (arm, body, eyes, brows, hair):
    o.select_set(True)
bpy.context.view_layer.objects.active = arm
# Only the scale is baked: the rig keeps its object rotation so its armature space stays the same as the
# animation libraries' (retargeting compares rotations in armature space). The writer uses world matrices.
bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
hair.parent = arm
hair.matrix_parent_inverse = arm.matrix_world.inverted()
mod = hair.modifiers.new("Armature", "ARMATURE"); mod.object = arm
bz = [(body.matrix_world @ v.co).z for v in body.data.vertices]
say(f"after scale: feet z {min(bz):.3f}, top {max(bz):.3f}, arm scale {tuple(arm.scale)}")

lib_arms = {}
for key, path in (("ual1", UAL1_FBX), ("ual2", UAL2_FBX)):
    objs, acts = import_fbx(path)
    a = next(o for o in objs if o.type == "ARMATURE")
    a.name = "src_" + key
    for o in objs:
        if o.type == "MESH": bpy.data.objects.remove(o, do_unlink=True)
    lib_arms[key] = (a, {x.name.split("|")[-1]: x for x in acts})
scene.render.fps = FPS; scene.render.fps_base = 1.0

# --------------------------------------------------------------------------------------------
# Retarget: rotations are carried over as changes from each skeleton's own rest pose, in armature
# space, so differences in bone roll between the packs don't matter. Pelvis height is scaled.

def bones_parent_first(a):
    order = []
    def walk(b):
        order.append(b.name)
        for c in b.children: walk(c)
    for b in a.data.bones:
        if b.parent is None: walk(b)
    return order

ORDER = bones_parent_first(arm)
REST_T = {n: arm.data.bones[n].matrix_local.copy() for n in ORDER}

def new_action(name):
    act = bpy.data.actions.new(name)
    act.use_fake_user = True	# keep it in the saved .blend
    slot = act.slots.new(id_type="OBJECT", name=arm.name)
    cb = anim_utils.action_ensure_channelbag_for_slot(act, slot)
    return act, cb

def write_curves(cb, frames, values):
    # values: {bone: [(quat, loc) per frame]}
    for n, seq in values.items():
        for idx in range(4):
            fc = cb.fcurves.new(f'pose.bones["{n}"].rotation_quaternion', index=idx, group_name=n)
            fc.keyframe_points.add(len(seq))
            co = []
            for f, (q, l) in zip(frames, seq): co += [f, q[idx]]
            fc.keyframe_points.foreach_set("co", co)
            fc.keyframe_points.foreach_set("interpolation", [1] * len(seq))  # LINEAR
        for idx in range(3):
            fc = cb.fcurves.new(f'pose.bones["{n}"].location', index=idx, group_name=n)
            fc.keyframe_points.add(len(seq))
            co = []
            for f, (q, l) in zip(frames, seq): co += [f, l[idx]]
            fc.keyframe_points.foreach_set("co", co)
            fc.keyframe_points.foreach_set("interpolation", [1] * len(seq))

def retarget(lib, src_name, slot_name):
    src, acts = lib_arms[lib]
    sact = acts[src_name]
    ad = src.animation_data or src.animation_data_create()
    ad.action = sact
    if sact.slots: ad.action_slot = sact.slots[0]
    f0, f1 = int(round(sact.frame_range[0])), int(round(sact.frame_range[1]))
    rest_s = {b.name: b.matrix_local.copy() for b in src.data.bones}
    pel_ratio = REST_T["pelvis"].translation.z / rest_s["pelvis"].translation.z
    values = {n: [] for n in ORDER}
    prev_q = {}
    for f in range(f0, f1 + 1):
        scene.frame_set(f)
        desired = {}
        for n in ORDER:
            bt = arm.data.bones[n]
            rt = REST_T[n]
            if n in src.pose.bones and n in rest_s:
                ms = src.pose.bones[n].matrix
                rot = (ms.to_3x3() @ rest_s[n].to_3x3().inverted()) @ rt.to_3x3()
            else:
                rot = None
            if bt.parent is None:
                parent_pose = Matrix.Identity(4); rel = rt
            else:
                parent_pose = desired[bt.parent.name]
                rel = REST_T[bt.parent.name].inverted() @ rt
            follow = parent_pose @ rel  # where the bone sits if it only follows its parent
            if rot is None: rot = follow.to_3x3()
            if n in ("root", "pelvis") and n in src.pose.bones:
                loc = src.pose.bones[n].matrix.translation * (pel_ratio if n == "pelvis" else 1.0)
            else:
                loc = follow.translation
            m = rot.to_4x4(); m.translation = loc
            desired[n] = m
            basis = follow.inverted() @ m
            q = basis.to_quaternion()
            if n in prev_q and prev_q[n].dot(q) < 0: q = -q
            prev_q[n] = q
            values[n].append((tuple(q), tuple(basis.translation)))
    act, cb = new_action(PREFIX + slot_name)
    frames = list(range(1, f1 - f0 + 2))
    write_curves(cb, frames, values)
    act.frame_range = (1, frames[-1]); act.use_frame_range = True
    say(f"{slot_name}: {lib} {src_name} frames {f0}-{f1} -> 1-{frames[-1]}")
    return act

actions = {slot: retarget(lib, name, slot) for slot, (lib, name) in SLOTS.items()}

def fc_map(act):
    cb = anim_utils.action_get_channelbag_for_slot(act, act.slots[0])
    return {(fc.data_path, fc.array_index): fc for fc in cb.fcurves}

# Downed: Death01's last pose held for one second
def held_action(slot_name, src_act):
    src = fc_map(src_act)
    end = src_act.frame_range[1]
    act, cb = new_action(PREFIX + slot_name)
    for (dp, idx), fc in src.items():
        nf = cb.fcurves.new(dp, index=idx, group_name=fc.group.name if fc.group else "")
        v = fc.evaluate(end)
        nf.keyframe_points.add(2)
        nf.keyframe_points.foreach_set("co", [1, v, FPS + 1, v])
    act.frame_range = (1, FPS + 1); act.use_frame_range = True
    return act
actions["Downed"] = held_action("Downed", actions["Death"])

# Lantern variants: base action with the lantern arm taken from IdleLantern, stretched to the base length
SIDE = LANTERN_SIDE
FINGERS = ("index", "middle", "pinky", "ring", "thumb")
CHAIN = {n for n in ORDER if n in (f"clavicle_{SIDE}", f"upperarm_{SIDE}", f"lowerarm_{SIDE}", f"hand_{SIDE}")
         or (n.endswith("_" + SIDE) and n.startswith(FINGERS))}
say("lantern arm chain:", sorted(CHAIN))
def lantern_variant(slot_name, base_act, lant_act):
    base = fc_map(base_act); lant = fc_map(lant_act)
    lb = base_act.frame_range[1]; ll = lant_act.frame_range[1]
    act, cb = new_action(PREFIX + slot_name)
    nfr = int(lb)
    for (dp, idx), fc in base.items():
        bone = dp.split('"')[1]
        srcfc = lant[(dp, idx)] if bone in CHAIN else fc
        nf = cb.fcurves.new(dp, index=idx, group_name=bone)
        nf.keyframe_points.add(nfr)
        co = []
        for f in range(1, nfr + 1):
            t = 1 + (f - 1) * (ll - 1) / max(lb - 1, 1) if bone in CHAIN else f
            co += [f, srcfc.evaluate(t)]
        nf.keyframe_points.foreach_set("co", co)
    act.frame_range = (1, nfr); act.use_frame_range = True
    return act
for slot, base in LANTERN_VARIANTS.items():
    actions[slot] = lantern_variant(slot, actions[base], actions["IdleLantern"])

# Crouching bends the spine far forward, and an arm copied in local terms follows it down to the knee.
# In the crouch variants the clavicle stays the crouch one and the upper arm keeps the orientation it has in
# IdleLantern in armature space, so the lantern is held at the side as when standing.
def pose_action(act, frame):
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = act
    if act.slots: ad.action_slot = act.slots[0]
    scene.frame_set(int(frame), subframe=frame - int(frame))

def upright_lantern_arm(var_act, base_act, lant_act):
    up, clav = f"upperarm_{SIDE}", f"clavicle_{SIDE}"
    lb = base_act.frame_range[1]; ll = lant_act.frame_range[1]
    rel_up = REST_T[clav].inverted() @ REST_T[up]
    cb = anim_utils.action_get_channelbag_for_slot(var_act, var_act.slots[0])
    curves = {(fc.data_path, fc.array_index): fc for fc in cb.fcurves}
    base_curves = fc_map(base_act)
    # the crouch clavicle
    for idx in range(4):
        dp = f'pose.bones["{clav}"].rotation_quaternion'
        src = base_curves[(dp, idx)]
        pts = curves[(dp, idx)].keyframe_points
        for k in pts: k.co.y = src.evaluate(k.co.x)
    vals = []
    prev = None
    for f in range(1, int(lb) + 1):
        t = 1 + (f - 1) * (ll - 1) / max(lb - 1, 1)
        pose_action(lant_act, t)
        want = arm.pose.bones[up].matrix.to_3x3()
        pose_action(base_act, f)
        follow = arm.pose.bones[clav].matrix @ rel_up
        q = (follow.to_3x3().inverted() @ want).to_quaternion()
        if prev is not None and prev.dot(q) < 0: q = -q
        prev = q; vals.append(q)
    dp = f'pose.bones["{up}"].rotation_quaternion'
    for idx in range(4):
        pts = curves[(dp, idx)].keyframe_points
        for k, q in zip(pts, vals): k.co.y = q[idx]
    for fc in cb.fcurves: fc.update()

for slot in ("CrouchIdleLantern", "CrouchWalkLantern"):
    upright_lantern_arm(actions[slot], actions[LANTERN_VARIANTS[slot]], actions["IdleLantern"])
    say(f"{slot}: lantern arm kept upright over the crouch")

for a, _ in lib_arms.values():
    bpy.data.objects.remove(a, do_unlink=True)

# --------------------------------------------------------------------------------------------
# Look: everyday proportions, clothes as their own layers, real skin/hair/eye textures, cloth textures

import sys
sys.path.insert(0, os.path.join(GAME, r"_coop\partner_src"))
import partner_look
LOOK = partner_look.build(arm, body, hair, brows, eyes, OUT, PREFIX, UBC,
                          {k: COLORS[k] for k in ("lantern_metal", "lantern_glass")}, say)
mats = LOOK["mats"]

# --------------------------------------------------------------------------------------------
# Lantern: modelled here, held in the lantern hand in the IdleLantern pose

def set_action(act):
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = act
    if act is not None and act.slots: ad.action_slot = act.slots[0]

set_action(actions["IdleLantern"]); scene.frame_set(1)
hand = arm.pose.bones[f"hand_{SIDE}"]
mh = arm.matrix_world @ hand.matrix
# Grip: middle of the fist, a little along the hand bone from the wrist
grip = mh @ Vector((0.0, 0.085, 0.0))
say(f"hand_{SIDE} at {tuple(round(v,3) for v in mh.translation)}, grip {tuple(round(v,3) for v in grip)}")

import bmesh
def add_part(bm, kind, mat, **kw):
    geom = None
    if kind == "cyl":
        r = bmesh.ops.create_cone(bm, cap_ends=True, segments=kw.get("seg", 16), radius1=kw["r1"], radius2=kw["r2"], depth=kw["h"])
    elif kind == "box":
        r = bmesh.ops.create_cube(bm, size=1.0)
        bmesh.ops.scale(bm, vec=kw["size"], verts=r["verts"])
    elif kind == "torus":
        r = {"verts": []}
        R, rr, seg, ring = kw["R"], kw["r"], 20, 8
        verts = [[None]*ring for _ in range(seg)]
        for i in range(seg):
            a = 2*math.pi*i/seg
            for j in range(ring):
                b = 2*math.pi*j/ring
                # loop in the XZ plane, so the handle stands upright
                x = (R + rr*math.cos(b)) * math.cos(a); z = (R + rr*math.cos(b)) * math.sin(a); y = rr*math.sin(b)
                verts[i][j] = bm.verts.new((x, y, z)); r["verts"].append(verts[i][j])
        for i in range(seg):
            for j in range(ring):
                f = bm.faces.new((verts[i][j], verts[(i+1)%seg][j], verts[(i+1)%seg][(j+1)%ring], verts[i][(j+1)%ring]))
    bmesh.ops.translate(bm, vec=kw.get("at", (0,0,0)), verts=r["verts"])
    faces = {f for v in r["verts"] for f in v.link_faces}
    for f in faces: f.material_index = mat

# Built hanging from its handle: the top of the handle at the origin, body below
bm = bmesh.new()
METAL, GLASS = 0, 1
add_part(bm, "torus", METAL, R=0.035, r=0.005, at=(0, 0, -0.035))                    # handle loop, top at z=0
add_part(bm, "cyl", METAL, r1=0.07, r2=0.02, h=0.045, at=(0, 0, -0.085))             # cap
add_part(bm, "cyl", METAL, r1=0.072, r2=0.072, h=0.008, at=(0, 0, -0.11))            # cap rim
add_part(bm, "cyl", GLASS, r1=0.05, r2=0.05, h=0.11, at=(0, 0, -0.17), seg=12)        # glass
for i in range(4):
    a = math.pi/4 + i*math.pi/2
    add_part(bm, "box", METAL, size=(0.008, 0.008, 0.12), at=(0.056*math.cos(a), 0.056*math.sin(a), -0.17))  # posts
add_part(bm, "cyl", METAL, r1=0.068, r2=0.068, h=0.02, at=(0, 0, -0.235))            # base
glass_centre_local = Vector((0, 0, -0.17))
lmesh = bpy.data.meshes.new(PREFIX + "lantern")
bm.to_mesh(lmesh); bm.free()
lmesh.materials.append(mats["lantern_metal"]); lmesh.materials.append(mats["lantern_glass"])
lmesh.uv_layers.new()
lantern = bpy.data.objects.new(PREFIX + "lantern", lmesh)
scene.collection.objects.link(lantern)
lz = [v.co.z for v in lmesh.vertices]
say(f"lantern height {max(lz)-min(lz):.3f} m")

# Hang it from the grip, upright in the world, facing the same way as the hand's yaw
hand_fwd = (mh.to_3x3() @ Vector((0, 1, 0))); yaw = math.atan2(hand_fwd.y, hand_fwd.x)
world_from_lantern = Matrix.Translation(grip) @ Matrix.Rotation(yaw, 4, "Z")
glass_world = world_from_lantern @ glass_centre_local
# Vertices in hand-bone space. The files are written Y_UP, which only turns the skeleton root, so a bone's
# own space in the game is the same as in Blender and the lantern can hang on the bone as is.
bone_from_world = mh.inverted()
lantern_bone_space = []
for v in lmesh.vertices:
    vb = bone_from_world @ (world_from_lantern @ v.co)
    lantern_bone_space.append(vb.copy())
    v.co = vb
glass_bone = bone_from_world @ glass_world
say(f"LanternOffset (glass centre in hand_{SIDE} space): {glass_bone.x:.4f} {glass_bone.y:.4f} {glass_bone.z:.4f}")

# A preview copy sitting in the hand, for the check render
prev = lantern.copy(); prev.data = lmesh.copy(); prev.name = "lantern_preview"
for v, vb in zip(prev.data.vertices, lantern_bone_space): v.co = vb
scene.collection.objects.link(prev)
prev.parent = arm; prev.parent_type = "BONE"; prev.parent_bone = f"hand_{SIDE}"
# Bone parenting is relative to the bone's tail; put the object at the bone head with the bone's axes
prev.matrix_world = arm.matrix_world @ hand.matrix

# --------------------------------------------------------------------------------------------
# Export

# Blender 5 has no Collada exporter any more, so coop_collada.py writes what HPL2 reads
import sys
sys.path.insert(0, os.path.join(GAME, r"_coop\partner_src"))
import coop_collada as cc

cmats = {k: cc.Material(PREFIX + k, diffuse) for k, (diffuse, _) in LOOK["files"].items()}
def slot_mats(ob):
    return [cmats[m.name[len(PREFIX):]] for m in ob.data.materials]

set_action(None)
for pb in arm.pose.bones:
    pb.rotation_quaternion = (1, 0, 0, 0); pb.location = (0, 0, 0); pb.scale = (1, 1, 1)
scene.frame_set(1)

rest_world = {n: arm.matrix_world @ arm.data.bones[n].matrix_local for n in ORDER}
parents = {n: (arm.data.bones[n].parent.name if arm.data.bones[n].parent else None) for n in ORDER}
parts = []
for ob, prefix in LOOK["meshes"]:
    ps = cc.parts_from_object(ob, slot_mats(ob), ORDER, part_prefix=prefix)
    for p in ps: p.name = prefix.rstrip("_")
    parts += ps
say("submeshes:", [(p.name, len(p.tris)) for p in parts])
cc.write(os.path.join(OUT, "coop_partner.dae"), parts, ORDER, rest_world, parents, None, FPS)

ALL_SLOTS = ["Idle", "Walk", "Run", "CrouchIdle", "CrouchWalk", "IdleLantern", "WalkLantern", "RunLantern",
             "CrouchIdleLantern", "CrouchWalkLantern", "Hurt", "Downed", "Death"]
for slot in ALL_SLOTS:
    act = actions[slot]
    set_action(act)
    frames = []
    for f in range(int(act.frame_range[0]), int(act.frame_range[1]) + 1):
        scene.frame_set(f)
        frames.append({n: (arm.matrix_world @ arm.pose.bones[n].matrix).copy() for n in ORDER})
    cc.write(os.path.join(OUT, PREFIX + slot + ".dae_anim"), parts, ORDER, rest_world, parents, frames, FPS)
    say(f"exported {slot}: {len(frames)} frames, {(len(frames)-1)/FPS:.2f} s")
set_action(None)

lantern.matrix_world = Matrix.Identity(4)
lparts = cc.parts_from_object(lantern, slot_mats(lantern), None, to_file=Matrix.Identity(4), skinned=False, part_prefix="coop_partner_lantern_")
cc.write(os.path.join(OUT, PREFIX + "lantern.dae"), lparts)

# --------------------------------------------------------------------------------------------
# .ent

anims = "\n".join(f'            <Animation File="{PREFIX}{slot}.dae_anim" Name="{slot}" Speed="1" SpecialEventTime="-1" />' for slot in ALL_SLOTS)
ent = f"""<Entity>
    <ModelData>
        <Entities />
        <Mesh Filename="coop_partner.dae" />
        <Bones />
        <Shapes />
        <Bodies />
        <Joints />
        <Animations>
{anims}
        </Animations>
        <ProcAnimations />
    </ModelData>
    <CoopAvatar YawOffset="0" LanternBone="hand_{SIDE}" LanternMesh="{PREFIX}lantern.dae" LanternOffset="{glass_bone.x:.4f} {glass_bone.y:.4f} {glass_bone.z:.4f}" />
    <UserDefinedVariables EntitySubType="" EntityType="NPC" />
</Entity>
"""
with open(os.path.join(OUT, "coop_partner.ent"), "w", encoding="utf-8") as fh:
    fh.write(ent)

# Keep a .blend of the result for inspection and later tweaks
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(GAME, r"_coop\partner_src\coop_partner.blend"))
with open(os.path.join(GAME, r"_coop\partner_src\build_log.txt"), "w", encoding="utf-8") as fh:
    fh.write("\n".join(log))
print("BUILD_DONE")
