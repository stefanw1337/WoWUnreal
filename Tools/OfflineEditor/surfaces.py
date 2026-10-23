"""Read authored WMO collision for explicit offline map placement (WoW units)."""
import math
import struct
from itertools import product

def chunks(raw):
    p=0
    while p+8<=len(raw):
        tag,n=struct.unpack_from('<4sI',raw,p)
        if p+8+n>len(raw): raise ValueError('Corrupt building model')
        yield tag,raw[p+8:p+8+n]
        p+=8+n

def transform(v,pos,rot,scale):
    # WMO -> ADT (X,Z,-Y), then authored YZX rotation, then WoW world.
    x,y,z=v[0]*scale,v[2]*scale,-v[1]*scale
    a,b,c=map(math.radians,(rot[2],-rot[0],rot[1]-90))
    y,z=y*math.cos(a)-z*math.sin(a),y*math.sin(a)+z*math.cos(a)
    x,y=x*math.cos(b)-y*math.sin(b),x*math.sin(b)+y*math.cos(b)
    x,z=x*math.cos(c)+z*math.sin(c),-x*math.sin(c)+z*math.cos(c)
    origin=32*1600/3
    return origin-(pos[2]+z),origin-(pos[0]+x),pos[1]+y

def intersects(bounds,x,y):
    return min(v[0] for v in bounds)-.4<=x<=max(v[0] for v in bounds)+.4 and min(v[1] for v in bounds)-.4<=y<=max(v[1] for v in bounds)+.4

def hit(a,b,c,x,y):
    ux,uy,uz=(b[i]-a[i] for i in range(3)); vx,vy,vz=(c[i]-a[i] for i in range(3))
    nz=ux*vy-uy*vx
    if abs(nz)<1e-8: return None
    u=((x-a[0])*vy-(y-a[1])*vx)/nz
    v=(ux*(y-a[1])-uy*(x-a[0]))/nz
    if min(u,v,1-u-v)<-1e-5: return None
    normal=abs(nz)/math.sqrt((uy*vz-uz*vy)**2+(uz*vx-ux*vz)**2+nz*nz)
    return a[2]+u*uz+v*vz,normal

class Surfaces:
    def __init__(self,mpq): self.mpq=mpq; self.cache={}
    def read(self,name):
        if name not in self.cache:
            if len(self.cache)>96: self.cache.clear()
            self.cache[name]=self.mpq.read(name)
        return self.cache[name]
    def floors(self,adt,x,y):
        parts=dict(chunks(adt)); names=parts.get(b'OMWM',b''); ids=parts.get(b'DIWM',b''); placements=parts.get(b'FDOM',b'')
        surfaces=[]; seen=set()
        for off in range(0,len(placements),64):
            name_id,uid=struct.unpack_from('<II',placements,off)
            if uid in seen: continue
            seen.add(uid)
            pos=struct.unpack_from('<3f',placements,off+8); rot=struct.unpack_from('<3f',placements,off+20)
            bounds=struct.unpack_from('<6f',placements,off+32)
            origin=32*1600/3
            if not(origin-bounds[5]-.4<=x<=origin-bounds[2]+.4 and origin-bounds[3]-.4<=y<=origin-bounds[0]+.4): continue
            scale=struct.unpack_from('<H',placements,off+62)[0]/1024 or 1
            idx=struct.unpack_from('<I',ids,name_id*4)[0]; name=names[idx:names.index(b'\0',idx)].decode()
            root=dict(chunks(self.read(name))); groups=root.get(b'IGOM',b'')
            for group in range(len(groups)//32):
                bb=struct.unpack_from('<6f',groups,group*32+4)
                corners=[transform(v,pos,rot,scale) for v in product(*[(bb[i],bb[i+3]) for i in range(3)])]
                if not intersects(corners,x,y): continue
                raw=dict(chunks(self.read(name[:-4]+f'_{group:03}.wmo')))[b'PGOM']
                sub=dict(chunks(raw[68:])); vertices=list(struct.iter_unpack('<3f',sub.get(b'TVOM',b'')))
                indices=list(struct.iter_unpack('<3H',sub.get(b'IVOM',b''))); flags=sub.get(b'YPOM',b'')
                if len(flags)!=len(indices)*2: raise ValueError('The building model has no readable collision data; select another location.')
                verts=[transform(v,pos,rot,scale) for v in vertices]
                for i,tri in enumerate(indices):
                    f,m=flags[i*2:i*2+2]
                    if not(f&8 or m==255 or (f&32 and not f&4)): continue
                    result=hit(*(verts[t] for t in tri),x,y)
                    if result: surfaces.append(result)
        # A floor needs standing headroom. Reject close ceilings, walls and steep faces.
        candidates=[]
        for z,n in sorted(surfaces):
            if n<.7 or any(z+.08<h<z+2.0 for h,_ in surfaces): continue
            if not candidates or abs(z-candidates[-1])>.1: candidates.append(z)
        return candidates
