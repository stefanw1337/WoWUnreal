import copy
import json
from pathlib import Path
import tempfile
import time
import tkinter as tk
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from data import CharacterStore, GameData, RACES
from editor import Editor, FIELDS

PROJECT=Path('E:/WowUnreal')
SAMPLE=[dict(guid=1,name='Tester',race=1,gender=0,skin=0,face=0,hairStyle=0,hairColor=0,facialHair=0,
             level=1,**{'class':1},map=0,zone=9,x=-8949,y=-132,z=84,unknown={'preserve':True}),
        dict(guid=2,name='Other',race=2,gender=1,level=8)]

class SaveTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); self.path=Path(self.temp.name)/'OfflineCharacters.json'
        self.path.write_text(json.dumps(SAMPLE)); self.store=CharacterStore(self.path)
    def tearDown(self): self.temp.cleanup()
    def test_save_backup_preserves_other_fields(self):
        before=self.path.read_bytes(); changed=copy.deepcopy(SAMPLE); changed[0].update(map=1,zone=14,x=-618,y=-4251,z=39)
        backup=self.store.save(changed,guard=lambda:False)
        self.assertEqual(backup.read_bytes(),before)
        result=json.loads(self.path.read_bytes()); self.assertEqual(result[1],SAMPLE[1]); self.assertEqual(result[0]['unknown'],SAMPLE[0]['unknown'])
        self.assertEqual(result[0]['map'],1)
    def test_rejects_running_client(self):
        before=self.path.read_bytes()
        with self.assertRaises(ValueError): self.store.save(SAMPLE,guard=lambda:True)
        self.assertEqual(self.path.read_bytes(),before)
    def test_rejects_external_changes(self):
        self.path.write_text('[]')
        with self.assertRaises(ValueError): self.store.save(SAMPLE,guard=lambda:False)
        self.assertEqual(self.path.read_text(),'[]')
    def test_rechecks_before_replace(self):
        calls=iter([False,True]); before=self.path.read_bytes()
        with self.assertRaises(ValueError): self.store.save(SAMPLE,guard=lambda:next(calls))
        self.assertEqual(self.path.read_bytes(),before)

class DataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.game=GameData(PROJECT)
    @classmethod
    def tearDownClass(cls): cls.game.mpq.close()
    def test_actual_northshire(self):
        p=self.game.ground(0,-8949,-132)
        self.assertEqual(p['zone'],9); self.assertTrue(83<p['z']<87)
        self.assertEqual(self.game.areas[9]['name'],'Northshire Valley')
    def test_continents_and_map_roundtrip(self):
        for area,map_id,x,y in [(12,0,-8949,-132),(14,1,-618,-4251),(3483,530,-248,-1029),(3537,571,2834,6186)]:
            z=next(z for z in self.game.zones if z['area']==area)
            self.assertEqual(z['map'],map_id); self.assertEqual(self.game.map_image(z).size,(1002,668))
            u,v=self.game.map_uv(z,x,y); xx,yy=self.game.map_position(z,u,v)
            self.assertAlmostEqual(xx,x); self.assertAlmostEqual(yy,y)
            p=self.game.ground(map_id,x,y); self.assertEqual(self.game.areas[p['zone']]['map'],map_id)
    def test_all_race_gender_options(self):
        for race in RACES:
            for gender in [0,1]:
                c=dict(race=race,gender=gender,skin=0,face=0,hairStyle=0,hairColor=0,facialHair=0)
                for key,_ in FIELDS:
                    opts=self.game.appearance(c,key); self.assertTrue(opts); self.assertTrue(all(0<=i<256 for i in opts)); c[key]=opts[0]
    def test_outside_world(self):
        with self.assertRaises(ValueError): self.game.ground(1,100000,100000)

class UiTests(unittest.TestCase):
    def test_selection_and_pin_without_touching_real_character(self):
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder)/'OfflineCharacters.json'; target.write_text(json.dumps([SAMPLE[0]]))
            before=target.read_bytes(); root=tk.Tk(); root.withdraw();
            with patch("editor.LiveBridge.enable"):
                app=Editor(root,PROJECT,target)
            try:
                limit=time.monotonic()+40
                with patch('editor.messagebox.showerror',side_effect=AssertionError),patch('editor.messagebox.showinfo',side_effect=AssertionError):
                    while app.char is None and time.monotonic()<limit: root.update(); time.sleep(.02)
                    self.assertIsNotNone(app.char); self.assertEqual(app.zone['area'],12)
                    app.values['continent'].set('Kalimdor'); app.continent_changed()
                    index=next(i for i,z in enumerate(app.mapped_zones) if z['area']==14)
                    app.widgets['zone'].current(index); app.zone_changed(); root.update()
                    # Force usable canvas layout while keeping the entire window hidden.
                    app.canvas.configure(width=750,height=500); app.canvas.update_idletasks(); app.draw_map()
                    ox,oy,w,h=app.rect; u,v=app.game.map_uv(app.zone,-618,-4251)
                    app.map_clicked(SimpleNamespace(x=ox+w*u,y=oy+h*v))
                    self.assertEqual(app.position['map'],1); self.assertTrue(app.dirty)
                    app.values['race'].set('Orc'); app.values['gender'].set('Female'); app.identity_changed()
                    self.assertEqual(app.char['race'],2); self.assertEqual(app.char['gender'],1)
                    app.northshire(); self.assertEqual(app.position['zone'],9)
                    self.assertEqual(target.read_bytes(),before)
            finally:
                app.dirty=False; app.close()

if __name__=='__main__': unittest.main(verbosity=2)
