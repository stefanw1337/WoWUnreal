import json
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch
from data import LiveBridge

class LiveEditTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.bridge=LiveBridge(self.temp.name)
        self.file=Path(self.temp.name)/'OfflineCharacters.json'
        self.bridge.enable()
    def tearDown(self): self.temp.cleanup()
    def status(self,**changes):
        record=dict(session='test',timestamp=time.time(),offline=True,ready=False,canEditRoster=True,characterFile=str(self.file))
        record.update(changes)
        (self.bridge.directory/'status.json').write_text(json.dumps(record))
    def test_enables_bridge(self): self.assertTrue((self.bridge.directory/'enabled.txt').exists())
    @patch('data.client_running',return_value=True)
    def test_client_out_of_world_can_save(self,_):
        for changes in [dict(),dict(canEditRoster=False),dict(offline=False),dict(timestamp=time.time()-6)]:
            self.status(**changes); self.assertFalse(self.bridge.editing_blocked(self.file,1))
    @patch('data.client_running',return_value=True)
    def test_character_in_world_rejected_only_for_itself(self,_):
        self.status(guid=1,ready=True,canEditRoster=False)
        self.assertTrue(self.bridge.editing_blocked(self.file,1))
        self.assertFalse(self.bridge.editing_blocked(self.file,2))
        self.assertFalse(self.bridge.editing_blocked(self.file.with_name('other.json'),1))
    @patch('data.client_running',return_value=True)
    def test_client_status_in_play_copy_is_found(self,_):
        bridge=LiveBridge(self.temp.name,Path(self.temp.name)/'dist-play'); bridge.enable()
        record=dict(session='s',timestamp=time.time(),offline=True,ready=True,guid=1,characterFile=str(self.file))
        (Path(self.temp.name)/'dist-play'/'WowUnreal'/'PrivateTools/OfflineCharacterEditor/Bridge'/'status.json').write_text(json.dumps(record))
        self.assertEqual(bridge.status()['guid'],1); self.assertTrue(bridge.editing_blocked(self.file,1))
    @patch('data.client_running',return_value=False)
    def test_closed_client_can_save(self,_):
        self.assertFalse(self.bridge.editing_blocked(self.file))
    def test_live_move_requires_ready_character(self):
        self.status()
        with self.assertRaises(ValueError): self.bridge.teleport(1,dict(map=0,x=1,y=2,z=3))
        self.status(guid=1,ready=True,canEditRoster=False)
        request=self.bridge.teleport(1,dict(map=0,x=1,y=2,z=3))
        self.assertEqual(json.loads((self.bridge.directory/'request.json').read_text())['id'],request)

if __name__=='__main__': unittest.main()
