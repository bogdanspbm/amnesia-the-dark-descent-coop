# The partner's look: everyday proportions, clothes as their own layers over the body, and textures.
# Skin, hair and eye textures come from the CC0 Quaternius pack; the cloth and leather textures and
# their normal maps are generated here.
import bpy, bmesh, os, math
import numpy as np
from mathutils import Vector, Matrix

TILE = 0.25			# metres of cloth one texture repeat covers
TEX = 512

MAT_XML = """<Material>
    <Main DepthTest="True" PhysicsMaterial="Default" Type="SolidDiffuse" />
    <TextureUnits>
        <Diffuse AnimFrameTime="" AnimMode="" File="{tex}" Mipmaps="true" Type="2D" Wrap="Repeat" />{nmap}
    </TextureUnits>
    <SpecificVariables />
</Material>
"""
NMAP_XML = '\n        <NMap AnimFrameTime="" AnimMode="" File="{tex}" Mipmaps="true" Type="2D" Wrap="Repeat" />'

# Bones whose flesh is pulled in towards the bone, and how much (1 = unchanged)
SLIM = {
    "upperarm_l": 0.82, "upperarm_r": 0.82, "lowerarm_l": 0.88, "lowerarm_r": 0.88,
    "clavicle_l": 0.88, "clavicle_r": 0.88, "spine_03": 0.88, "spine_02": 0.92, "spine_01": 0.95,
    "neck_01": 0.9, "thigh_l": 0.92, "thigh_r": 0.92, "calf_l": 0.94, "calf_r": 0.94,
}

BELT = (0.025, 0.03, 0.02)	# below and above the waistline, offset from the body; it covers the shirt/trousers seam

GARMENTS = {
    # key: (offset from the body in metres, base colour, texture kind, smoothing passes)
    # Smoothing irons the muscles (and toes) out of the copied body surface, so it reads as cloth and leather
    "shirt": (0.007, (0.80, 0.76, 0.68), "linen", 6),
    "trousers": (0.013, (0.24, 0.21, 0.18), "twill", 10),
    "waistcoat": (0.016, (0.23, 0.15, 0.10), "wool", 10),
    "boots": (0.014, (0.11, 0.08, 0.06), "leather", 40),
}

# ----------------------------------------------------------------------------------------------
# Textures

def _periodic_noise(rng, size, scale):
    """Tileable smooth noise: white noise low-passed in the frequency domain."""
    f = np.fft.fft2(rng.standard_normal((size, size)))
    fy = np.fft.fftfreq(size)[:, None]; fx = np.fft.fftfreq(size)[None, :]
    r = np.sqrt(fx * fx + fy * fy) * size
    f *= np.exp(-(r / scale) ** 2)
    n = np.real(np.fft.ifft2(f))
    return (n - n.mean()) / (n.std() + 1e-9)

def _cloth_height(kind, rng):
    s = TEX
    v, u = np.mgrid[0:s, 0:s] / s
    low = _periodic_noise(rng, s, 6); mid = _periodic_noise(rng, s, 40); hi = _periodic_noise(rng, s, 160)
    if kind == "linen":
        warp = np.sin(2 * np.pi * 56 * u); weft = np.sin(2 * np.pi * 56 * v)
        h = 0.35 * (warp + weft) + 0.25 * mid + 0.2 * hi
    elif kind == "twill":
        h = 0.45 * np.sin(2 * np.pi * 72 * (u + v)) + 0.25 * mid + 0.15 * hi
    elif kind == "wool":
        herring = np.sin(2 * np.pi * 48 * (u + np.where((np.floor(v * 12) % 2) == 0, v, -v)))
        h = 0.35 * herring + 0.35 * mid + 0.3 * hi
    else:  # leather
        cells = np.abs(_periodic_noise(rng, s, 30)) * -1.0
        h = 0.6 * cells + 0.25 * mid + 0.15 * hi
    return h, low

def _normal_from_height(h, strength):
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5
    n = np.stack([-dx * strength, -dy * strength, np.ones_like(h)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n * 0.5 + 0.5

def _save(name, rgb, out_dir):
    s = rgb.shape[0]
    img = bpy.data.images.new(name, s, s, alpha=False)
    px = np.concatenate([np.clip(rgb, 0, 1), np.ones((s, s, 1))], axis=-1).astype(np.float32)
    img.pixels.foreach_set(px.ravel())
    img.filepath_raw = os.path.join(out_dir, name + ".png"); img.file_format = "PNG"; img.save()
    return img

def _cloth_textures(prefix, key, base, kind, out_dir, rng):
    h, low = _cloth_height(kind, rng)
    shade = 1.0 + 0.10 * np.clip(h, -2, 2) + 0.06 * low
    rgb = np.array(base)[None, None, :] * shade[..., None]
    _save(prefix + key, rgb, out_dir)
    _save(prefix + key + "_nrm", _normal_from_height(h, {"linen": 2.0, "twill": 2.5, "wool": 3.0, "leather": 3.0}[kind]), out_dir)
    return prefix + key + ".png", prefix + key + "_nrm.png"

def _copy_texture(src, name, out_dir, size=1024, tint=None):
    img = bpy.data.images.load(src, check_existing=False)
    img.scale(size, size)
    if tint is not None:
        px = np.empty(size * size * 4, dtype=np.float32); img.pixels.foreach_get(px)
        px = px.reshape(-1, 4); px[:, :3] *= np.array(tint, dtype=np.float32); img.pixels.foreach_set(px.ravel())
    img.filepath_raw = os.path.join(out_dir, name + ".png"); img.file_format = "PNG"; img.save()
    return name + ".png"

def _write_mat(out_dir, name, tex, nmap):
    with open(os.path.join(out_dir, name + ".mat"), "w", encoding="utf-8") as fh:
        fh.write(MAT_XML.format(tex=tex, nmap=NMAP_XML.format(tex=nmap) if nmap else ""))

def _blender_mat(name):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    return m

# ----------------------------------------------------------------------------------------------
# Geometry helpers

def _bone_seg(arm, name):
    b = arm.data.bones[name]
    return arm.matrix_world @ b.head_local, arm.matrix_world @ b.tail_local

def _closest_on_seg(p, a, b):
    ab = b - a; t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-9)))
    return a + ab * t

def slim(ob, arm, say):
    """Pull the flesh around the listed bones in towards the bone, weighted by skinning weights."""
    mw = ob.matrix_world; inv = mw.inverted()
    segs = {n: _bone_seg(arm, n) for n in SLIM if n in arm.data.bones}
    gname = {g.index: g.name for g in ob.vertex_groups}
    moved = 0
    for v in ob.data.vertices:
        p = mw @ v.co
        tot = sum(g.weight for g in v.groups) or 1.0
        d = Vector((0, 0, 0))
        for g in v.groups:
            n = gname.get(g.group)
            if n not in segs: continue
            c = _closest_on_seg(p, *segs[n])
            d += ((c + (p - c) * SLIM[n]) - p) * (g.weight / tot)
        if d.length_squared > 0:
            v.co = inv @ (p + d); moved += 1
    say(f"slimmed {moved} vertices of {ob.name}")

class Landmarks:
    def __init__(self, arm):
        w = lambda n: arm.matrix_world @ arm.data.bones[n].head_local
        self.waist = w("pelvis").z + 0.11
        self.boot = w("foot_l").z + (w("calf_l").z - w("foot_l").z) * 0.42
        self.neck = w("neck_01").z + 0.035
        self.shoulder = w("clavicle_l").z + 0.02
        self.v_bottom = w("spine_03").z - 0.02
        # Armholes: the waistcoat stops this far out from the middle
        self.armhole_x = abs(w("upperarm_l").x) - 0.05
        self.wrists = []
        for side in ("l", "r"):
            hand = w(f"hand_{side}"); fore = w(f"lowerarm_{side}")
            self.wrists.append((hand, (hand - fore).normalized()))

def _plane_side(c, co, no):
    # Only near the hand: the wrist plane is infinite and would otherwise also cut through the legs
    if (c - co).length > 0.28: return -1.0
    return (c - co).dot(no)

def _region(c, normal_w, arm_weight, lm, front):
    """Which garment a point of the body belongs to ("skin" for bare parts)."""
    for co, no in lm.wrists:
        if _plane_side(c, co, no) > -0.012: return "skin"
    if c.z > lm.neck: return "skin"
    if c.z < lm.boot: return "boots"
    if c.z < lm.waist: return "trousers"
    return "shirt"

def _in_waistcoat(c, arm_weight, lm, front):
    if abs(c.x) > lm.armhole_x: return False
    if c.z < lm.waist - 0.07 or c.z > lm.shoulder: return False
    # V neck and open front below it
    if (c.y * front) > 0 and c.z > lm.v_bottom and abs(c.x) < (c.z - lm.v_bottom) * 0.55 + 0.01: return False
    return True

def _arm_weight(v, gname):
    tot = sum(g.weight for g in v.groups) or 1.0
    a = sum(g.weight for g in v.groups if gname.get(g.group, "").startswith(("clavicle", "upperarm", "lowerarm", "hand", "index", "middle", "ring", "pinky", "thumb")))
    return a / tot

def _cut(bm, mw, lm):
    """Straight seams: split the faces along the garment edges."""
    inv = mw.inverted(); n3 = mw.to_3x3().transposed()  # world normal -> local normal for the plane
    planes = [(Vector((0, 0, z)), Vector((0, 0, 1))) for z in (lm.waist, lm.waist - 0.07, lm.boot, lm.neck, lm.shoulder)]
    planes += [(co - no * 0.012, no) for co, no in lm.wrists]
    planes += [(Vector((x, 0, 0)), Vector((1, 0, 0))) for x in (lm.armhole_x, -lm.armhole_x)]
    planes += [(Vector((0, 0, lm.v_bottom)), Vector((0, 0, 1)))]
    # The two edges of the V neck: |x| = (z - v_bottom) * 0.55 + 0.01
    planes += [(Vector((s * 0.01, 0, lm.v_bottom)), Vector((s, 0, -0.55)).normalized()) for s in (1, -1)]
    # The belt's edges
    planes += [(Vector((0, 0, z)), Vector((0, 0, 1))) for z in (lm.waist - BELT[0], lm.waist + BELT[1])]
    for co, no in planes:
        geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
        bmesh.ops.bisect_plane(bm, geom=geom, plane_co=inv @ co, plane_no=(n3 @ no).normalized(), dist=0.0005)

def _cube_uv(bm, mw):
    uvl = bm.loops.layers.uv.active or bm.loops.layers.uv.new("UVMap")
    rot = mw.to_3x3()
    for f in bm.faces:
        n = rot @ f.normal; ax = max(range(3), key=lambda i: abs(n[i]))
        a, b = [(1, 2), (0, 2), (0, 1)][ax]
        for l in f.loops:
            p = mw @ l.vert.co
            l[uvl].uv = (p[a] / TILE, p[b] / TILE)

def make_garment(body, arm, key, lm, front, keep, offset, mat, say, smooth=0):
    """A copy of the body's faces in one region, pushed out along the normals."""
    ob = body.copy(); ob.data = body.data.copy(); ob.name = "coop_partner_" + key
    bpy.context.scene.collection.objects.link(ob)
    me = ob.data; mw = ob.matrix_world
    gname = {g.index: g.name for g in ob.vertex_groups}
    bm = bmesh.new(); bm.from_mesh(me)
    _cut(bm, mw, lm)
    bm.faces.ensure_lookup_table()
    dl = bm.verts.layers.deform.active
    def aw(vert):
        tot = sum(vert[dl].values()) or 1.0
        return sum(w for gi, w in vert[dl].items() if gname.get(gi, "").startswith(("clavicle", "upperarm", "lowerarm", "hand", "index", "middle", "ring", "pinky", "thumb"))) / tot
    gone = [f for f in bm.faces if not keep(mw @ f.calc_center_median(), sum(aw(v) for v in f.verts) / len(f.verts))]
    bmesh.ops.delete(bm, geom=gone, context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    # Iron out the body's detail; the seams stay put so neighbouring garments still meet
    inner = [v for v in bm.verts if not v.is_boundary]
    for _ in range(smooth):
        bmesh.ops.smooth_vert(bm, verts=inner, factor=0.5, use_axis_x=True, use_axis_y=True, use_axis_z=True)
    bm.normal_update()
    for v in bm.verts: v.co += v.normal * (offset / max(mw.to_scale().x, 1e-6))
    _cube_uv(bm, mw)
    bm.to_mesh(me); bm.free()
    me.materials.clear(); me.materials.append(mat)
    for p in me.polygons: p.material_index = 0
    say(f"garment {key}: {len(me.polygons)} faces")
    return ob

def trim_body(body, lm, front, say):
    """Remove skin that is well inside the clothes, keeping a margin at every seam."""
    me = body.data; mw = body.matrix_world
    gname = {g.index: g.name for g in body.vertex_groups}
    bm = bmesh.new(); bm.from_mesh(me)
    m = 0.04
    def covered(c):
        for co, no in lm.wrists:
            if _plane_side(c, co, no) > -0.012 - m: return False
        if c.z > lm.neck - m: return False
        if abs(c.z - lm.boot) < m or abs(c.z - lm.waist) < m: return False
        return True
    gone = [f for f in bm.faces if covered(mw @ f.calc_center_median())]
    bmesh.ops.delete(bm, geom=gone, context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    bm.to_mesh(me); bm.free()
    say(f"body skin kept: {len(me.polygons)} faces")

# ----------------------------------------------------------------------------------------------

def build(arm, body, hair, brows, eyes, out_dir, prefix, ubc, lantern_colors, say):
    os.makedirs(out_dir, exist_ok=True)
    rng = np.random.default_rng(11)
    tex = ubc + r"\Base Characters\Textures"
    mats = {}; files = {}

    def mat(key, diffuse, nmap):
        m = _blender_mat(prefix + key); mats[key] = m; files[key] = (diffuse, nmap)
        _write_mat(out_dir, prefix + key, diffuse, nmap)
        return m

    # Skin, hair and eyes from the pack
    mat("skin", _copy_texture(tex + r"\T_Superhero_Male_Ligh.png", prefix + "skin", out_dir),
                _copy_texture(tex + r"\Normals Unity - Godot\T_Superhero_Male_Normal.png", prefix + "skin_nrm", out_dir))
    mat("hair", _copy_texture(tex + r"\T_Hair_1_BaseColor.png", prefix + "hair", out_dir, tint=(0.42, 0.27, 0.16)),
                _copy_texture(tex + r"\Normals Unity - Godot\T_Hair_1_Normal.png", prefix + "hair_nrm", out_dir))
    mat("eyes", _copy_texture(tex + r"\T_Eye_Brown.png", prefix + "eyes", out_dir, size=256),
                _copy_texture(tex + r"\Normals Unity - Godot\T_Eye_Normal.png", prefix + "eyes_nrm", out_dir, size=256))
    # Cloth and leather
    for key, (_, base, kind, _s) in GARMENTS.items():
        mat(key, *_cloth_textures(prefix, key, base, kind, out_dir, rng))
    # Lantern: flat colours
    for key, col in lantern_colors.items():
        img = _save(prefix + key, np.ones((64, 64, 3)) * np.array(col)[None, None, :], out_dir)
        mat(key, prefix + key + ".png", None)

    slim(body, arm, say)

    # The character faces +Y in the rig's world (checked in the renders)
    front = 1.0
    lm = Landmarks(arm)
    say(f"landmarks: waist {lm.waist:.3f} boot {lm.boot:.3f} neck {lm.neck:.3f} shoulder {lm.shoulder:.3f} V {lm.v_bottom:.3f}")

    garments = []
    for key in ("shirt", "trousers", "boots"):
        # The belt covers the shirt/trousers seam, so the shirt is not tucked in (a tucked part pokes through)
        keep = (lambda k: (lambda c, a: _region(c, None, a, lm, front) == k))(key)
        garments.append(make_garment(body, arm, key, lm, front, keep, GARMENTS[key][0], mats[key], say, GARMENTS[key][3]))
    garments.append(make_garment(body, arm, "waistcoat", lm, front,
                                 lambda c, a: _region(c, None, a, lm, front) == "shirt" and _in_waistcoat(c, a, lm, front),
                                 GARMENTS["waistcoat"][0], mats["waistcoat"], say, GARMENTS["waistcoat"][3]))
    # A leather belt over the shirt/trousers seam (same leather as the boots); arms that hang to the hips are left out
    belt = make_garment(body, arm, "belt", lm, front,
                        lambda c, a: a < 0.3 and lm.waist - BELT[0] < c.z < lm.waist + BELT[1] and _region(c, None, a, lm, front) != "skin",
                        BELT[2], mats["boots"], say, 4)
    garments.append(belt)

    trim_body(body, lm, front, say)
    for ob, key in ((body, "skin"), (hair, "hair"), (brows, "hair"), (eyes, "eyes")):
        ob.data.materials.clear(); ob.data.materials.append(mats[key])
        for p in ob.data.polygons: p.material_index = 0
        if not ob.data.uv_layers: ob.data.uv_layers.new()
        ob.data.uv_layers.active_index = 0

    meshes = [(body, "coop_partner_body_"), (hair, "coop_partner_hair_"), (brows, "coop_partner_brows_"), (eyes, "coop_partner_eyes_")]
    meshes += [(g, "coop_partner_" + g.name[len("coop_partner_"):] + "_") for g in garments]
    return {"mats": mats, "files": files, "meshes": meshes}
