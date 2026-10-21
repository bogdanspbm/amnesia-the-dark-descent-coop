# Minimal Collada 1.4 writer for HPL2 (Amnesia), used because Blender 5 no longer exports Collada.
# Writes exactly what HPL2's loader reads: Y_UP, one geometry + skin controller per material,
# joints named after the bones with a "transform" matrix, sampled matrix animations, and the
# FCOLLADA start/end time extra that sets the clip length.
from mathutils import Matrix, Vector
from xml.sax.saxutils import escape

# Blender is Z up, HPL Y up: rotate -90 degrees about X, so (x, y, z) -> (x, z, -y)
C = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))
C3 = C.to_3x3()
MAX_INFLUENCES = 4

def fmt(vals):
    return " ".join(f"{v:.6g}" for v in vals)

def mat_rows(m):
    return fmt([m[r][c] for r in range(4) for c in range(4)])

class Material:
    def __init__(self, name, texture_file):
        self.name = name; self.texture = texture_file

class Part:
    """One geometry: triangles of one material, optionally skinned."""
    def __init__(self, name, material):
        self.name = name; self.material = material
        self.positions = []; self.normals = []; self.uvs = []; self.tris = []  # tris: ((pi, ni, ti) * 3)
        self.weights = []  # per position: [(joint_index, weight)]

def parts_from_object(ob, materials_by_slot, joints, to_file=C, skinned=True, part_prefix=""):
    """Split a mesh object into one Part per material slot, in file space."""
    me = ob.data
    me.calc_loop_triangles()
    mw = ob.matrix_world
    nmat = (to_file.to_3x3() @ mw.to_3x3()).inverted().transposed()
    uvl = me.uv_layers.active.data if me.uv_layers.active else None
    gname = {g.index: g.name for g in ob.vertex_groups}
    jidx = {n: i for i, n in enumerate(joints)} if joints else {}
    parts = {}
    for tri in me.loop_triangles:
        slot = tri.material_index
        if slot not in parts:
            parts[slot] = (Part(part_prefix + materials_by_slot[slot].name, materials_by_slot[slot]), {}, {}, {})
        part, pmap, nmap, tmap = parts[slot]
        corner = []
        for vi, li in zip(tri.vertices, tri.loops):
            if vi not in pmap:
                pmap[vi] = len(part.positions)
                part.positions.append(to_file @ (mw @ me.vertices[vi].co))
                if skinned:
                    infl = []
                    for g in me.vertices[vi].groups:
                        n = gname.get(g.group)
                        if n in jidx and g.weight > 1e-4: infl.append((jidx[n], g.weight))
                    infl.sort(key=lambda x: -x[1]); infl = infl[:MAX_INFLUENCES]
                    tot = sum(w for _, w in infl) or 1.0
                    part.weights.append([(j, w / tot) for j, w in infl])
            n = (nmat @ me.corner_normals[li].vector).normalized()
            nk = (round(n.x, 4), round(n.y, 4), round(n.z, 4))
            if nk not in nmap: nmap[nk] = len(part.normals); part.normals.append(n)
            uv = uvl[li].uv if uvl else (0.0, 0.0)
            tk = (round(uv[0], 5), round(uv[1], 5))
            if tk not in tmap: tmap[tk] = len(part.uvs); part.uvs.append(tk)
            corner.append((pmap[vi], nmap[nk], tmap[tk]))
        part.tris.append(corner)
    return [p[0] for p in parts.values()]

def write(path, parts, joints=None, rest_world=None, parents=None, anim=None, fps=30):
    """
    joints: bone names in parent-first order; rest_world[name]: rest matrix in Blender armature space;
    parents[name]: parent name or None. anim: list of frames, each {name: pose matrix in armature space}.
    """
    out = []
    w = out.append
    w('<?xml version="1.0" encoding="utf-8"?>')
    w('<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">')
    w('  <asset><contributor><authoring_tool>coop_collada.py (Blender 5)</authoring_tool></contributor>'
      '<unit name="meter" meter="1"/><up_axis>Y_UP</up_axis></asset>')

    mats = []
    for p in parts:
        if p.material not in mats: mats.append(p.material)
    w('  <library_images>')
    for m in mats:
        w(f'    <image id="{m.name}-image" name="{m.name}-image"><init_from>{escape(m.texture)}</init_from></image>')
    w('  </library_images>')
    w('  <library_effects>')
    for m in mats:
        w(f'    <effect id="{m.name}-effect"><profile_COMMON>'
          f'<newparam sid="{m.name}-surface"><surface type="2D"><init_from>{m.name}-image</init_from></surface></newparam>'
          f'<newparam sid="{m.name}-sampler"><sampler2D><source>{m.name}-surface</source></sampler2D></newparam>'
          f'<technique sid="common"><lambert><diffuse><texture texture="{m.name}-sampler" texcoord="UVMap"/></diffuse></lambert></technique>'
          f'</profile_COMMON></effect>')
    w('  </library_effects>')
    w('  <library_materials>')
    for m in mats:
        w(f'    <material id="{m.name}-material" name="{m.name}"><instance_effect url="#{m.name}-effect"/></material>')
    w('  </library_materials>')

    w('  <library_geometries>')
    for p in parts:
        g = p.name
        pos = [c for v in p.positions for c in v]
        nor = [c for v in p.normals for c in v]
        uv = [c for t in p.uvs for c in t]
        w(f'    <geometry id="{g}-mesh" name="{g}"><mesh>')
        w(f'      <source id="{g}-positions"><float_array id="{g}-positions-array" count="{len(pos)}">{fmt(pos)}</float_array>'
          f'<technique_common><accessor source="#{g}-positions-array" count="{len(p.positions)}" stride="3">'
          f'<param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>')
        w(f'      <source id="{g}-normals"><float_array id="{g}-normals-array" count="{len(nor)}">{fmt(nor)}</float_array>'
          f'<technique_common><accessor source="#{g}-normals-array" count="{len(p.normals)}" stride="3">'
          f'<param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>')
        w(f'      <source id="{g}-map"><float_array id="{g}-map-array" count="{len(uv)}">{fmt(uv)}</float_array>'
          f'<technique_common><accessor source="#{g}-map-array" count="{len(p.uvs)}" stride="2">'
          f'<param name="S" type="float"/><param name="T" type="float"/></accessor></technique_common></source>')
        w(f'      <vertices id="{g}-vertices"><input semantic="POSITION" source="#{g}-positions"/></vertices>')
        idx = [i for tri in p.tris for c in tri for i in c]
        w(f'      <triangles material="{p.material.name}-material" count="{len(p.tris)}">'
          f'<input semantic="VERTEX" source="#{g}-vertices" offset="0"/>'
          f'<input semantic="NORMAL" source="#{g}-normals" offset="1"/>'
          f'<input semantic="TEXCOORD" source="#{g}-map" offset="2" set="0"/>'
          f'<p>{" ".join(str(i) for i in idx)}</p></triangles>')
        w('    </mesh></geometry>')
    w('  </library_geometries>')

    skinned = joints is not None
    if skinned:
        w('  <library_controllers>')
        inv_bind = [(C @ rest_world[j]).inverted() for j in joints]
        for p in parts:
            g = p.name
            weights = []; vcount = []; v = []
            for infl in p.weights:
                vcount.append(len(infl))
                for j, wt in infl:
                    v += [j, len(weights)]; weights.append(wt)
            w(f'    <controller id="{g}-skin" name="{g}-skin"><skin source="#{g}-mesh">')
            w('      <bind_shape_matrix>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</bind_shape_matrix>')
            w(f'      <source id="{g}-joints"><Name_array id="{g}-joints-array" count="{len(joints)}">{" ".join(joints)}</Name_array>'
              f'<technique_common><accessor source="#{g}-joints-array" count="{len(joints)}" stride="1"><param name="JOINT" type="name"/></accessor></technique_common></source>')
            mats_flat = " ".join(mat_rows(m) for m in inv_bind)
            w(f'      <source id="{g}-bind_poses"><float_array id="{g}-bind_poses-array" count="{16*len(joints)}">{mats_flat}</float_array>'
              f'<technique_common><accessor source="#{g}-bind_poses-array" count="{len(joints)}" stride="16"><param name="TRANSFORM" type="float4x4"/></accessor></technique_common></source>')
            w(f'      <source id="{g}-weights"><float_array id="{g}-weights-array" count="{len(weights)}">{fmt(weights)}</float_array>'
              f'<technique_common><accessor source="#{g}-weights-array" count="{len(weights)}" stride="1"><param name="WEIGHT" type="float"/></accessor></technique_common></source>')
            w(f'      <joints><input semantic="JOINT" source="#{g}-joints"/><input semantic="INV_BIND_MATRIX" source="#{g}-bind_poses"/></joints>')
            w(f'      <vertex_weights count="{len(p.weights)}"><input semantic="JOINT" source="#{g}-joints" offset="0"/>'
              f'<input semantic="WEIGHT" source="#{g}-weights" offset="1"/>'
              f'<vcount>{" ".join(str(c) for c in vcount)}</vcount><v>{" ".join(str(i) for i in v)}</v></vertex_weights>')
            w('    </skin></controller>')
        w('  </library_controllers>')

    def local_rest(j):
        par = parents[j]
        return C @ rest_world[j] if par is None else rest_world[par].inverted() @ rest_world[j]

    if skinned and anim:
        w('  <library_animations>')
        times = [i / fps for i in range(len(anim))]
        for j in joints:
            par = parents[j]
            mats_j = []
            for fr in anim:
                m = C @ fr[j] if par is None else fr[par].inverted() @ fr[j]
                mats_j.append(mat_rows(m))
            a = f"{j}-anim"
            w(f'    <animation id="{a}">')
            w(f'      <source id="{a}-input"><float_array id="{a}-input-array" count="{len(times)}">{fmt(times)}</float_array>'
              f'<technique_common><accessor source="#{a}-input-array" count="{len(times)}" stride="1"><param name="TIME" type="float"/></accessor></technique_common></source>')
            w(f'      <source id="{a}-output"><float_array id="{a}-output-array" count="{16*len(times)}">{" ".join(mats_j)}</float_array>'
              f'<technique_common><accessor source="#{a}-output-array" count="{len(times)}" stride="16"><param name="TRANSFORM" type="float4x4"/></accessor></technique_common></source>')
            w(f'      <source id="{a}-interp"><Name_array id="{a}-interp-array" count="{len(times)}">{" ".join(["LINEAR"]*len(times))}</Name_array>'
              f'<technique_common><accessor source="#{a}-interp-array" count="{len(times)}" stride="1"><param name="INTERPOLATION" type="name"/></accessor></technique_common></source>')
            w(f'      <sampler id="{a}-sampler"><input semantic="INPUT" source="#{a}-input"/><input semantic="OUTPUT" source="#{a}-output"/>'
              f'<input semantic="INTERPOLATION" source="#{a}-interp"/></sampler>')
            w(f'      <channel source="#{a}-sampler" target="{j}/transform"/>')
            w('    </animation>')
        w('  </library_animations>')

    w('  <library_visual_scenes><visual_scene id="Scene" name="Scene">')
    if skinned:
        root = joints[0]
        def node(j, depth):
            ind = "  " * depth
            w(f'{ind}<node id="{j}" name="{j}" sid="{j}" type="JOINT"><matrix sid="transform">{mat_rows(local_rest(j))}</matrix>')
            for c in joints:
                if parents[c] == j: node(c, depth + 1)
            w(f'{ind}</node>')
        for j in joints:
            if parents[j] is None: node(j, 2)
    for p in parts:
        g = p.name
        inst = f'<bind_material><technique_common><instance_material symbol="{p.material.name}-material" target="#{p.material.name}-material">' \
               f'<bind_vertex_input semantic="UVMap" input_semantic="TEXCOORD" input_set="0"/></instance_material></technique_common></bind_material>'
        w(f'    <node id="{g}" name="{g}" type="NODE"><matrix sid="transform">1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</matrix>')
        if skinned:
            w(f'      <instance_controller url="#{g}-skin"><skeleton>#{joints[0]}</skeleton>{inst}</instance_controller>')
        else:
            w(f'      <instance_geometry url="#{g}-mesh">{inst}</instance_geometry>')
        w('    </node>')
    end = (len(anim) - 1) / fps if anim else 0
    w(f'    <extra><technique profile="FCOLLADA"><start_time>0</start_time><end_time>{end:.6g}</end_time></technique></extra>')
    w('  </visual_scene></library_visual_scenes>')
    w('  <scene><instance_visual_scene url="#Scene"/></scene>')
    w('</COLLADA>')
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(out))
