import struct
import unittest
from surfaces import Surfaces,hit,transform
from data import GameData

def chunk(tag,data): return tag+struct.pack('<I',len(data))+data

class SurfaceTests(unittest.TestCase):
    def test_rotated_placement(self):
        origin=32*1600/3
        self.assertEqual(transform((2,3,4),(origin,10,origin),(0,90,0),2),(6,-4,18))
        self.assertAlmostEqual(transform((2,3,4),(origin,10,origin),(0,0,0),1)[0],-2)

    def test_triangle_height_and_outside(self):
        self.assertEqual(hit((0,0,10),(4,0,10),(0,4,10),1,1),(10,1))
        self.assertIsNone(hit((0,0,10),(4,0,10),(0,4,10),5,5))

    def test_collision_levels_and_headroom(self):
        origin=32*1600/3; pos=(origin,0,origin)
        placement=struct.pack('<II3f3f6f4H',0,1,*pos,0,90,0,origin-10,-1,origin-10,origin+10,50,origin+10,0,0,0,0)
        adt=chunk(b'OMWM',b'test.wmo\0')+chunk(b'DIWM',struct.pack('<I',0))+chunk(b'FDOM',placement)
        root=chunk(b'IGOM',struct.pack('<I6fi',0,-10,-10,-1,10,10,50,0))
        verts=[]; tris=[]; flags=[]
        # Lower floor, ceiling too low, valid street, roof, non-solid decorative card.
        for z,f in [(0,8),(1,8),(10,8),(30,8),(40,4)]:
            i=len(verts); verts += [(-10,-10,z),(10,-10,z),(0,10,z)]
            tris.append((i,i+1,i+2)); flags.append((f,0))
        group=chunk(b'PGOM',bytes(68)+chunk(b'TVOM',b''.join(struct.pack('<3f',*v) for v in verts))+
                    chunk(b'IVOM',b''.join(struct.pack('<3H',*v) for v in tris))+
                    chunk(b'YPOM',b''.join(bytes(f) for f in flags)))
        class Archive:
            def read(self,name): return {'test.wmo':root,'test_000.wmo':group}[name]
        self.assertEqual(Surfaces(Archive()).floors(adt,0,0),[1,10,30])
        self.assertEqual(Surfaces(Archive()).floors(adt,100,100),[])

    def test_reported_stormwind_point(self):
        game=GameData('E:/WowUnreal')
        try:
            x,y=-8545.063,474.9505
            terrain=game.ground(0,x,y); floors=game.ground_options(0,x,y)
            self.assertAlmostEqual(terrain['z'],60.4569,places=2)
            self.assertEqual(len(floors),1)
            self.assertAlmostEqual(floors[0]['z'],103.6716,places=2)
            self.assertGreater(floors[0]['z']-terrain['z'],40)
            self.assertEqual(game.ground_options(0,-8949,-132),[game.ground(0,-8949,-132)])
        finally: game.mpq.close()

if __name__=='__main__': unittest.main(verbosity=2)
