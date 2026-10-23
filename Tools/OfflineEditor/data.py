"""Private, read-only WoW data access and transactional offline character editing."""
import ctypes as c
import csv
import datetime
import io
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import time
import uuid
from PIL import Image
from surfaces import Surfaces

RACES = {1:'Human',2:'Orc',3:'Dwarf',4:'Night Elf',5:'Undead',6:'Tauren',7:'Gnome',8:'Troll',10:'Blood Elf',11:'Draenei'}
CLASSES = {1:'Warrior',2:'Paladin',3:'Hunter',4:'Rogue',5:'Priest',6:'Death Knight',7:'Shaman',8:'Mage',9:'Warlock',11:'Druid'}
CONTINENTS = {0:'Eastern Kingdoms',1:'Kalimdor',530:'Outland',571:'Northrend'}
TILE = 1600/3

def chunks(raw):
    p = 0
    while p+8 <= len(raw):
        tag, size = struct.unpack_from('<4sI',raw,p)
        if p+8+size > len(raw): raise ValueError('Corrupt map file')
        yield tag, raw[p+8:p+8+size]
        p += 8+size

SETTINGS_FILES = ('User/Settings/GameUserSettings.ini', 'Settings/GameUserSettings.ini')

def settings_file(client):
    """The client's settings: User\\Settings (current builds), else the old Settings folder."""
    for rel in SETTINGS_FILES:
        path = Path(client)/rel
        if path.is_file(): return path
    return None

CLIENT_NAMES = ('dist-play', 'dist')

def last_used(folder):
    """When this client last ran: its log is written while the game runs, its settings on exit."""
    times = [0.0]
    for path in (Path(folder)/'WowUnreal/Saved/Logs/WowUnreal.log', settings_file(folder)):
        try:
            if path: times.append(path.stat().st_mtime)
        except OSError: pass
    return max(times)

def client_folders(project):
    """Every client copy in the project (dist-play, dist), most recently used first."""
    folders = [Path(project)/name for name in CLIENT_NAMES if settings_file(Path(project)/name)]
    return sorted(folders, key=last_used, reverse=True)

def client_folder(project, client=None):
    """The game folder to read: --client if given, else the copy that was used last (dist-play or dist).
    r463 (Stefan 13.10: moved the character, still logged in at Northshire): the tool always took
    dist-play while he played from dist, so it edited a roster the game never read."""
    project = Path(project)
    names = [client] if client else list(CLIENT_NAMES)
    if not client:
        found = client_folders(project)
        if found: return found[0]
    for name in names:
        folder = Path(name) if Path(name).is_absolute() else project/name
        if settings_file(folder): return folder
    tried = ', '.join(str((Path(n) if Path(n).is_absolute() else project/n)/SETTINGS_FILES[0]) for n in names)
    raise FileNotFoundError('No client settings found. Start the game once from the client folder first. Looked for: '+tried)

class Archives:
    def __init__(self, project, client=None):
        self.project = Path(project)
        self.client = client_folder(project, client)
        cfg = settings_file(self.client).read_text(encoding='utf-8-sig')
        match = re.search(r'^DataSource=(.*)$',cfg,re.M)
        if not match: raise ValueError('The Data folder is missing from client settings.')
        self.root = Path(match[1].strip().strip('"'))
        dllpath = self.project/'Source/ThirdParty/StormLib/bin/StormLib_dll/x64/Release/StormLib.dll'
        self.dll = d = c.WinDLL(str(dllpath))
        ptr = c.c_void_p
        for name in ['SFileOpenArchive','SFileOpenFileEx','SFileReadFile','SFileCloseFile','SFileCloseArchive']:
            getattr(d,name).restype = c.c_bool
        d.SFileOpenArchive.argtypes = [c.c_wchar_p,c.c_uint32,c.c_uint32,c.POINTER(ptr)]
        d.SFileOpenFileEx.argtypes = [ptr,c.c_char_p,c.c_uint32,c.POINTER(ptr)]
        d.SFileGetFileSize.argtypes = [ptr,c.POINTER(c.c_uint32)]
        d.SFileGetFileSize.restype = c.c_uint32
        d.SFileReadFile.argtypes = [ptr,ptr,c.c_uint32,c.POINTER(c.c_uint32),ptr]
        d.SFileCloseFile.argtypes = [ptr]
        d.SFileCloseArchive.argtypes = [ptr]
        original = [f'{loc}/patch-{loc}{version}.mpq' for loc in ['enUS','enGB'] for version in ['-3','-2','']]
        original += ['patch-3.mpq','patch-2.mpq','patch.mpq']
        for expansion in ['lichking','expansion']:
            original += [f'{loc}/{expansion}-{kind}-{loc}.mpq' for loc in ['enUS','enGB'] for kind in ['locale','speech']]
            original += [expansion+'.mpq']
        original += [f'{loc}/{kind}-{loc}.mpq' for loc in ['enUS','enGB'] for kind in ['locale','base','speech','backup']]
        original += ['common.mpq','common-2.mpq']
        ranks = {name.lower():i for i,name in enumerate(original)}
        def priority(p):
            rel=p.relative_to(self.root).as_posix().lower()
            if rel in ranks: return (1,ranks[rel],[])
            if re.search(r'dede|frfr|eses|esmx|ruru|kokr|zhcn|zhtw|ptbr|itit',rel): return (2,0,[rel])
            # Custom patches precede original archives, matching the client.
            natural=[(0,-int(v)) if v.isdigit() else (1,tuple(-ord(ch) for ch in v)) for v in re.split(r'(\d+)',rel)]
            return (0,0,natural)
        self.handles = []
        for path in sorted(self.root.rglob('*.MPQ'),key=priority):
            h = ptr()
            if d.SFileOpenArchive(str(path),0,0x100,c.byref(h)): self.handles.append(h)
        if not self.handles: raise ValueError('No readable MPQ archives found in the Data folder.')

    def read(self, name):
        f = c.c_void_p()
        for h in self.handles:
            if not self.dll.SFileOpenFileEx(h,name.encode(),0,c.byref(f)): continue
            try:
                size = self.dll.SFileGetFileSize(f,None)
                if size > 128*1024*1024: raise ValueError('Unexpectedly large data file')
                buf = c.create_string_buffer(size); count = c.c_uint32()
                if self.dll.SFileReadFile(f,buf,size,c.byref(count),None) and count.value == size:
                    return buf.raw
            finally: self.dll.SFileCloseFile(f)
        raise FileNotFoundError(name)

    def close(self):
        for h in self.handles: self.dll.SFileCloseArchive(h)
        self.handles.clear()

class Dbc:
    def __init__(self, raw):
        magic,n,fields,size,strings = struct.unpack_from('<4s4I',raw)
        if magic != b'WDBC' or size != fields*4 or len(raw)<20+n*size+strings:
            raise ValueError('Invalid DBC file')
        self.rows = [struct.unpack_from('<'+'I'*fields,raw,20+i*size) for i in range(n)]
        self.strings = raw[20+n*size:20+n*size+strings]

    def text(self, offset):
        return self.strings[offset:].split(b'\0',1)[0].decode('utf8',errors='replace')

def as_float(value): return struct.unpack('<f',struct.pack('<I',value))[0]

class GameData:
    def __init__(self, project, client=None):
        self.mpq = Archives(project, client)
        def dbc(name): return Dbc(self.mpq.read('DBFilesClient\\'+name+'.dbc'))
        maps = dbc('Map')
        self.maps = {r[0]: maps.text(r[1]) for r in maps.rows if r[0] in CONTINENTS}
        areas = dbc('AreaTable')
        self.areas = {r[0]:dict(map=r[1],parent=r[2],name=areas.text(r[11])) for r in areas.rows}
        world = dbc('WorldMapArea')
        self.zones = []
        for r in world.rows:
            if r[1] not in CONTINENTS or not r[2] or r[2] not in self.areas: continue
            bounds = list(map(as_float,r[4:8]))
            if not all(math.isfinite(v) for v in bounds) or bounds[0]==bounds[1] or bounds[2]==bounds[3]: continue
            self.zones.append(dict(map=r[1],area=r[2],world_area=r[0],name=self.areas[r[2]]['name'],folder=world.text(r[3]),bounds=bounds))
        self.zones.sort(key=lambda z:z['name'])
        self.sections = dbc('CharSections').rows
        self.hair = dbc('CharHairGeosets').rows
        self.facial = dbc('CharacterFacialHairStyles').rows
        self.section_dbc = dbc('CharSections')
        self.overlays = dbc('WorldMapOverlay')
        self._maps = {}
        self.surfaces = Surfaces(self.mpq)

    def appearance(self, char, key):
        rows = [r for r in self.sections if r[1]==char['race'] and r[2]==char['gender'] and r[7]&1]
        if key=='skin': values = {r[9] for r in rows if r[3]==0}
        elif key=='face': values = {r[8] for r in rows if r[3]==1 and r[9]==char['skin']}
        elif key=='hairColor': values = {r[9] for r in rows if r[3]==3 and r[8]==char['hairStyle']}
        elif key=='hairStyle': values = {r[3] for r in self.hair if r[1]==char['race'] and r[2]==char['gender']}
        else: values = {r[2] for r in self.facial if r[0]==char['race'] and r[1]==char['gender']}
        return sorted(values) or [0]

    def face_texture(self, char):
        for r in self.sections:
            if r[1:4] == (char['race'],char['gender'],1) and r[8:10] == (char['face'],char['skin']) and r[7]&1:
                parts=[]
                for off in r[4:6]:
                    if off:
                        parts.append(Image.open(io.BytesIO(self.mpq.read(self.section_dbc.text(off)))).convert('RGBA'))
                if parts:
                    out=Image.new('RGBA',(max(p.width for p in parts),sum(p.height for p in parts)))
                    y=0
                    for p in parts: out.paste(p,(0,y)); y+=p.height
                    return out
        return None

    def map_image(self, zone):
        folder=zone['folder']
        if folder in self._maps: return self._maps[folder].copy()
        out=Image.new('RGB',(1024,768),'#252b2c')
        for i in range(12):
            name=f'Interface\\WorldMap\\{folder}\\{folder}{i+1}.blp'
            piece=Image.open(io.BytesIO(self.mpq.read(name))).convert('RGB')
            out.paste(piece,((i%4)*256,(i//4)*256))
        # Base sheets are deliberately blank in unexplored areas. Compose all
        # authored exploration layers so this testing map is fully revealed.
        for row in self.overlays.rows:
            if row[1]!=zone['world_area']: continue
            texture=self.overlays.text(row[8]); width,height,ox,oy=row[9:13]
            columns=(width+255)//256; rows=(height+255)//256
            for y in range(rows):
                for x in range(columns):
                    path=f'Interface\\WorldMap\\{folder}\\{texture}{y*columns+x+1}.blp'
                    piece=Image.open(io.BytesIO(self.mpq.read(path))).convert('RGBA')
                    piece=piece.crop((0,0,min(256,width-x*256),min(256,height-y*256)))
                    out.paste(piece,(ox+x*256,oy+y*256),piece)
        out=out.crop((0,0,1002,668))
        if len(self._maps)>12: self._maps.clear()
        self._maps[folder]=out
        return out.copy()

    @staticmethod
    def map_position(zone,u,v):
        left,right,top,bottom=zone['bounds']
        return top+(bottom-top)*v, left+(right-left)*u

    @staticmethod
    def map_uv(zone,x,y):
        left,right,top,bottom=zone['bounds']
        return (y-left)/(right-left),(x-top)/(bottom-top)

    def ground_options(self,map_id,x,y):
        if map_id not in self.maps or not all(math.isfinite(v) for v in [x,y]): raise ValueError('Invalid map point')
        tx,ty=math.floor((32*TILE-y)/TILE),math.floor((32*TILE-x)/TILE)
        if not(0<=tx<64 and 0<=ty<64): raise ValueError('The point is outside the continent.')
        folder=self.maps[map_id]
        raw=self.mpq.read(f'World\\Maps\\{folder}\\{folder}_{tx}_{ty}.adt')
        floors=self.surfaces.floors(raw,x,y)
        if not floors: return [self.ground(map_id,x,y)]
        cx=int(((32*TILE-y)-tx*TILE)/(TILE/16)); cy=int(((32*TILE-x)-ty*TILE)/(TILE/16))
        area=next((struct.unpack_from('<I',d,52)[0] for tag,d in chunks(raw)
                   if tag==b'KNCM' and struct.unpack_from('<II',d,4)==(cx,cy)),0)
        # These are explicit surface choices, never an automatic sky-to-roof snap.
        return [dict(map=map_id,zone=area,x=x,y=y,z=z+.02) for z in floors]

    def ground(self,map_id,x,y):
        if map_id not in self.maps or not all(math.isfinite(v) for v in [x,y]): raise ValueError('Invalid map point')
        ax,ay=32*TILE-y,32*TILE-x
        tx,ty=math.floor(ax/TILE),math.floor(ay/TILE)
        if not(0<=tx<64 and 0<=ty<64): raise ValueError('The point is outside the continent.')
        folder=self.maps[map_id]
        try: raw=self.mpq.read(f'World\\Maps\\{folder}\\{folder}_{tx}_{ty}.adt')
        except FileNotFoundError: raise ValueError('No terrain exists here. Select a point on land.')
        cx,cy=int((ax-tx*TILE)/(TILE/16)),int((ay-ty*TILE)/(TILE/16))
        for tag,data in chunks(raw):
            if tag!=b'KNCM' or struct.unpack_from('<II',data,4)!=(cx,cy): continue
            area=struct.unpack_from('<I',data,52)[0]
            holes=struct.unpack_from('<H',data,60)[0]
            u=(ax-tx*TILE-cx*TILE/16)/(TILE/128)
            v=(ay-ty*TILE-cy*TILE/16)/(TILE/128)
            ix,iy=min(7,int(u)),min(7,int(v)); u-=ix; v-=iy
            if holes & (1<<((iy//2)*4+ix//2)): raise ValueError('This point is a terrain hole or interior. Select a point outside the building.')
            off=struct.unpack_from('<I',data,20)[0]-8
            if data[off:off+4]!=b'TVCM': raise ValueError('Could not read terrain height.')
            heights=struct.unpack_from('<145f',data,off+8)
            base=struct.unpack_from('<f',data,112)[0]
            a=heights[iy*17+ix]; b=heights[iy*17+ix+1]
            cc=heights[(iy+1)*17+ix]; d=heights[(iy+1)*17+ix+1]; mid=heights[iy*17+9+ix]
            # Four authored triangles around the central vertex, not bilinear terrain.
            if v<=u and v<=1-u: h=a*(1-u-v)+b*(u-v)+mid*2*v
            elif u>=v and u>=1-v: h=b*(u-v)+d*(u+v-1)+mid*2*(1-u)
            elif v>=u and v>=1-u: h=cc*(v-u)+d*(u+v-1)+mid*2*(1-v)
            else: h=a*(1-u-v)+cc*(v-u)+mid*2*u
            z=base+h
            if not math.isfinite(z): raise ValueError('Invalid terrain height.')
            return dict(map=map_id,zone=area,x=x,y=y,z=z+1.0)
        raise ValueError('Terrain tile missing.')

def client_running():
    result=subprocess.run(['tasklist','/FO','CSV','/NH'],capture_output=True,text=True,check=True,creationflags=0x08000000)
    return any(row and row[0].lower() in {'wowunreal.exe','wowunreal-win64-development.exe','unrealeditor.exe'} for row in csv.reader(result.stdout.splitlines()))

class CharacterStore:
    def __init__(self,path): self.path=Path(path); self.load()
    def load(self):
        self.original=self.path.read_bytes()
        self.characters=json.loads(self.original.decode('utf-8-sig'))
        if not isinstance(self.characters,list) or not all(isinstance(c,dict) and 'guid' in c and 'name' in c for c in self.characters): raise ValueError('Invalid offline character file.')
        if len({c['guid'] for c in self.characters})!=len(self.characters): raise ValueError('Duplicate characters in the file.')
    def save(self,characters,guard=client_running):
        if guard(): raise ValueError('This character is in the world right now. Log out first, or use "Move here now" for a live move.')
        if self.path.read_bytes()!=self.original: raise ValueError('The character file changed after it was opened. Reload it first.')
        encoded=(json.dumps(characters,ensure_ascii=False,indent=2)+'\n').encode('utf8')
        backup=self.path.with_name(self.path.stem+'.before-editor-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'.json')
        shutil.copy2(self.path,backup)
        fd,temp=tempfile.mkstemp(prefix='.offline-editor-',suffix='.tmp',dir=self.path.parent)
        try:
            with os.fdopen(fd,'wb') as f: f.write(encoded); f.flush(); os.fsync(f.fileno())
            if guard() or self.path.read_bytes()!=self.original: raise ValueError('The client or character file changed while saving. Reload before trying again.')
            os.replace(temp,self.path)
        finally:
            if os.path.exists(temp): os.unlink(temp)
        self.load()
        return backup

BRIDGE='PrivateTools/OfflineCharacterEditor/Bridge'

class LiveBridge:
    # r462 (Stefan 13.10: "Log out to character selection" even at the login screen and at character
    # selection): a packaged client writes its bridge under its own project folder
    # (dist-play\WowUnreal\PrivateTools\...), while the tool only looked in the repository root, so it
    # never saw the client and refused every save while the game ran. Both folders are used now.
    def __init__(self,project,client=None):
        clients=[] if client is None else [client] if isinstance(client,(str,Path)) else list(client)
        folders=[Path(c)/'WowUnreal'/BRIDGE for c in clients]+[Path(project)/BRIDGE]
        self.directories=list(dict.fromkeys(folders)); self.directory=self.directories[0]
    def enable(self):
        for folder in self.directories:
            try: folder.mkdir(parents=True,exist_ok=True); (folder/'enabled.txt').touch(exist_ok=True)
            except OSError: pass

    def editing_blocked(self, character_file, guid=None):
        # The client re-reads the roster at login, at character selection and when entering the world,
        # and a logout re-reads the file and stores only that character's position. So saving is safe
        # everywhere except for the character that is in the world from this file right now.
        if not client_running(): return False
        state=self.status()
        if not state or 'guid' not in state: return False
        try: same_file=Path(state['characterFile']).resolve()==Path(character_file).resolve()
        except (KeyError,TypeError,OSError): same_file=True
        return same_file and (guid is None or state.get('guid')==guid)

    def status(self):
        best=None
        for folder in self.directories:
            try:
                status=json.loads((folder/'status.json').read_text(encoding='utf-8-sig'))
                age=time.time()-float(status['timestamp'])
                if not status.get('offline') or not 0<=age<5 or not status.get('session'): continue
                if best is None or float(status['timestamp'])>float(best[1]['timestamp']): best=(folder,status)
            except (OSError,ValueError,KeyError,TypeError): continue
        if not best: return None
        self.directory=best[0]
        return best[1]
    def fly(self,guid,on):
        # r528: offline world only; the client refuses it with a server connection.
        status=self.status()
        if not status or not status.get('ready'): raise ValueError('No ready offline client connected. Start a supported client in offline mode.')
        if status['guid']!=guid: raise ValueError('Select the same character in the tool and the client.')
        request=dict(action='fly',on=bool(on),id=str(uuid.uuid4()),session=status['session'],guid=guid,expires=time.time()+20)
        temp=self.directory/('request-'+request['id']+'.tmp')
        try:
            temp.write_text(json.dumps(request),encoding='utf8')
            os.replace(temp,self.directory/'request.json')
        finally:
            if temp.exists(): temp.unlink()
        return request['id']
    def teleport(self,guid,position):
        status=self.status()
        if not status or not status.get('ready'): raise ValueError('No ready offline client connected. Start a supported client in offline mode.')
        if status['guid']!=guid: raise ValueError('Select the same character in the tool and the client.')
        request=dict(action='teleport',id=str(uuid.uuid4()),session=status['session'],guid=guid,expires=time.time()+20)
        request.update({k:position[k] for k in ['map','x','y','z']})
        if request['map'] not in CONTINENTS or not all(math.isfinite(request[k]) for k in ['x','y','z']): raise ValueError('Invalid starting location.')
        temp=self.directory/('request-'+request['id']+'.tmp')
        try:
            temp.write_text(json.dumps(request),encoding='utf8')
            os.replace(temp,self.directory/'request.json')
        finally:
            if temp.exists(): temp.unlink()
        return request['id']
